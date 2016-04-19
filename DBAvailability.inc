<?php
namespace QQAPI;

include_once "PDOWrapper.inc";
include_once "timezone.inc";
include_once "availability.inc";

/**
* This is for a database used to store availability objects.
*
* NOTE: We use a combination of "ownerId" and "whichId". But the combination is allowed to 
* have multiple occurences. (This is not to be used currently, but is there for future expansion,
* the idea being that the startTimestamp and endTimestamp of each would not be overlapping,
* for complex availability requirements. The separate occurences are referred to as segments, but
* generally all the public functions here take the full array of segments when doing any actions.)
*
* There is also an internal-use unique id for each row in the table (this is needed so we can do
* updates or deletes of certain records, even within a set).
*/
class DBAvailability extends PDOWrapperJson{
/**
* The table name.
*
* Change this in the constructor of your derived class, if you need something different.
*
* @internal This was added so that you can keep two different user tables in the same
*   DB, or just simply to work with an existing table.
*
* @todo URGENT I don't like the way timezone needs to be passed to so many functions here. Consider
*    giving it as a constructor param?
*       ---> Decide this quickly, as it breaks the interface.
*    The other idea is for it to be a row in the database table. Then when we call one of the update
*   functions, at least there is no need to ask timezone there.
*   i.e. will a single database table need to use multiple timezones.
        ---> I think so.
*/
protected $tableName = "availability";

//-----------------------
/** */
function __construct($dsn,$user=null,$pw=null,$options=array()){
$this->jsonFields = array('byWeekday','exceptionDays','summary','extra');
parent::__construct($dsn,$user,$pw,$options);
}


/**
* Creates the empty table (see class comments for  DB structure)
*
* @internal I use "DOUBLE" for the timestamps, for two reasons of portability (e.g. sqlite has
*  no date/time data type), and for precision.
*
* @todo I think we should add a index, that uses both which and ownerId. (But remember it cannot be a unique index.)
*
* @internal This table is in the library as it goes with some library functions we have for manipulating this
*   kind of scheduling data.
*/
public function createTable(){
if($this->checkForTable($this->tableName) !== null)return;
$q = "CREATE TABLE {$this->tableName}(";
$q.= $this->getCreateTableSQLForId();
$q.=<<<EOD
  ownerId INTEGER NOT NULL,
  whichId INTEGER NOT NULL,

  created DOUBLE NOT NULL,
  updated DOUBLE,

  byWeekday TEXT NOT NULL,
  exceptionDays TEXT NOT NULL,
  summary TEXT NOT NULL,

  startTimestamp DOUBLE,
  endTimestamp DOUBLE,
  futureAvailabilityInDays INT,
  lastDayConsidered VARCHAR(8),
  
  extra TEXT
  )
EOD;
$this->get()->exec($q);
}

/**
* @return Array The entry or entries that match ownerId and whichId.
*/
public function getOne($ownerId, $whichId){
$q = 'SELECT * FROM '.$this->tableName.' WHERE ownerId = ? AND whichId = ? ORDER BY startTimestamp';
return $this->fetchAll($q, array($ownerId, $whichId));
}

/**
* @return Array One entry per whichId, with key as the whichId. The inner array is an array of segments.
*   (if not using multi-segments you end up with one entry arrays, inside the outer array.)
*   Note: all fields are in the inner rows, include ownerid and whichId.
*
* @internal I had ORDER BY which in the query, but it is pointless, so removed.
*/
public function getAllForOwner($ownerId){
$q = 'SELECT * FROM '.$this->tableName.' WHERE ownerId = ? ORDER BY startTimestamp';
$d = array();
foreach($this->fetchAll($q, array($ownerId)) as $row){
    $w = $row['whichId'];
    if(array_key_exists($w, $d))$d[$w][] = $row;
    else $d[$w] = array($row);
    }
return $d;
}


/**
*/
public function deleteAllForOwner($ownerId){
$q = 'DELETE FROM '.$this->tableName.' WHERE ownerId = '.(int)$ownerId;
$this->get()->exec($q);
}



