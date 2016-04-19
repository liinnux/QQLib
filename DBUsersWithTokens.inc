<?php
namespace QQAPI;

include_once "DBUsers.inc";

/**
* This adds to the DBUsers table the fields for "stay-logged-in" cookies.
*
 * There are two types of token login being supported here.
 * token1/token2/tokenexpires are for stay-logged-in: They are set on request (e.g. by the user
 * checking a box when logging in), allowing you to send back a cookie to the user. This is not
 * removed from the DB when used, but persists until expired.
 * All three fields are NULL if the user does not request stay-logged-in, or after they have
 * expired.
 * 
 * Then `tokenloginas` is used to store a special one-off login URL. These only last
 * a short amount of time (60-120s), and are intended for an administrator to be able to login as
 * a user. The code contains the expiry, so there is no separate expiry field; also they stay
 * in the DB even after being used.
 * 
 * @internal This code based on the following articles:
 *    https://paragonie.com/blog/2015/04/secure-authentication-php-with-long-term-persistence
 *    http://stackoverflow.com/q/244882/841830
 *    http://stackoverflow.com/q/549/841830
 *    http://security.stackexchange.com/a/63438/31291
*/
class DBUsersWithTokens extends DBUsers{

/**
* Creates the empty table
* 
* Note: this modifies extraFields; however it is safe to call it more than once.
* Note: token2 is 26 bytes, but we store the md5() hash of token2 here, so that is why we
*   need 32 bytes.
*/
public function createTable(){
$s = <<<EOD
  token1 VARCHAR(6) UNIQUE,
  token2 VARCHAR(32),
  tokenexpires DOUBLE,
  tokenloginas VARCHAR(32)
EOD;
if(strpos('token1 VARCHAR(', $this->extraFields) === FALSE){  
    if($this->extraFields)$this->extraFields = $s. ",\n" . $this->extraFields;
    else $this->extraFields = $s;
    }
//else (most likely) this is second call of this function.
parent::createTable();
}

/** 
 * Add a new random token to the table, and return it
 * 
 * @param string $email The user to modify and fetch
 * @param string $token The token to use
 * @param int $validitySecs The number of seconds the token is valid for. Added to $now.
 *     E.g. 1209600 for 2 weeks.
 * @param numeric $now The current time. (Give as default of false to set to current time.)
 * 
 * @todo a createTokenGivenId() version?
 *  ---> Or generalize to take $keyField, $keyValue  (i.e. just like updateRecordAndFetch() does)
 */
public function createTokenGivenEmail($email, $validity, $now = false){
if(!$now)$now = microtime(true);
$token = base64_encode(openssl_random_pseudo_bytes(24)); //Will be 32 characters long, I think. (24 + 33%)
$token1 = substr($token,0,6); //64 ^6 = 68.7 billion combinations. I.e. 6 character is sufficient
$token2 = substr($token,6,26);
$d=array(
    'token1' => $token1,
    'token2' => md5($token2),  //Only the hash is stored in the DB
    'tokenexpires' => $now + $validity,
    );
$this->updateRecord($this->tableName, $d, 'email', $email, $now);
return $token1.':'.$token2; //Don't need a separator, as token1 is fixed length. But it does no harm,
    //and allows a validation check.
}

/**
 * Now the lookup function
 * 
 * @todo Rename to findUserByCookieToken? That differentiates from the GET-based token methods?
 * 
 * @param Number $now If false, then set to the current time. (This is just here for unit testing.)
 * 
 * @return Mixed On failure a string error message (suitable for logging, not for giving to
 *    an end user). On success an array, representing the user.
 *    On success, but expired, false is returned (indicating it should redirect to the
 *    login page).
 *    Some DB errors might cause an exception to be found, but "token1 not found"
 *    and "token2 does not match" will be done as a string return.
 * 
 * @internal If 2+ rows found it just uses one of them. (Should never happen
 *   if the unique index is working correctly.)
 */
public function findUserByToken($token1,$token2, $now=false){
//Lookup by token1
$d = $this->getAllForKey($this->tableName, 'token1', $token1);
if(count($d)==0)return "Token1 ({$token1}) not found in {$this->tableName}";
$user = array_pop($d);

//If found, match token2
if(@$user['token2'] != md5($token2)){
    //Very unlikely: it implies a hacker has guessed a token1, but does not know token2.
    return "Token2 ({$token2}) not matched: token1={$token1}, id={$user['id']}, email={$user['email']}";
    }

//Check expiry
if($now===false)$now = time();
if($now > $user['tokenexpires'])return false;
    
return $user;
}



/**
 * Chooses a new value for the salt field, used in token login, and returns it
 * 
 * @param string $email Used to decide which member record is being updated.
 */
public function chooseNewToken($email){
$salt = base64_encode(openssl_random_pseudo_bytes(24)); //Will be 32 characters long, I think. (24 + 33%)
$this->updateOneField($this->tableName, 'tokenloginas', $salt, "email", $email);
return $salt;
}

/**
 * Like chooseNewToken(), but returns the whole member record
 */
public function chooseNewTokenAndFetch($email){
$salt = base64_encode(openssl_random_pseudo_bytes(24)); //Will be 32 characters long, I think. (24 + 33%)
return $this->updateRecordAndFetch($this->tableName, array('tokenloginas'=>$salt), "email", $email);
}

//TODO: might make sense to move some other authentication functions in here too?
//---> There is so little code, it is hard to do it. Better to show some example usage?


}
