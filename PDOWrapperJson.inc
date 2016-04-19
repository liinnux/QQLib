<?php
namespace QQAPI;

include_once "PDOWrapper.inc";


/**
* This adds a few functions to PDOWrapper, as well as some member data. A class should
* derive from this instead if it wants to store json-encoded data in some fields.
* 
* Note: when the derived class has multiple tables, $jsonFields applies to all of them.
* E.g. if there is a field called "details" in both "tableThis" and "tableThat", and if
* you list "details" in $jsonFields, then it must be a JSON field in both tables. It cannot
* be JSON in one table, and not in the other.
*
* @todo I think we should make wrapper versions of the base class functions
    --> So the derived class knows they are using it, I think the Json suffix should always be used.
* @internal I considered suffixing all function names with "Json" so a derived class knows the
*   conversion is happening. But it made the code look uglier, and could also be confusing, e.g.
*   does "fetchAllJson()" mean it just fetches the Json fields, or all fields?
*/
class PDOWrapperJson extends PDOWrapper{
/** An array of the field names */
protected $jsonFields;

//--------------------------
/** */
function __construct($dsn,$user=null,$pw=null,$options=array()){
parent::__construct($dsn,$user,$pw,$options);
}


/**
* @param string $q The SQL query (actually a prepared statement)
* @param Mixed $values The values for the prepared statement. Normally an array. If your
*   prepared statement only contains one "?" then it can be just the string or number, 
*   and this function will wrap it into an array for you.
*   If you have no question marks in $q, you can pass null here.
* @return array The rows, with json un-encoding already done
*/
protected function fetchAll($q, $values){
$dbh = $this->get();
if($values !==null && !is_array($values))$values=array($values);
$statement=$dbh->prepare($q);
$statement->execute($values);
$rows = $statement->fetchAll(\PDO::FETCH_ASSOC);
foreach($rows as &$d)$this->unencodeJson($d);
return $rows;
}

/**
* Like fetchAll(), but for when you expect just one row (or only need the first
* row of a however many rows there are).
*
* @param string $q The SQL query (actually a prepared statement)
* @param Mixed $values The values for the prepared statement. Normally an array. If your
*   prepared statement only contains one "?" then it can be just the string or number, 
*   and this function will wrap it into an array for you.
* @return array The row, with json un-encoding already done
*/
protected function fetchRow($q, $values){
$dbh = $this->get();
if(!is_array($values))$values=array($values);
$statement=$dbh->prepare($q);
$statement->execute($values);
$row = $statement->fetch(\PDO::FETCH_ASSOC);
$this->unencodeJson($row);
return $row;
}

/**
* @overrides base class (PDOWrapper) version to add the encodeJson()
*/
public function insertRow($tablename,$d){
$this->encodeJson($d);
parent::insertRow($tablename,$d);
}

/**
* @overrides base class (PDOWrapper) version to add the encodeJson() and unencodeJson()
*/
public function insertRowAndFetch($tablename,$d, $idField){
$this->encodeJson($d);
$row = parent::insertRowAndFetch($tablename,$d, $idField);
$this->unencodeJson($row);
return $row;
}


/**
* @overrides base class (PDOWrapper) version to add the encodeJson()
*/
public function replaceRow($tablename,$d){
$this->encodeJson($d);
parent::replaceRow($tablename,$d);
}

/**
* @overrides base class (PDOWrapper) version to add the encodeJson() and unencodeJson()
*/
public function replaceRowAndFetch($tablename,$d, $idField){
$this->encodeJson($d);
$row = parent::replaceRowAndFetch($tablename,$d, $idField);
$this->unencodeJson($row);
return $row;
}


/**
* @overrides base class (PDOWrapper) version to add the unencodeJson()
*/
public function updateRecord($tablename, $d, $keyField, $keyValue, $now = false){
assert(!in_array($keyField, $this->jsonFields));
$this->encodeJson($d);
parent::updateRecord($tablename, $d, $keyField, $keyValue, $now);
}

/**
* @overrides base class (PDOWrapper) version to add the unencodeJson()
*/
public function updateRecordAndFetch($tablename, $d, $keyField, $keyValue, $now = false){
assert(!in_array($keyField, $this->jsonFields));
$this->encodeJson($d);
$row = parent::updateRecordAndFetch($tablename, $d, $keyField, $keyValue, $now);
$this->unencodeJson($row);
return $row;
}

//--------------------------
/**
*/
protected function encodeJson(&$d){
if(!is_array($d))return;
foreach($this->jsonFields as $k){
    if(array_key_exists($k,$d)){
        if($d[$k] === null)continue;    //Leave null as null.
        $d[$k] = json_encode($d[$k],JSON_UNESCAPED_UNICODE);
        }
    }
}

/** */
protected function unencodeJson(&$d){
if(!is_array($d))return;
foreach($this->jsonFields as $k){
    if(array_key_exists($k,$d))$d[$k] = json_decode($d[$k],true);   //2nd param of true, so we get assoc. arrays
        //and not PHP objects.
    }
}



}   //End of class PDOWrapper


