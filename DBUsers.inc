<?php
namespace QQAPI;

include_once "PDOWrapper.inc";

/**
* This is for a database used to store users or members.
*
* It is a minimal example of what is needed to work with signupDB, and the forgotpw
* and changeEmail routes. However the "extra" field could be used to store a json object
* (or csv string, etc.) for a load of custom fields.
*
* This database just has one table, "users". See createTable() for fields and their types.
* Define extraFields in the constructor of your derived class if you want more fields (or
* override createTable()).
*/
class DBUsers extends PDOWrapper{
/**
* The table name.
*
* Change this in the constructor of your derived class, if you need something different.
*
* @internal This was added so that you can keep two different user tables in the same
*   DB, or just simply to work with an existing table.
*/
protected $tableName = "users";

/**
* Defaults to false, meaning email is the only way to fetch data. If you set this
* to true then each new record is also assigned an id.
 */
protected $withId = false;

/**
 * If true then "lastLogin" and "currentSession" are created in the DB, and the `recordLogin()`
 * and `recordLogout()` functions do something.
 * 
 * The main motivation for this is to be able to track multiple logins. When processing a login
 * you look at `currentSession`. If not null it tells you either they didn't logoff their previous
 * session properly, or they are currently logged in (i.e. in another browser or on another device).
 * Depending on the application you then have numerous choices:
 *   1. Give an error message, telling them to log-off. (This is bad as maybe they just forgot
 *        to log-off, and no longer have the device/browser open.)
 *   2. Delete the old session file directly off disk/DB.
 *   3. Delete the old session through PHP (either launch a commandline PHP script to do that,
 *        or see http://php.net/manual/en/function.session-destroy.php#114709 for one way to do it
 *        within the current script (i.e. save current session, destroy other session, restore new session).
 *   4. Do a curl request to the logout route, using a cookie with the old session id.
 *  Note that only solution 4 handles the case of a load balancer sitting in front of multiple web
 *  servers, where the load balancer does sticky sessions.
 *  (See http://stackoverflow.com/q/10715461/841830 for more suggestions on PHP sessions over
 *   a cluster. Option 2 above seems like it might be a better fit.)
 * 
 * Additional benefits of this are that you can review the DB to find out the last login. As it
 * does not touch the `updated` field, you can see last login, and last user account editing,
 * distinctly. You can also get an estimate of currently logged in users, by counting non-NULL
 * sessions.
 * 
 * The main downside is that it requires a DB write each time you process a login, and a logout.
 * 
 * Another downside is the extra couple of fields in a DB (though that is very minor).
 * 
 * Note: this design is not ready-to-go for the case of *allowing* multiple logins. E.g. allowing a
 * simultaneous login from a desktop and from a smartphone. You could do it by storing the sessions as
 * a csv list (or json array). But `recordLogin()` and `recordLogout()` parameters would need to change
 * to cope. (The bigger challenge is sorting out the data sync issues; though if only one of the
 * logins is allowed to write to the DB, that is a little easier.)
 * 
 * 
 * @internal I made this public, rather than protected like `withId` and `extraFields`, as it is
 *   reasonable an application might want to use `DBusers` with just this modified.
 *   The only concern was that it must not be false when createTable() is called, then true
 *   afterwards: certain actions would fail in that case.  (The reverse is fine - that would
 *   simply mean the fields are in the DB table but are not being used.)
 *   Another advantage of making it public, and defaulting it to true, is that the table can
 *   be created with these fields, but if an application wants to turn off the `lastLogin`
 *   updating (e.g. temporarily, perhaps due to high load) then it can.
 * 
 * @internal I considered a single field, perhaps json, storing both login time and session. This
 *   could be used for extra information in future. However it is quite useful to have login time
 *   and session as distinct fields at the SQL level; this was considered worth more than some
 *   potential future flexibility.
 */
public $recordLogin = true;


/**
* This is the SQL, for CREATE TABLE, to describe extra fields we use.
*
* It would be quite common to at least use "name TEXT,lang VARCHAR(2)" here.
* If you want "id" it is better to set $withId to true, than specify it here (see getCreateTableSQLForId()
*   in PDOWrapper, for why).
*
* NOTE: the alternative to using this is to simply override createTable().
*
* @internal I decided this should be a field, with no unused extras at all, on the
*    don't pay for what you don't use principle. But still it is very easy to add more
*    fields. However "id" is special, e.g. it may become DB-specific, so has been done separately.
*/
protected $extraFields = "";


/**
* Creates the empty table (see class comments for  DB structure)
*
* @internal I use "DOUBLE" for the timestamps, for two reasons of portability (e.g. sqlite has
*  no date/time data type), and for precision.
*
* @internal We use TEXT type for all fields, as they are flexible with no downside.
*    However in some DBs we cannot make a TEXT column a primary key.
*    Therefore we put a partial index on the email column, but don't flag it as the primary key.
*/
public function createTable(){
if($this->checkForTable($this->tableName) !== null)return;
$q = "CREATE TABLE {$this->tableName}(";
if($this->withId)$q.=$this->getCreateTableSQLForId();
$q .= <<<EOD
  created DOUBLE NOT NULL,
  updated DOUBLE,
  email TEXT NOT NULL,
  password TEXT NOT NULL
EOD;
if($this->recordLogin)$q.=",lastLogin DOUBLE,currentSession TEXT";
if($this->extraFields)$q.=",".$this->extraFields;
$q.=")";
$this->get()->exec($q);
$this->addUniquePartialIndex($this->tableName, 'email', 20);   //Only index first 20 characters is sufficient.
}


/** 
* Add a new user to the table
*
* @param Array $d This is for a derived class to specify its custom fields, but also
*    be able to share this base class version.  
*/
public function createUser($now, $email, $password, $d = array()){
$d += array(
    'created'=>$now,
    'updated'=>$now,
    'email'=>$email,
    'password' => password_hash($password, PASSWORD_DEFAULT),
    );
$this->insertRow($this->tableName, $d);
}


/**
* Lookup, using the email address
* 
* @return Mixed Returns an array (for the DB row) or false if not found.
*
* @internal If 2+ rows found it just returns one of them. (Should never happen
*   if the unique index is working correctly.)
*/
public function findUserByEmail($email){
$d = $this->getAllForKey($this->tableName, 'email', $email);
if(count($d)==0)return false;
return array_pop($d);
}

/**
* Delete a user, if there.
*
* Note: if not there, behaviour is identical (no error thrown).
*/
public function deleteUserByEmail($email){
$q = 'DELETE FROM '.$this->tableName.' WHERE email = ?';
$statement = $this->get()->prepare($q);
$statement->execute(array($email));
}


/**
* Hashes then sets the new password (row must already exist); updated field also gets set.
*
* @param string $pw This is the cleartext password, and it will be hashed here.
*/
public function setUserPassword($email, $pw){
$pw = password_hash($pw, PASSWORD_DEFAULT);
$this->updateOneField($this->tableName, "password", $pw, "email", $email);
}

/**
* Hashes then sets the new password (row must already exist); updated field also gets set.
*
* @param string $newEmail This is the new email address, i.e. the new key.
*/
public function setUserEmail($currentEmail, $newEmail){
$this->updateOneField($this->tableName, "email", $newEmail, "email", $currentEmail);
}


/** */
protected function fetchAll(){
$dbh = $this->get();
$q = 'SELECT * FROM '.$this->tableName;
$statement=$dbh->prepare($q);
$statement->execute(array());
return $statement->fetchAll(\PDO::FETCH_ASSOC);
}


/**
 * This should be called after a successful login; it will record the new session in the
 * DB table, as well as the time.
 * 
 * Note: because loginTime is recorded, this function deliberately does not set `updated` field.
 * 
 * Note: If $this->recordLogin is false, then it does nothing.
 * 
 * @param String $email Who logged-in.
 * @param String $session The session ID of this login.
 * @param Number $now The current time (as int or double secs since 1970). If not given
 *    then it will default to the current time.
 * 
 * @internal We don't use PDOWrapper::updateRecord(), because we don't want the `updated`
 *  field to get set. (And because the query is straightforward, so it would be overkill.)
 */
public function recordLogin($email, $session, $now=null){
if(!$this->recordLogin)return;

if(!$now)$now = microtime(true);
$q='UPDATE '.$this->tableName.' SET lastLogin = ?,currentSession = ? WHERE email = ?';
$db = $this->get();
$st = $db->prepare($q);
$st->execute( array($now, $session, $email) );
}


/**
 * This clears out the currentSession column (lastLogin is not cleared, and the time of
 * the logout is not recorded).
 * 
 * @param String $email The user who logged-out.
 */
public function recordLogout($email){
if(!$this->recordLogin)return;

$q='UPDATE '.$this->tableName.' SET currentSession = NULL WHERE email = ?';
$db = $this->get();
$st = $db->prepare($q);
$st->execute( array($email) );
}


}
