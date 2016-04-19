<?php
namespace QQAPI;

include_once "PDOWrapper.inc";

/**
* This is for the database used to store signups, as well as forgot-password and change-email requests.
*
* This database just has one table, "signups" (customizable with $this->tableName) which has these fields:
*   status: VARCHAR(20)  (or an ENUM in supporting back-ends)(or it could be a CHAR(1)
*       if some layer was going to map the statuses)
*   created: TIMESTAMP
*   updated: TIMESTAMP
*   email:  VARCHAR(120)
*   code:   CHAR(64)
*   origin: TEXT
*   extra: TEXT
* (see the createTable() function source for the actual SQL and the actual field types)
*
* There will be another DB (typically DBUsers, or a class derived from that) which will contain
* a "users" table that has at least these fields:
*   email
*   password   (a hashed password)
*   updated
*
* NOTE: one design decision is that the "signups" table contains all the email addresses
* in the main "users" table (they will all be there with a "processed" status); i.e. they are
* not deleted from this table once they successfully sign-up. This creates the risk of the two
* tables going out of sync; in particular when a user is deleted from "users" we require
* that their status is changed to "deleted".
*
* Status can have the following values for sign-ups:
*   pending: Sign-up request has been received (and a URL sent by email)
*   bademail: We change an entry from pending to bademail if we got a bounce email back.
*   processing: The user clicked the URL in the sign-up request email (and was shown a form
*        asking password, and maybe other stuff)
*   processed: The user submitted the password form, and an entry in
*       "users" table was created successfully.
*   deleted: The user has been created, but was deleted from the main DB.
*  --> For these "created" is the time it was set to "pending", and "updated" is the time
*        it was set to any of the others.
*  --> origin is any tracking special-offer codes, etc. that were in the initial URL
*
* Status can have the following value for people using the forgot-pw form:
*   pwrequested: They made the request (gave their email address), and we found them
*         with "processed" status in this table. No changes made to "user" table.
*   pwbademail: As bademail above.
*   pwprocessing: They clicked the URL in the email, their password has been
*         blanked (i.e., account locked) in the main DB (and they were given a form
*         asking for the new password to set).
*   pwprocessed: They submitted the new password form. "users" table was updated.
*  --> For these "created" is the time it was set to "pwrequested", and "updated" is the time
*        it was set to any of the others.
*
* Status can have the following values, for people making a change-email request:
*   empending: They have changed their email address to 'email', and an email
*       has been sent to their new email address to confirm it.
*       (The origin field will contain their old email address; the "extra" field can
*       be used for anything else about their request.)
*   embademail: As bademail above.
*   emconfirmed: The user clicked the confirmation email in the database, and the "users"
*       table was successfully updated.
*  --> For these "created" is the time it was set to "empending", and "updated" is the time
*        it was set to any of the others. Also the "origin" field is used for the previous email address.
*
* Note: for an properly signed-up user, there will be exactly one "processed" entry
*   and zero or more pwprocessed entries, and zero or more emconfirmed enties,
*   for any given email address. (NOTE: there can be 2+ emconfirmed entries: e.g. if they
*   have changed from a@x to b@x then back to a@x then to c@x then back to a@x again, etc.) 
*
* Note: entries with any of the following statuses are deleted once they are over N minutes old:
*   pending, bademail, processing, pwrequested, pwbademail, pwprocessing, empending, embademail.
*
* NOTE: it is allowed that these clean-up actions can happen:
*   1. 'pending' entries that are over N seconds old. E.g. if the URL in an email has not been clicked
*           after 24 hours then it is probably not going to be.
*   2. Ditto for 'bademail'. 
*   3. Ditto for 'processing'. I.e. the URL in an email was clicked, but never followed through on. But
*           I'd set a higher N for these: the user intended to sign-up, but perhaps got interrupted, so they
*           might come back to the email in the next day or two?
*  ---> The point of all these is a new signup request would replace the row with status=pending, so
*       there is no harm in getting rid of them. Expiring them might be more secure (e.g. an attacker
*       does not have access to a user's live email, but has got hold of an old email backup, and could
*       use the URL to complete the sign-up process.)
*
*
* @todo IMPORTANT When email address is changed, should we leave behind an entry for
*   the previous email address, the same way we have a "deleted" status?
*   ---> This is important. It means if someone changed from a@b.com to x@y.com they might
*       be doing it deliberately to free up a@b.com for another account. We've confirmed that
*       x@y.com is valid, so there is no reason we shouldn't allow a@b.com to be reused.
*       (It could be that the person responsible for a certain account has been changed
*       from fred@example.com to bob@example.com; then tom@example.com might be given
*       to fred@example.com, and then a 3rd new account might be created for tom@example.com
*       to manage.)
*
* @todo I'd like to store IP address and user-agent. Should they be in their own fields, or
*       should they be bundled into the 'extra' field? (The advantage of their own field is we
*       can directly query on them.)
*       However, e.g. "processed" has involved 3 distinct requests. Which do we store (first, last, all)?
*       If using "extra", then we could just keep appending.
*       ---> The question also becomes: do we really need all that? Does it duplicate what will
*           already be in log files? Or should we have our own log file for this data, with just enough
*           data to tie it back to this.
*
* @internal I've made some functions private, that could be public but are not currently
*   being used directly, only via the higher-level functions.
*/
class DBSignups extends PDOWrapper{
/**
* The table name.
*
* @internal This is public to save an application having to derive from this class just to
*   change the tablename.
*/
public $tableName = "signups";

/**
* Change this if you want shorter codes than your hash function is returning. You might set
* this if you want the URLs to be shorter.
*
* NOTE: the alternative way to do this is to set up a custom codeGenerate function.
*
* @see $codeMinLength, $codeMaxLength This value must be inside that range.
*/
public static $codeLength = 64;

/**
* This is the maximum code length that will ever be used.
*
* It is used to make the field size in the table, and for error-checking.
* It is not recommended to change it from the default given here, and it has
* been made protected for that reason.
* If making your DB table externally, not calling createTable(), then make sure
* the field size is the same as this.
*/
protected static $codeMaxLength=64;

/**
* This is the shortest a code is allowed to be.
*
* It is only used in error-checking. Make sure $codeLength is never set to less
* than this (or exceptions will be thrown whenever anyone tries to use a code).
*
* The point of having a min length is to avoid codes of insecure length.
*/
protected static $codeMinLength=32;

//------------------------------------------------

/**
* Creates the empty table (see class comments for  DB structure)
*
* @internal I use "DOUBLE" for the timestamps, for two reasons. First it is more portable (sqlite
*  has no date/time data types, mysql TIMESTAMP has automatic behaviours, etc.).
*  Second the route base class is giving us a timestamp as unix time, but with a fractional
*  part to microsecond accuracy. We might as well keep that level of accuracy.
*  In sqlite DOUBLE maps to "REAL", which is 8 bytes.
*  In MySQL DOUBLE is 8 bytes.
*  Postgresql appears to have DOUBLE meaning 8 bytes.
*
* @todo
*  In SQL Server there is no DOUBLE, you need to write FLOAT(53).
*  In Oracle it is unclear if DOUBLE is 8 bytes; the BINARY_DOUBLE type appears to be.
*
* @internal We use TEXT type for all fields, to take advantage of their flexible size.
*  There is little performance difference compared to varchar (none on sqlite and postgresql,
*   a minor one on MySQL), see: http://stackoverflow.com/a/1203785/841830
*   NOTE: VARCHAR(64) is used for code; 32 is enough for md5, 40 enough for
*   sha1, and 64 enough for sha256 (as well as host of other hash functions). It was not
*   possible to use a TEXT type and still make it the primary key.
*
* @internal I've made 'code' be the primary key. Theoretically it is not unique, but we assume
*    that the hash function (md5, sha256, etc.) will never give us the same return value more than once.
*
* @internal I use self::$codeMaxLength to set the size of `code`, instead of
*	hard-coding to VARCHAR(64).
*/
public function createTable(){
if($this->checkForTable( $this->tableName ) !== null)return;
$len = self::$codeMaxLength;    //Using a self:$XXX inside a heredoc didn't work.
$q = <<<EOD
CREATE TABLE {$this->tableName}(
  created DOUBLE NOT NULL,
  updated DOUBLE,
  status TEXT NOT NULL,
  email TEXT NOT NULL,
  code VARCHAR({$len}) PRIMARY KEY,
  origin TEXT,
  extra TEXT
  )
EOD;
$this->get()->exec($q);
$this->addNonUniquePartialIndex($this->tableName,'email',20);   //Only index first 20 characters is sufficient.
}


/**
* This is used specifically to set a new code for an existing 'pending' entry.
*
* If the email is there but status is not 'pending', then it will throw.
* (No return value: if it returns it means it worked.)
*
* @param String $email
* @param String $code The new code to use
* @param Number $now Unix time (might be a float, with a microseconds part)
*
* @internal The "LIMIT 1" suffix on the query is not supported in SQLite unless
*  you do a custom compile: http://www.sqlite.org/compile.html#enable_update_delete_limit
*  Unfortunately it throws a syntax error without that.
*  As we check $rowCount anyway, the LIMIT 1 part was dropped, rather than mess around
*  with detecting the SQL client.
*
* @internal I choose not to update origin/extra here. But consider if we might have
*   a need to do that. In that case, I guess $options should be passed in as a parameter.
*/
private function refreshPendingRequest($email,$code,$now){
$q = "UPDATE {$this->tableName} SET code=?,created=?,status='pending' WHERE email=? AND status IN ('pending','processing','bademail','deleted')";
$statement=$this->get()->prepare($q);
$statement->execute(array( $code, $now, $email ));
$rowCount = $statement->rowCount();
if($rowCount==1)return;
if($rowCount==0)throw new SystemException("refreshPendingRequest: Did not match given email, with expected status (q=$q, code=$code, now=$now, email=$email");
throw new SystemException("refreshPendingRequest: Found {$rowCount} matches for given email, with expected status (q=$q, code=$code, now=$now, email=$email");
}


/**
* @see refreshPendingRequest()
*
* @todo URGENT Also update if a emconfirmed row is found?
*/
private function refreshChangeEmailRequest($email,$code,$now){
$q = "UPDATE {$this->tableName} SET code=?,created=?,status='empending' WHERE email=? AND status IN ('empending','embademail')";
$statement=$this->get()->prepare($q);
$statement->execute(array( $code, $now, $email ));
$rowCount = $statement->rowCount();
if($rowCount==1)return;
if($rowCount==0)throw new SystemException("refreshForgotpwRequest: Did not match given email, with expected status (q=$q, code=$code, now=$now, email=$email");
throw new SystemException("refreshForgotpwRequest: Found {$rowCount} matches for given email, with expected status (q=$q, code=$code, now=$now, email=$email");
}


/**
* Helper function to update a row (pending->processing->processed)
*
* It expects one row (with $expectedStatus), and changes it to be $newStatus.
* 
* @param Array $row The database row that has been matched. We assume this
*       is straight from the DB, so trusted, and must be up to date.
* @param Number $now Timestamp to set as the updated field
*/
public function changeStatus($row,$now,$expectedStatus,$newStatus){
if($row['status'] != $expectedStatus)throw new SystemException("In changeStatus status is wrong (should be $expectedStatus). Here is full row:".print_r($row,true));
$q = "UPDATE {$this->tableName} SET updated=?,status=? WHERE email=? AND status=?";
$statement=$this->get()->prepare($q);
$statement->execute(array( $now, $newStatus, $row['email'], $expectedStatus ));
$rowCount = $statement->rowCount();
if($rowCount==1)return;
if($rowCount==0)throw new SystemException("Did not match given email, with expected status (q=$q, expectedStatus=$expectedStatus, newStatus=$newStatus, now=$now, email=$email");
throw new SystemException("Found {$rowCount} matches for given email, with expected status (q=$q, expectedStatus=$expectedStatus, newStatus=$newStatus, now=$now, email=$email");
}


/**
* High-level function to add a signup request to the database, as "pending".
*
* By changing $type you can also use it for forgot-password and change-email
* requests, not just signup requests.
*
* The normal case is very simple: it should find no entries in the table for the
* given email address, so it will add the pending entry. Most of the complexity
* in this function is for dealing with the cases where entries are already found.
* If the function returns then it means an entry was created or updated; for every
* other possible outcome an exception is thrown.
*
* To see the exact behaviour for each status, look at the source code, but basically
* if there is a pending or bademail request in the DB, that is over 15 minutes old,
* then the entry is updated (i.e. the old code will no longer work).
* If there is a "processing" entry it implies they requested to sign-up, we sent
* them an email, and they received it and successfully clicked the link. Now for
* some reason they have requested to sign-up again. This could mean they couldn't
* finish the processing, and lost the original email (or it has expired).
* We treat this the same as a fresh request to sign-up, and send them a new code.
*
* If a "deleted" status they've previously been fully created, but then account deleted.
* We treat this just like a re-request above, but use a higher value for N, i.e. default
* is one full day after the account was deleted before it can be recreated.
*
* @param String $type "signup", "forgotpw" or "changeemail"
* @param String $email
* @param Number $now Unix time (might be a float, with a microseconds part)
* @param Array $options These keys (and their defaults)
*       resendProcessingSecs: 900
*       resendPendingSecs: 900
*       resendBadEmailSecs: 900
*       resendDeletedSecs: 86400
*       generateCode: a function that takes $email and $now, and returns as 32 to 64 character hash
*           code (strictly $codeMinLength to $codeMaxLength characters).
*           The $now is used to add some randomness, but you could also use a salt.
*           The default version uses sha256, so returns a 64-character code.
*   NOTE: Setting any of the resendXXX variables to 0 (or -ve) will mean refresh of that status not allowed.
* @return String The code that was used and inserted in the DB.
*
* @internal Note that pending status age is measured from 'created' timestamp,
*   while the others are measured from 'updated'.
*
* @todo If the resendXXX variables are 0 (or -ve) then the error message will
*   be weird. Really need to be checking explicitly for zero values.
*
* @internal $this->startTime is a float to microsecond accuracy, so I consider
*   that to be enough random digits. However, just for extra paranoia,
*   I also use $this->salt, and if that has not been set we use a random number.
*/
public function addOrRefreshRequest($type, $email, $now, $options = array()){
$options = array_merge( array(
    'generateCode' => function($email, $now){
        $salt = mt_rand(1,4000);
        return hash("sha256", $salt.$email.(string)$now);
        },    
    'resendProcessingSecs' => 900,
    'resendPendingSecs' => 900,
    'resendBadEmailSecs' => 900,
    'resendDeletedSecs' => 86400,
    'origin' => '',
    'extra' => '',
    ),$options);    //Use defaults, and override with user-specified options.

$code = $options['generateCode']($email, $now);
$code = substr($code, 0, self::$codeLength); //Truncate

if(!preg_match('/^[0-9a-z]{'.self::$codeMinLength.','.self::$codeMaxLength.'}$/',$code))throw new SystemException("Codes must be {self::$codeMinLength} to {self::$codeMaxLength} lowercase alphanumeric characters. Pad or truncate if using a different type of code.");

switch($type){
    case 'signup':$statusList = array('processed','processing','pending','bademail','deleted');break; 
    case 'forgotpw':$statusList = array('pwrequested','pwprocessing','pwprocessed','pwbademail');break;
    case 'changeemail':$statusList = array('empending','emconfirmed','embademail');break;
    default:throw new SystemException("Bad type ($type) in addOrRefreshRequest()");
    }

$entries = $this->getAllForMultipleKeys($this->tableName, array(
    'email' => $email,
    'status' => $statusList,
    ) );

if($type == "signup")$this->handleSignupRequest($entries, $email, $code, $now, $options);
elseif($type == "forgotpw")$this->handleForgotpwRequest($entries, $email, $code, $now, $options);
else $this->handleChangeEmailRequest($entries, $email, $code, $now, $options);

return $code;
}


/**
* Helper for addOrRefreshRequest() for when doing new signups.
*
* @internal This has become irrelevent now we filter on status in the SQL (?)
if($match===null){ //Entries for $email, but none of them are for signup statuses.
    //  ---> I guess that might mean a user was a member, then changed their email address
    //   successfully. And now they (or someone else) is trying to sign-up with the previous address.
    //  ---> This might be perfectly valid, e.g. someone switching their personal account to the
    //      company email address for someone else to control, but then wanting to sign-up for themselves.
    //@todo URGENT Add either a static var or $options entry to control if this is allowed or not.
    throw new ErrorException('Your email address is not allowed to sign-up again.',array(),"Has no signup entries, but does have other entries. Investigate why. Here is full entries array for email={$email}:".print_r($entries,true));
    }
*/
private function handleSignupRequest($entries, $email, $code, $now, $options){
if(count($entries)==0){ //Easiest case, and also the most common
    $this->insertRow($this->tableName, array(
        'status' => 'pending',
        'email' => $email,
        'code' => $code,
        'created' => $now,
        'origin' => $options['origin'],
        'extra' => $options['extra'],
        ));
    return;
    }
if(count($entries) >= 2)throw new SystemException("Found a 2+ rows with signup statuses. DB table {$this->tableName} is corrupted for email={$email}. Here are all entries for that email address:".print_r($entries,true));
$match = $entries[0];
switch($match['status']){
    case 'processed':
        //Already in DB, show them the login page instead!
        //Might be because they've double-clicked the URL; or they've found an
        //old email and clicked the link. Either way they are already in the DB.
        throw new ErrorException('Already signed-up (email=%1$s)',array($email));
    case 'pending':
        $ageInSeconds = $now - $match['created'];
        if($ageInSeconds < $options['resendPendingSecs'])throw new ErrorException('Recently sent a sign-up email to this address. %1$d minutes before you can request a fresh sign-up email.',array( ceil($options['resendPendingSecs']/60) ),"ageInSeconds={$ageInSeconds}, now=$now, match=".print_r($match,true));
        break;  //Fall through and update DB below
    case 'bademail':
        $ageInSeconds = $now - $match['updated'];
        if($ageInSeconds < $options['resendBademailSecs'])throw new ErrorException('Recently sent a sign-up email to this address. %1$d minutes before you can request a fresh sign-up email.',array( ceil($options['resendBademailSecs']/60) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
        break;  //Fall through and update DB below
    case 'processing':
        $ageInSeconds = $now - $match['updated'];
        if($ageInSeconds < $options['resendProcessingSecs'])throw new ErrorException('Recently did the initial sign-up and followed the emailled link. Use that link again. Otherwise it is %1$d minutes before you can request a fresh sign-up email.',array( ceil($options['resendProcessingSecs']/60) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
        break;  //Fall through and update DB below
    case 'deleted':
        $ageInSeconds = $now - $match['updated'];
        if($ageInSeconds < $options['resendDeletedSecs'])throw new ErrorException('Recently deleted an account at this email address. %1$d hours before you can request a fresh sign-up email.',array( ceil($options['resendDeletedSecs']/3600) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
        break;  //Fall through and update DB below
    default:
        throw new SystemException("Bad status: implies coding error within addOrRefreshRequest():".print_r($match,true));
        break;
    }

$this->refreshPendingRequest($email,$code,$now);
}


/**
* Helper for addOrRefreshRequest() for when doing a forgot-password action.
*
* The basic idea is that if we find a *recent* existing entry then we complain.
* Otherwise we do nothing and simply add a new entry.
*
* @param Array $entries Basically one row for each previous time they've
*       requested to reset their password. So, it is quite typical for this to be empty.
*/
private function handleForgotpwRequest($entries, $email, $code, $now, $options){
foreach($entries as $match){
    switch($match['status']){
        case 'pwprocessed':break;   //Don't care (even if very recent)

        case 'pwrequested':
            $ageInSeconds = $now - $match['created'];
            if($ageInSeconds < $options['resendPendingSecs'])throw new ErrorException('Recently sent an email to this address. %1$d minutes before you can request another email.',array( ceil($options['resendPendingSecs']/60) ),"ageInSeconds={$ageInSeconds}, now=$now, match=".print_r($match,true));
            break;  //Otherwise it is fine, so fall through and send another below

        case 'pwbademail':
            $ageInSeconds = $now - $match['updated'];
            if($ageInSeconds < $options['resendBademailSecs'])throw new ErrorException('Recently sent an email to this address. %1$d minutes before you can request another email.',array( ceil($options['resendBademailSecs']/60) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
            break;  //Otherwise it is fine, so fall through and send another below

        case 'pwprocessing':
            $ageInSeconds = $now - $match['updated'];
            if($ageInSeconds < $options['resendProcessingSecs'])throw new ErrorException('Recently sent a link in an email. Use that link again. Otherwise it is %1$d minutes before you can request a fresh email.',array( ceil($options['resendProcessingSecs']/60) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
            break;  //Otherwise it is fine, so fall through and send another below

        default:
            throw new SystemException("Bad status: implies coding error within addOrRefreshRequest() or handleForgotpwRequest():".print_r($match,true));
            break;
        }
    }

$this->insertRow($this->tableName, array(
    'status' => 'pwrequested',
    'email' => $email,
    'code' => $code,
    'created' => $now,
    'origin' => $options['origin'],
    'extra' => $options['extra'],
    ));
}



/**
* Helper for addOrRefreshRequest() for when doing an email address change.
*
* The basic idea is that if we find a *recent* existing entry then we complain.
* Otherwise we do nothing and simply add a new entry.
*
* @param Array $entries Basically one row for each previous time they've
*       requested to change their email address. So, it is quite typical for this to be empty.
*/
private function handleChangeEmailRequest($entries, $email, $code, $now, $options){
foreach($entries as $match){
    switch($match['status']){
        case 'emconfirmed':break;   //Don't care (even if very recent)

        case 'empending':
            $ageInSeconds = $now - $match['created'];
            if($ageInSeconds < $options['resendPendingSecs'])throw new ErrorException('Recently sent an email to this address. %1$d minutes before you can request another email.',array( ceil($options['resendPendingSecs']/60) ),"ageInSeconds={$ageInSeconds}, now=$now, match=".print_r($match,true));
            break;  //Otherwise it is fine, so fall through and send another below

        case 'embademail':
            $ageInSeconds = $now - $match['updated'];
            if($ageInSeconds < $options['resendBademailSecs'])throw new ErrorException('Recently sent an email to this address. %1$d minutes before you can request another email.',array( ceil($options['resendBademailSecs']/60) ),"ageInSeconds={$ageInSeconds},match=".print_r($match,true));
            break;  //Otherwise it is fine, so fall through and send another below

        default:
            throw new SystemException("Bad status: implies coding error within addOrRefreshRequest() or handleForgotpwRequest():".print_r($match,true));
        }
    }

$this->insertRow($this->tableName, array(
    'status' => 'empending',
    'email' => $email,
    'code' => $code,
    'created' => $now,
    'origin' => $options['origin'],
    'extra' => $options['extra'],
    ));
}


/**
* High-level function to take the given code, check it is in the DB, then update
* the corresponding row as 'processing' instead of 'pending'. This is called when
* they click the URL in the email they received.
*/
function processSignupCode($code, $now){
return $this->processCode($code, $now, 'pending', 'processing', 'processed');
}


/**
* High-level function to take the given code, check it is in the DB, then update
* the corresponding row as 'pwprocessing' instead of 'pwrequested'. This is called when
* they click the URL in the email they received.
*/
function processForgotpwCode($code, $now){
return $this->processCode($code, $now, 'pwrequested', 'pwprocessing', 'pwprocessed');
}


/**
* High-level function to take the given code, check it is in the DB, then update
* the corresponding row as 'emconfirmed' instead of 'empending'. This is called when
* they click the URL in the email they received.
*/
function processChangeEmailCode($code, $now){
return $this->processCode($code, $now, 'empending', 'emconfirmed', '');
}


/**
*
* @param String $code Raw user input, so is validated by this function.
* @param $now Current timestamp (used to record the change of status)
* @param $pendingStatus, $processingStatus, $processedStatus
* @return The signup table row that matches $code. (NB. status won't have been changed,
*               so will still say $pendingStatus, unless this is the 2nd time for the user to click the URL!)
*           If they are already processed, RedirectException is thrown.
*           If any other problem ErrorException is thrown.
*
* @internal Same error message for the two error-checks, for consistency (and to save revealing data).
*
* @internal Originally I returned SystemException for finding an unexpected status. After
*   thinking about possible causes, I've decided to an ErrorException is fine.
*
* @internal There is no $processedStatus for changing email address.
*/
private function processCode($code, $now, $pendingStatus, $processingStatus, $processedStatus){
$msg = 'Unknown or corrupted code. Check the URL or request a new one.';
if(!preg_match('/^[0-9a-z]{'.self::$codeMinLength.','.self::$codeMaxLength.'}$/',$code))throw new ErrorException($msg,array(),"code=$code");

$rows = $this->getAllForKey($this->tableName,'code',$code);
if(count($rows)!=1)throw new ErrorException($msg,array(),"code=$code, rows=".print_r($rows,true));    //NOTE: 2+ rows should be impossible, but if it it ever happens, this handles it.

$row = $rows[0];
if($row['status'] == $pendingStatus){   //99% of the time we end up here
    $this->changeStatus($row, $now, $pendingStatus, $processingStatus);
    return $row;
    }

if($row['status'] == $processingStatus)return $row; //Nothing to do. Implies they just pressed the link in the email a second time.

if($row['status'] == $processedStatus)throw new RedirectException();   //Already signed up. Go to top page of site, so they can login.

//Some other status. 'bademail', 'deleted' should be impossible. Other possibilities are the user
//  mistyped someone else's code. Or a /signup/ url got muddled with a /forgotpw/ code.
throw new ErrorException($msg,array(),"Found row for code=$code, but status is wrong. Here is full row:".print_r($row,true));
}


/**
* Finds the row for the given code and email combination, expecting status to be "processing"
*
* Any status except processing and processed will cause an error to be thrown; the calling
* function should detect "processed" and redirect the user to a login page.
*
* Typically after calling this, you'll then create the user DB entry (or change their password, or their
* email), then you should called changeStatus(), to set their status to processed (or pwprocessed/emprocessed).
*
* @return Array The row that is found. 
*/
public function getProcessingEntry($email,$code){
$msg = 'Unknown or corrupted code/email.';  //Consistent error message
if(!preg_match('/^[0-9a-z]{'.self::$codeMinLength.','.self::$codeMaxLength.'}$/',$code))throw new ErrorException($msg, array(), "code=$code");
$rows = $this->getAllForKey($this->tableName,'code',$code);
if(count($rows)!=1)throw new ErrorException($msg, array(), "code=$code, email=$email, rows=".print_r($rows,true));    //NOTE: 2+ rows should be impossible, but if it it ever happens, this handles it.
$row = $rows[0];
if($row['email']!=$email)throw new ErrorException($msg, array(), "Row found for code=$code but it did not match email=$email, row=".print_r($row,true));
return $row;
}


/**
* Finds all entries for a given email address
*/
public function getAllForEmail($email){
return $this->getAllForKey($this->tableName, 'email', $email);
}


/**
* Clear out all entries for the given email.
*
* NOTE: this is only intended for removing test users. In normal usage a processed
* user is left in forever.
*
* @todo How about another version that takes a status, and will only delete if they are of that status?
*   ---> Perhaps an array of statuses is most useful?
*/
public function deleteAllForEmail($email){
$q = "DELETE FROM {$this->tableName} WHERE email=?";
$statement=$this->get()->prepare($q);
$statement->execute(array( $email ));
}



}