/**
* Add a new entry; this function does not support multiple-segment entries.
*
* NOTE: if it exists a SystemException is thrown.
*
* @return The row as added to the DB (but in a wrapper array, i.e. just as getOne() would return).
*/
public function addWithOneSegment($ownerId, $whichId, $input, $timezone, $extraValidation = null){
self::validateInputOneSegment($input);    //Throws if a problem. $input may be modified.
$obj = $GLOBALS['DBAvailability']->getOne($ownerId, $whichId);
if(count($obj)>=1)throw new SystemException("The whichId ($whichId) was found in the availabilityDB, but should not have been there. Found:".print_r($obj,true));

$tzOffset = getTzOffset($timezone);
$startDate = date("Y-m-d", time() + $tzOffset); //TODO: get from startTimestamp, if that was given.

$maxDays=$input['futureAvailabilityInDays']; //TODO: but also limit it to endTimestamp - startTimestamp
$lastDayConsidered=null; //Gets set
$summary = summarizeAvailabilityObject($input['byWeekday'], $input['exceptionDays'],
  $timezone,
  $startDate,
  $maxDays,
  $lastDayConsidered
  );


$now = microtime(true);
$d = array(
    'ownerId' => $ownerId,
    'whichId' => $whichId,
    'created'=>$now,
    'updated'=>$now,
    'byWeekday'=>$input['byWeekday'],
    'exceptionDays'=>$input['exceptionDays'],
    //Leave start/end as null
    'futureAvailabilityInDays'=>$input['futureAvailabilityInDays'],
    'summary'=>$summary,
    'lastDayConsidered' => $lastDayConsidered,
    //Leave extra as null
    );

if($extraValidation !== null)$extraValidation($d);    //Throws if a problem

return array($this->insertRowAndFetch($this->tableName, $d, "id"));
}


/**
* A high-level function to find the record in the DB, and replace the existing byWeekday/exceptionDays
* with the new values being given here.
*
* NOTE: it must already exist. If not a SystemException is thrown.
* NOTE: not suitable for use with multi-segment availability objects.
*
* It does validation of the inputs, so ErrorException can be thrown. It also does
* DB queries, where a SystemException can be thrown. It is also possible for evaluateAvailabilityObjectException 
* to be thrown (you may want to catch these and turn them into an ErrorException).
*
* @param Array $input This is expected to have these keys:
*       byWeekday, exceptionDays, futureAvailabilityDays, startTimestamp, endTimestamp
*   however they are mostly optional, and have defaults. The exception is that either futureAvailabilityDays
*   must be set, or startTimestamp/endTimestamp must be set.
* @param string $timezone Text description of the timezone.
*/
public function updateWithOneSegment($ownerId, $whichId, $input, $timezone, $extraValidation = null){
self::validateInputOneSegment($input);    //Throws if a problem. $input may be modified.

$obj = $GLOBALS['DBAvailability']->getOne($ownerId, $whichId);
if(!$obj)throw new SystemException("The ownerId ($ownerId) and whichId ($whichId) combination was not found.");
if(count($obj)!=1)throw new SystemException("The whichId ($whichId) was found in the availabilityDB, but should have exactly one row, but found:".print_r($obj)); //Only supporting one row per availability object currently.
$obj = $obj[0]; //For code readability, just use the first element.

$changes = array();
foreach( array('byWeekday', 'exceptionDays', 'futureAvailabilityInDays', 'startTimestamp', 'endTimestamp') as $field){
    if($obj[$field] != $input[$field])$changes[$field] = $obj[$field] = $input[$field];
    }
if(count($changes)==0)return array($obj);

$tzOffset = getTzOffset($timezone);
$startDate = date("Y-m-d", time() + $tzOffset); //TODO: get from startTimestamp, if that was given.
$maxDays=$obj['futureAvailabilityInDays']; //TODO: but also limit it to endTimestamp - startTimestamp

$lastDayConsidered=null; //Gets set
$summary = summarizeAvailabilityObject($obj['byWeekday'], $obj['exceptionDays'],
  $timezone,
  $startDate,
  $maxDays,
  $lastDayConsidered
  );
$obj['summary'] = $changes['summary'] = $summary;
$obj['lastDayConsidered'] = $changes['lastDayConsidered'] = $lastDayConsidered;

if($extraValidation !== null)$extraValidation($obj);    //Throws if a problem

$this->updateRecord($this->tableName, $changes, 'id', $obj['id']);
return array($obj);
}


/**
* This finds all objects for a given ownerId, then updates the summary field.
*
* @param Number $ownerId Set to 0 to process all owner IDs.
* @param Number $minAge If zero (the default) then this function will process all records.
*       If you set it to 1800 then records that have been updated in the last 30 minutes will
*       not be touched. This can be useful if this function is called twice in rapid succession,
*       by mistake. If maxDays is, say, 30, then it could actually be much higher than 30m. E.g. 12 hours.
*       This would then mean we could call this as an hourly cronjob quite nicely.
* @return The number of records updated
*
* @internal We do the prepared statement stuff ourselves, so it can be re-used for each record,
*   which might turn out here to be a significant saving.
*/
public function updateAllSegments($ownerId, $minAge = 0){
if($ownerId == 0){
    if($minAge > 0){
        $q = "SELECT * FROM {$this->tableName} WHERE updated <= ?";
        $statement=$this->get()->prepare($q);
        $statement->execute(array(time() - $minAge));
        }
    else{
        $q = "SELECT * FROM {$this->tableName}";
        $statement=$this->get()->prepare($q);
        $statement->execute();
        }
    }
else{
    if($minAge > 0){
        $q = "SELECT * FROM {$this->tableName} WHERE ownerId = ? AND updated >= ?";
        $statement=$this->get()->prepare($q);
        $statement->execute(array($ownerId, time() - $minAge));
        }
    else{
        $q = "SELECT * FROM {$this->tableName} WHERE ownerId = ?";
        $statement=$this->get()->prepare($q);
        $statement->execute(array($ownerId));
        }
    }
$d = $statement->fetchAll(\PDO::FETCH_ASSOC);

$updater = $this->get()->prepare("UPDATE {$this->tableName} SET summary=?, lastDayConsidered=?, updated = ? WHERE ownerId=? AND whichId = ?");

$cnt = 0;

foreach($d as $obj){
    $timezone = "Asia/Tokyo";   //VERY TEMP. (See @todo at the top of the class)
    $tzOffset = getTzOffset($timezone);
    $startDate = date("Y-m-d", time() + $tzOffset); //TODO: get from startTimestamp, if that was given.
    $now = microtime(true);

    $lastDayConsidered=null; //Gets set by next function call.

    $summary = summarizeAvailabilityObject(
        json_decode($obj['byWeekday'],true),
        json_decode($obj['exceptionDays'],true),
        $timezone,
        $startDate,
        $obj['futureAvailabilityInDays'],   //maxDays
        $lastDayConsidered
        );
    $summaryAsJson = json_encode($summary, JSON_UNESCAPED_UNICODE);
    if($obj['summary'] == $summaryAsJson && $obj['lastDayConsidered'] == $lastDayConsidered){
        continue;
        }

    $updater->execute( array(
        $summaryAsJson,
        $lastDayConsidered,
        $now,
        $obj['ownerId'],
        $obj['whichId']
        ) );

    $cnt++;
    }

return $cnt;
}


/**
* This deletes one specific availability object (if multi-segment then all segments
* will be deleted).
*
* On error an exception is thrown. If no exception then it worked.
*/
public function deleteOne($ownerId, $whichId){
$q = "DELETE FROM {$this->tableName} WHERE ownerId = ? AND whichId = ?";
$statement=$this->get()->prepare($q);
$statement->execute(array( $ownerId, $whichId ));
}

//------------------------

/**
* Validate the inputs (as given to updateOne(), etc.)
*
* @param Array $input Associative array, with various keys.
*
* @todo The lack of checking that either $futureAvailabilityInDays or $endTimestamp
*       is set, can cause a lot of confusing problems.
*/
private static function validateInputOneSegment(&$input){
if(!array_key_exists('byWeekday',$input))$input['byWeekday']=array(
    'sun'=>'', 'mon'=>'', 'tue'=>'', 'wed'=>'',
    'thu'=>'', 'fri'=>'', 'sat'=>'',
    );
if(!array_key_exists('exceptionDays',$input))$input['exceptionDays']=array();

if(!array_key_exists('futureAvailabilityInDays',$input))$input['futureAvailabilityInDays']=0;
else{
    if(!preg_match('/^\d{1,3}$/',$input['futureAvailabilityInDays']))throw new ErrorException("Badly formatted futureAvailabilityInDays.",array(),"input=".print_r($input,true));   //We use a regex, rather than
        //an (int) cast, to differentiate between a deliberate "0" and a bad input.
    $futureAvailabilityInDays = (int)$input['futureAvailabilityInDays'];
    if($futureAvailabilityInDays>180)$futureAvailabilityInDays=180; //TODO: 1. Should this instead throw
        //an error? 2. Make 180 a parameter or class static?
    $input['futureAvailabilityInDays'] = $futureAvailabilityInDays;
    }

if(!array_key_exists('startTimestamp',$input))$input['startTimestamp'] = null;
if(!array_key_exists('endTimestamp',$input))$input['endTimestamp'] = null;
//TODO: check startTimestamp and endTimestamp too. (I.e. here we just get them, but we also
//   have to turn them into timestamps. startTimestamp must be in the future, and endTimestamp
//   must be greater than endTimestamp.
//TODO: if $futureAvailabilityInDays==0, then endTimestamp must be set
}



}
