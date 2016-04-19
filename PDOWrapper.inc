<?php
namespace QQAPI;


/**
* This holds an instance of a DB connection, along with the dsn, username
* and password needed for them.
*
* This class is doing two things. The first is on-demand connection.
* The second is a place to store the DSN, username and password; this is needed
* as PDO's DSN does not allow storing username/password, forcing us to pass
* three things around instead of just one.
*
* NOTE: it always uses exceptions for error-reporting.
*
* NOTE: it is expected that classes will derive from this, one per DB, and extend
* this class with functions specific to that DB. It is not an abstract class, and there
* are no functions that a base class should override.
*
* A few very common helper functions can also go in this base class. They may
* not support all DB back-ends that PDO supports: look at the source or comments
* for each.
* Generally the public helper functions should not take $dbh as a parameter, and
* instead use get() to get it for themselves. This might be slightly inefficient, but
* it also means any function can be used at any time, and consistently.
*
* @internal Even derived classes will use get() to get the DB handle, which
*   is why all variables are private.
*/
class PDOWrapper{
/**
* The PDO DB connection. If you want to share a connection, set this in
* the constructor (or just after - the key point is to make sure it is set
* before the first need to read from the DB).
*/
private $dbh;

/**
 * The database type (i.e. PDO::ATTR_DRIVER_NAME)
 * 
 * Cached here the first time we need it.
 * 
 * @internal If $other is set, we initialize it from that.
 */
private $dbType;

/**
* If this object (usually a derived class, which is adding specialized usage functions)
* is set then we call the get() function on it instead.
* This gets set by passing another PDOWrapper class as the only parameter to
* the constructor. (In fact it does not have to be PDOWrapper - any object that
* has a get() function is fine.)
* 
* Note: if this is set, it also means the disconnect() function is ignored.
*/
private $other;

/**
* The DB connection string; used if $conn is null at the first time
* we need to read from the DB.
*/
private $dsn;

/**
* Username for the DB connection
*/
private $username;

/**
* Username for the DB connection
*/
private $password;

/** Assoc array of driver-specific connection options */
private $options;

//-------------------------------------------------
/**
* Give the PDO DSN, as well as optionally username, password and options.
*
* NOTE: if $user and $pw are null, then $dsn string can contain them: username:password@dsn
*       If either username or password not needed, still include the ":" and "@".
*       E.g. If username needed, but not password do:   "myuser:@mysql:dbname=test"
*       If no colon and ampersand found at the start of the string, the whole string
*       will be treated as a dsn (with username/password set to null).
*
* (Rare-use case Aside: I'm not aware of any DSN's that use ampersand. But if you had one, and
*       also did not want to specify username/password, give the
*       blank username and password explicitly, so it looks like:
*           :@mysql:dbname=test;something=a@b.com
*       The alternative way is to use the full constructor, and give $user as "", instead of null; then
*       $dsn will not be parsed for special meanings!)
*
* @param Mixed $dsn If a string then either as described in the PDO docs, or "username:password@DSN", where DSN
*            is again as described in the PDO docs.
*            If an object, then it is assumed to be another PDOWrapper instance that we should delegate all
*            calls to get() to. If setting this, then it should be the only parameter specified.
* @param String $user The username to log into the DB with. Optional.
* @param String $pw The password to log into the DB with. Optional.
* @param Array $options Associative array to pass to PDO constructor. See PDO docs.
*
* @todo No options support yet in the string-only version of the constructor.
*    For the moment you can do:
*          new PDOWrapper("me:pw@mysql:dbname=test", null, null, 
*               array( PDO::MYSQL_ATTR_INIT_COMMAND:"SET NAMES 'UTF8'" ) );
*    I think what we could also do, in future, is append a JSON object to the end, after a semi-colon.
*    (The point of using JSON is so there is escaping, to fallback on, for exotic needs.)
*    E.g. it might look like this:
*        me:pw@mysql:dbname=test;{"PDO::MYSQL_ATTR_INIT_COMMAND":"SET NAMES 'UTF8'"}
*    I.e. by looking for the first occurence of ";{" we should be able to detect the existence
*    and start of it, without ever clashing with a valid PDO DSN.
*   ---> Actually that particular example fails. PDO::MYSQL_ATTR_INIT_COMMAND is referring
*       to a compile-time constant, and therefore is not a string. So the actual example would have
*       to be written as:
*          me:pw@mysql:dbname=test;{1002:"SET NAMES 'UTF8'"}
*       and therefore be a bit brittle.
*      (The integer constants can be found out by going to php -a, and:
*       echo PDO::MYSQL_ATTR_INIT_COMMAND;  )
*/
function __construct($dsn,$user=null,$pw=null,$options=array()){
if(is_object($dsn)){
    assert($user===null && $pw===null && $options===array());   //Shouldn't be giving other params.
    $this->other = $dsn;
    return;
    }
if($user!==null || $pw!==null){
    $this->dsn = $dsn;
    $this->username = $user;
    $this->password = $pw;
    }
elseif(preg_match('/^([^:@]*):([^@]*)@(.+)$/',$dsn,$parts)){
    $this->dsn = $parts[3];
    $this->username = $parts[1];
    $this->password = $parts[2];
    }
else{   //I.e. username/password are null, but also no "@" in the DSN.
    $this->dsn = $dsn;
    $this->username = null;
    $this->password = null;
    }
$this->options = $options;
}


/**
* Simple function to help debugging
*/
public function getConnectionAsString(){
if($this->other)return $this->other->getConnectionAsString();
$attributes = array(
    "AUTOCOMMIT", "ERRMODE", "CASE", "CLIENT_VERSION", "CONNECTION_STATUS",
    "ORACLE_NULLS", "PERSISTENT", "PREFETCH", "SERVER_INFO", "SERVER_VERSION",
    "TIMEOUT"
    );
$s='';
$s.=$this->dsn." (username=".$this->username.");";
$dbh = $this->get();
foreach ($attributes as $val) {
    try{
        $v=$dbh->getAttribute(constant("PDO::ATTR_$val"));
        }catch(\PDOException $e){continue;}
    $s.="PDO::ATTR_$val: $v; ";
    }
return $s;
}


/**
* Get the DB handle, doing a connection if necessary.
*/
public function get(){
if($this->other)return $this->other->get(); //Sharing a connection
if($this->dbh)return $this->dbh;

$this->dbh = new \PDO($this->dsn,$this->username,$this->password,$this->options);
$this->dbh->setAttribute(\PDO::ATTR_ERRMODE,\PDO::ERRMODE_EXCEPTION);
//$dbh->exec("SET CHARACTER SET utf8"); //TODO: do we need this?
    //---> No, I think it can be done in the DSN now. See http://www.php.net/manual/en/pdo.construct.php#113498 and http://www.php.net/manual/en/pdo.construct.php#96325
return $this->dbh;
}


/**
 * Get (then cache) database type
 * 
 * Note: if get() has not been called before, this will end up doing the connection.
 */
public function getType(){
if($this->dbType)return $this->dbType;
if($this->other)$this->dbType = $this->other->getType();
else $this->dbType = $this->get()->getAttribute(\PDO::ATTR_DRIVER_NAME);
return $this->dbType;
}


/**
* Explicitly close the connection
*/
public function disconnect(){
if($this->other)return; //Too complicated to allow sharing instances to close the connection.
if(!$this->dbh)return;
$this->dbh = null;
}

//-------------------------------------------------

/**
* Just get any one row (as that both tells us which fields, while also being an existence check).
*
* DB SUPPORT: MySQL and SQLite only. (Other DBs will return a row if the table exists, but
* will throw some PDOException if not, rather than return null.)
*
* @return Mixed null if table does not exist. False if table exists but has no rows.
*       Otherwise an associative array of an arbitrary row in that table (so you can get column
*       names).
*
* @internal Originally called doesTableExist(). But that sounds more like a yes/no
*   question, not one that gives a mixed return. checkForTable() is deliberately more vague.
*
* @internal If you get SQLSTATE[42000]: Syntax error or access violation:
*           1103 Incorrect table name '...' then
*    it appears to be because period is not supported in table names until MySQL 5.1.6.
*/
public function checkForTable($tn){
assert(gettype($tn)=="string");

$dbh = $this->get();
$q="SELECT * FROM ".$this->quoteIdentifier($tn)." LIMIT 1";

try{
    $st=$dbh->query($q);    //throws on error, returns PDOStatement
    }catch(\PDOException $e){
    if($this->getType()=='mysql' && $e->getCode()=='42S02')return null;   //I.e. "SQLSTATE[42S02]: Base table or view not found: 1146 Table 'api.FX.USDJPY.1m.main.changes' doesn't exist"
    if($this->getType()=='sqlite' && $e->getCode()=='HY000')return null;   //I.e. "SQLSTATE[HY000]: General error: 1 no such table: FX.USDCAD.1m.main"
    throw($e);  //Else re-throw
    }
return $st->fetch(\PDO::FETCH_ASSOC);
}


/**
* Adds an index for a given field, where only the first $length characters
* of the index need to be used.
*
* $length is a hint, and for databases not supporting this, it is ignored,
* and the whole field is used.
* NOTE: for MySQL you cannot index a TEXT column, without length, so length is required.
* But for Sqlite you cannot specify the length, but you can index a TEXT column.
*
* @todo Use regexes to check $tn and $field for sanity!
*/
public function addNonUniquePartialIndex($tn,$field,$length){
$dbh = $this->get();
$indexName = "index_".$tn."_".$field;
if($this->getType() == 'sqlite'){
    $q = "CREATE INDEX {$indexName} ON {$tn}({$field})"; 
    }
else{
    $length=(int)$length;
    $q = "CREATE INDEX {$indexName} ON {$tn}({$field}({$length}))"; 
    }
$dbh->exec($q);
}


/**
* Like addNonUniquePartialIndex, but the index will be unique.
*/
public function addUniquePartialIndex($tn,$field,$length){
$dbh = $this->get();
$indexName = "index_".$tn."_".$field;
if($this->getType()=='sqlite'){
    $q = "CREATE UNIQUE INDEX {$indexName} ON {$tn}({$field})"; 
    }
else{
    $length=(int)$length;
    $q = "CREATE UNIQUE INDEX {$indexName} ON {$tn}({$field}({$length}))"; 
    }
$dbh->exec($q);
}


/**
* Clears out all tables, but leaves the database behind
*
* DB SUPPORT: MySQL only (and relies on commandline tools)
*
* @todo Is it okay to throw "Exception"? One of the QQAPI exceptions is better?
*
* @todo Here is an alternative approach not using commandline tools:
$res = $dbh->query("SELECT TABLE_NAME FROM information_schema.tables WHERE table_schema = 'flexdesk1'");
foreach($res->fetchAll(\PDO::FETCH_NUM) as $row){
    $tn = $row[0];
    $q = "DELETE FROM ".$tn;
    echo "q=$q\n";  //TEMP
    $dbh->exec($q); 
    }
$dbh = null;$GLOBALS['DBDefault']->disconnect();

--> It seems to work, but auto-increment ids do not get reset. (Whereas the below approach does do that resetting)
*/
public function removeAllTables(){
$remove_all_tables=<<<EOD
mysql -u #USER --password=#PW #DB -e "show tables" | grep -v Tables_in | grep -v "+" | gawk '{print "drop table `" $1 "`;"}' | mysql -u #USER --password=#PW #DB
EOD;

$remove_all_tables=str_replace('#PW',$this->password,$remove_all_tables);
$remove_all_tables=str_replace('#USER',$this->username,$remove_all_tables);

if(!preg_match('/^mysql:.*?;dbname=([^;]+)/',$this->dsn,$parts))throw new Exception("DB not supported by removeAllTables() (or the dbname part is missing in the DSN)");
$dbname = $parts[1];
$remove_all_tables=str_replace('#DB',$dbname,$remove_all_tables);

shell_exec($remove_all_tables);
}


/**
 * This gets a list of all tables in the database
 * 
 * It is useful for validating your DB has the latest schema.
 * 
 * @todo This could also be used by removeAllTables() ?
 * 
 * @see http://www.sqlite.org/faq.html#q7 for SQLite
 * @see https://dev.mysql.com/doc/refman/5.0/en/show-tables.html for MySQL
 */
public function getAllTables(){
switch($this->getType()){
    case 'sqlite':
        $q = "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name";
        break;
    case 'mysql':
        $q = "SHOW TABLES";
        break;
    default:
        throw new SystemException('getAllTables() not supported for this DB ('.$this->getType().') yet.');
    }
$dbh = $this->get();
$statement = $dbh->prepare($q);
$statement->execute();
return $statement->fetchAll(\PDO::FETCH_COLUMN, 0);
}

/**
 * Gets schema for one table
 *
 * @todo The sqlite code is completely untested.
 *
 * @see https://www.sqlite.org/pragma.html#pragma_table_info
 *   and http://stackoverflow.com/a/11996548/841830 for sqlite
 * @see https://dev.mysql.com/doc/refman/5.0/en/show-create-table.html for mysql
 */
public function getTableSchema($tablename){
switch($this->getType()){
    case 'sqlite':
        $q = "pragma table_info({$tablename})";
        break;
    case 'mysql':
        $q = "SHOW CREATE TABLE ".$tablename;
        break;
    default:
        throw new SystemException('getTableSchema() not supported for this DB ('.$this->getType().') yet.');
    }
$dbh = $this->get();
$statement = $dbh->prepare($q);
$statement->execute();
return $statement->fetchAll(\PDO::FETCH_COLUMN, 1);
}


/**
 * Escape field names (etc) in SQL queries
 * 
 * Note: they are escaped whether they need to be or not (as we cannot be sure what is a reserved
 * keyword for any particular DB).
 *
 * @param string $s The identifier to escape
 * @param string $dbType The SQL DB being used. The default of null means it will do the lookup.
 *     (This is just a parameter so you can do the lookup once before a loop, i.e. for efficiency.)
 *
 * @see http://stackoverflow.com/a/214344/841830
 * @todo Not done Microsoft SQL server, yet, as it seems to use multiple drivers, including ODBC, so
 *    hard to be sure when using it.
 */
public function quoteIdentifier($s){
switch($this->getType()){
    case 'mysql':
        return '`'.$s.'`';
    default:    //SQL92 standard, and most DBs, use double quotes
        return '"'.$s.'"';
    }
}


/**
 * @param Array $d A set of field names to escape.
 * @return Array Same size as $d
 */
public function quoteIdentifierArray($d){
return array_map(function($s){
    return $this->quoteIdentifier($s);
    }, $d);
}


/**
* Does a select of all rows, all columns, for the given value for a column.
*
* @param String $tn The table to query
* @param String $key The column to query against
* @param Mixed $value If a string, then exact match.
*                If an array then all rows that match any of those values are returned (i.e.
*               an SQL IN clause is used).
*              If an object of type Closure, then it is called and its return is assigned
*              to $value (i.e. it is expected to return a string or array, and if it wants
*              to cancel the request it should return a blank array; or it could of course
*              throw an exception).
*               The closure call is given $tn and $key as parameters, just in case it needs them.
*              NOTE: if any other type is passed, it is cast to a string.
* @param String $select Defaults to '*', to return all columns. This can be used to just return
*               some of the columns, or to do casting on them (or even sub-queries).
*               If just specifying one column, it might be more efficient to use getColumnForKey() instead.
*               @todo getColumnForKey() not written yet!
* @return Array Each element is an associative array.
*
* @internal We escape $tn and $key. Prepared statements used so it is fine
*    for $value to contain end-user input.)
*
* @internal Closures are the only allowed type of callbacks, and we don't use is_callable. This
*       is because it becomes impossible to tell the difference between a simple string value we
*       want to find in the DB, and a string value that is the name of a function. E.g. this query
*       wouldn't be created from setting $value='date': SELECT * FROM tn WHERE key = 'date'
*/
public function getAllForKey($tn,$key,$value,$select='*'){
$dbh = $this->get();
$q = 'SELECT '.$select.' FROM '.$this->quoteIdentifier($tn).' WHERE '.$this->quoteIdentifier($key);

//if($value instanceof Closure){    //This refuses to work (with php 5.3.2; could be a bug)
if(is_object($value) && get_class($value)=="Closure"){
    //throw new Exception(print_r($value,true)); //TEMP
    $value = $value($tn,$key);
    }

if(is_array($value)){
    $cnt = count($value);
    if($cnt < 1)return array();  //Obviously cannot get any matches, so stop now
    $q.=' IN (?' . str_repeat(',?',$cnt-1) . ')';
    $statement = $dbh->prepare($q);
    $statement->execute($value);
    }
else{
    $value = (string)$value;    //Might throw if an object that cannot be converted
    $statement=$dbh->prepare($q.'=?');
    $statement->execute(array($value));  //Throws on error (or does it not actually
        //run until the below fetchAll() call??)
    }
return $statement->fetchAll(\PDO::FETCH_ASSOC);
}


/**
 * Like getAllForKey(), for when you have 2+ fields you want to query on
 *
 * @param $KV Array The key is the field name to query on. The value is like $value
 *   in getAllForKey() (i.e. it can be string, array or a closure).
 *  By default the multiple entries in $KV are combined with AND.
 *     E.g. array('id'=>2, state=>'active')  will only match records where both ID is 2
 *    and the state is "active".
 *    E.g. array('id'=>2, state=> ['A','B'] )  will match records where ID is 2 and
 *    state is A or B.
 *    E.g. array('id'=> [2,3], state=> ['A','B'] )  will match records where ID is 2 OR 3 AND
 *    state is A or B.
 * @see deleteAllForMultipleKeys()
 */
public function getAllForMultipleKeys($tn, $KV, $select='*', $operator="AND"){
$dbh = $this->get();

$q = "";    //We built up just the WHERE clause, then prefix the rest of the query
$params = array();

foreach($KV as $key => $value){
    //if($value instanceof Closure){    //This refuses to work (with php 5.3.2; could be a bug)
    if(is_object($value) && get_class($value)=="Closure"){
        //throw new Exception(print_r($value,true)); //TEMP
        $value = $value($tn,$key);
        }

    $key = $this->quoteIdentifier($key);
    if($q == "")$q = $key;
    else $q.= " ".$operator." ".$key;

    if(is_array($value)){
        $cnt = count($value);
        if($cnt < 1)continue;   //No matches, skip it
        $q.=' IN (?' . str_repeat(',?',$cnt-1) . ')';
        $params = array_merge($params, $value); //Append the new entries
        }
    else{
        $q.='=?';
        $params[] = (string)$value;    //Might throw if an object that cannot be converted
        }
    }

$q = 'SELECT '.$select.' FROM '.$this->quoteIdentifier($tn).' WHERE '.$q;
$statement = $dbh->prepare($q);
$statement->execute($params);


return $statement->fetchAll(\PDO::FETCH_ASSOC);
}


/**
* Handles doing the prepared query, escaping, etc.
*
* @param String $tablename The table to insert into
* @param Array Keys are field names, values are the values to insert.
*
* @todo I wonder if we should throw an exception for bad parameters, not quietly ignore?
*/
public function insertRow($tablename,$d){
$this->insertRowHelper($tablename,$d);
}


/**
 * Like insertRow(), but it then returns the created row. Use this if you want to know
 * the id field value, and any other automatically set values.
 * 
 * If $idField is not an auto-increment field, then it expects to find it in $d, and that
 * will be used for the select instead. But auto-increment fields are the raison d'etre of
 * the xxxAndFetch functions.
 *
 * @param String $idField The name of the unique ID field.
 *    
 * @return Array Empty array if bad $d input (exception thrown on any other error)
 *       Note: it only ever returns a single row. If $idField is not a UNIQUE column then
 *       the behaviour is undefined, and you should not be using this function.
 *    Note: because we insert, the select should always work, and therefore 
 * 
 * @todo For postgres we need to use different insert query!
 *     See http://stackoverflow.com/a/2944335/841830
 *    and http://stackoverflow.com/a/6169915/841830
 *   I.e. add "RETURNING id", and do it with fetch() instead of execute()
 *   The alternative would be to pass in the id sequence name as $idField.
*/
public function insertRowAndFetch($tablename,$d, $idField){
$this->insertRowHelper($tablename,$d);

$db = $this->get();
$q = 'SELECT * FROM '.$this->quoteIdentifier($tablename).' WHERE '.$this->quoteIdentifier($idField).'=?';
$statement = $db->prepare($q);
if(!array_key_exists($idField, $d))$id = $db->lastInsertId($idField);  //TODO: Postgres needs a different approach - see comment above
else $id = $d[$idField];
$statement->execute(array($id));
$ret = $statement->fetch(\PDO::FETCH_ASSOC);
if(!is_array($ret))throw new SystemException("Failed to find the record we should have just inserted. idField={$idField}, id={$id}.");
return $ret;
}


/**
* Like insertRow(), but it does an update if already there
* 
* Note: might be implemented as a delete then insert, so be very careful if you have any
* delete triggers.
*
* @param String $tablename The table to insert into
* @param Array Keys are field names, values are the values to insert.
* 
* @todo URGENT PDOWrapperJson version
*/
public function replaceRow($tablename,$d){
$this->insertRowHelper($tablename,$d,"REPLACE INTO");
}


/**
* Like insertRowAndFetch(), but it does an update if already there
* 
* Note: might be implemented as a delete then insert, so be very careful if you have any
* delete triggers.
*
* @param String $idField The name of the unique ID (auto-increment) field.
* @return Array Empty array if bad $d input (exception thrown on any other error)
*       Note: it only ever returns a single row. If $idField is not a UNIQUE column then
*       the behaviour is undefined, and you should not be using this function.
* 
* @todo URGENT PDOWrapperJson version
*/
public function replaceRowAndFetch($tablename,$d, $idField){
$this->insertRowHelper($tablename,$d,"REPLACE INTO");
$db = $this->get();
$id = $db->lastInsertId($idField);  //TODO: Postgres needs a different approach
$q = 'SELECT * FROM '.$this->quoteIdentifier($tablename).' WHERE '.$this->quoteIdentifier($idField).'=?';
$statement = $db->prepare($q);
$statement->execute(array($id));
return $statement->fetch(\PDO::FETCH_ASSOC);
}


/**
 * Common code for insertRow(), insertRowAndFetch() and the update versions.
 */
private function insertRowHelper($tablename, $d, $action="INSERT INTO"){
if(!$d || !is_array($d) || count($d)<1)return;  //Quietly ignore
$fields = $this->quoteIdentifierArray(array_keys($d));
$qmarks = '?'.str_repeat(',?',count($fields)-1);
$q = $action.' '.$tablename.'('.implode(',',$fields).') VALUES('.$qmarks.')';
$db = $this->get();
$st=$db->prepare($q);
$st->execute( array_values($d) );   //Use of array_values() is important?!! If just passing in $d then
    //it fails with "SQLSTATE[HY093]: Invalid parameter number: parameter was not defined" !!
}



/**
* Update just one field in (normally) just one row; the updated field also gets set to the current time.
*
* NOTE: If the $keyField is a unique column, then only 0 or 1 rows can be updated. Otherwise
* there is no guarantee how many rows will be updated.
*
* @todo Do we want any return value  (there will be an exception thrown if serious problems)
* @todo I wonder if we need some way to control the microtime(true) value that is used. (I think
*       not setting updated at all belongs in another function? E.g. called setOneField())
*   --> Or we could take the name of the updated field in a final parameter. Then that could be
*       null if not being used.
*/
public function updateOneField($tablename, $field, $value, $keyField, $keyValue){
$q='UPDATE '.$tablename.' SET '.$this->quoteIdentifier($field).' = ?, updated = ? WHERE '.$this->quoteIdentifier($keyField).' = ?';
$db = $this->get();
$st=$db->prepare($q);
$st->execute( array($value, microtime(true), $keyValue) );
}


/**
* Like updateOneField(), but to set multiple fields in one go
*
* NOTE: if $d is the full row it will still work, but ideally you just set the fields that
* have actually changed. Also this function will set updated, so no need to include
* that in $d.
*/
public function updateRecord($tablename, $d, $keyField, $keyValue, $now = false){
$this->updateRecordHelper($tablename, $d, $keyField, $keyValue, $now);
}


/**
* So updateRecord() and updateRecordAndFetch() can share common code.
*
* @internal We originally had updateRecordAndFetch() call $this->updateRecord(...) but
*   when we have a derived class, it called the derived class version (which meant values
*   were getting json-encoded twice)!
*/
private function updateRecordHelper($tablename, $d, $keyField, $keyValue, $now){
if($now === false)$now = microtime(true);

$q='UPDATE '.$this->quoteIdentifier($tablename).' SET ';
$values = array();
foreach($d as $k=>$v){
    $q.=$this->quoteIdentifier($k).' = ?,';
    $values[]=$v;
    }
$q.=' updated = ? WHERE '.$this->quoteIdentifier($keyField).' = ?';
$values[]=$now;
$values[]=$keyValue;

$db = $this->get();
$st=$db->prepare($q);
$st->execute($values);
}


/**
* Like updateOneField(), but to set multiple fields in one go, and also return the new record.
*/
public function updateRecordAndFetch($tablename, $d, $keyField, $keyValue, $now = false){
$this->updateRecordHelper($tablename,$d, $keyField, $keyValue, $now);
$q = 'SELECT * FROM '.$this->quoteIdentifier($tablename).' WHERE '.$this->quoteIdentifier($keyField).'=?';
$db = $this->get();
$statement = $db->prepare($q);
$statement->execute(array($keyValue));
return $statement->fetch(\PDO::FETCH_ASSOC);
}



//TODO URGENT: I have insertRow, but updateRecord. Be consistent with one or the other. --> I suggest we use updateRow()




/**
 * 
 * @param string $tablename
 * @param string $keyField The field to match $keyValue on. E.g. "id". It must be UNIQUE.
 * @param mixed $keyValue The unique identifier for the row to delete.
 */
public function deleteOneRecord($tablename, $keyField, $keyValue){
$q = 'DELETE FROM '.$this->quoteIdentifier($tablename).' WHERE '.$this->quoteIdentifier($keyField).'=? LIMIT 1';
$db = $this->get();
$statement = $db->prepare($q);
$statement->execute(array($keyValue));
}


/**
 * Use to delete 0 or more records
 * 
 * @param array $KV @see getAllForMultipleKeys()
 */
public function deleteAllForMultipleKeys($tn, $KV, $operator="AND"){
$q = "";    //We built up just the WHERE clause, then prefix the rest of the query
$params = array();

foreach($KV as $key => $value){
    if(is_object($value) && get_class($value)=="Closure"){
        $value = $value($tn,$key);
        }

    $key = $this->quoteIdentifier($key);
    if($q == "")$q=$key;
    else $q.=" ".$operator." ".$key;

    if(is_array($value)){
        $cnt = count($value);
        if($cnt < 1)continue;   //No matches, skip it
        $q.=' IN (?' . str_repeat(',?',$cnt-1) . ')';
        $params = array_merge($params, $value); //Append the new entries
        }
    else{
        $q.='=?';
        $params[] = (string)$value;    //Might throw if an object that cannot be converted
        }
    }

$q = 'DELETE FROM ' . $this->quoteIdentifier($tn) . " WHERE " . $q;
    
$db = $this->get();
$statement = $db->prepare($q);
$statement->execute($params);
}


/**
* Use this for a query that will return a single row, single field, which is a number.
*
* @param string $q E.g. "SELECT COUNT(*) FROM mytable WHERE x = ?". NB. no additional
*     quoting or escaping is done, so the SQL must be valid SQL.
* @param mixed $values An array of the values for the prepared statement. If a single value
*      then it will be put in an array for you. If not a prepared statement, give this as null.
* @return Int
*
* @todo We need a higher-level version that will just take $tn,$field and the clause to match against $field,
*    and do all the $this->quoteIdentifier() calls, etc.
*/
public function countQuery($q, $values = null){
$dbh = $this->get();
if($values === null){
    $statement = $dbh->query($q);
    }
else{
    if(!is_array($values))$values = array($values);
    $statement = $dbh->prepare($q);
    $statement->execute($values);
    }
return (int)$statement->fetchColumn();
}


/**
*
*  @todo For id, could add the SQL Server way, too, shown here: http://www.w3schools.com/sql/sql_autoincrement.asp
*/
public function getCreateTableSQLForId(){
if($this->getType() == 'sqlite'){
    //An INTEGER marked as PRIMARY KEY will be an alias for ROWID, which means it
    //   will be nice and efficient (In fact we get it for free, as ROWID is there anyway.)
    return "id INTEGER PRIMARY KEY,";
    }
return "id INTEGER NOT NULL AUTO_INCREMENT PRIMARY KEY,";  //This way is MySQL
}


/**
 * 
 * @param File $fp Where to write the csv to. Note: PHP's fputcsv is used.
 *     If you want a string, @see exportAsCsvString
 *     And if you wanted export to stdout @see exportAsCsvStdout
 * 
 * @param Array $options It has these keys:  (all optional, unless otherwise stated)
 *    'q': string: The query. Required if tablename not given. If given, then tablename and fieldsToInclude
 *      are ignored, but fieldsToExclude is still processed.
 *    'tablename': string: (required unless 'q' given) The table to query. You must escape it yourself
 *       if it contains unusual characters.
 *    'fieldsToInclude': String or Array: List of fields to fetch. If blank then '*' is fetched.
 *    'orderBy': Which field to order by. (If more flexibility needed, set 'q' directly.)
 *        It is valid to give 'age DESC', etc. if you want descending order. I.e. the given string
 *        is used directly, so escape first if necessary.
 * 
 *    'data': Array: If this is set then you are assumed to have already fetched the data from
 *        that database (or wherever), and the above fields are ignored. The below fields can
 *        still be used. The format should be compatible with PDO::FETCH_ASSOC, i.e. an outer
 *        simple array, an associative inner array, each of which has the same fields.
 * 
 *    'fieldsToExclude': Array: List of fields to exclude. These are removed once the
 *        data has been received from the DB.
 *    'columnProcess': Array: Key is a field name, value is a function instance. The callback takes
 *        two parameters: the value from one row, and then this $options parameter, as a reference.
 *        It returns the new value to use.
 *    'rowProcess': Function: It is given two parameters: an array of the row, and this $options parameter, as a reference.
 *        It returns the new row.
 *        Note: if both rowProcess and columnProcess given, then rowProcess is done first.
 *    'timestampFields': Array: A list of fields that are currently in secs-since-1970 format,
 *        and should be formatted as human-readable strings. Processed after rowProcess and columnProcess.
 *    'timestampMsFields': Array: Like timestampFields, but it adds milliseconds. (If the field
 *        is integer, not decimal, then it just adds .000)
 *    'timestampFormat': String: defaults to "Y-m-d H:i:s"  (If timestampMsFields then the 's'
 *        gets replaced with MsFormat, done using sprintf. By default that gives three decimal places.)
 *    'i18nJsonFields': Array: fields here are assumed to be json-encoded, and to be either strings
 *        or arrays of strings, where the key is the language. You must set i18nPreference if setting this.
 *        Processed after rowProcess and columnProcess.
 *    'i18nPreference': array: E.g. array('ja','en') shows Japanese if available, otherwise English,
 *        otherwise nothing.
 *    'headerRow': Mixed: If array then it is used as the header row. If boolean true then there
 *        will be no header row. The default of boolean false means the array keys of the first row
 *        will be used (this will be after fieldsToExclude and rowProcess, so any columns that
 *        got removed or added will be considered.
 *        If a string, then it will be put in the first column of the header row, and the other
 *        columns will be blank (i.e. don't give a ready-made csv string for headerRow - give an array).
 *        (Or, for full control, you could output your headerRow to $fp before calling this function!)
 *    'msFormat': string: defaults to "%.3f"
 *    'delimiter': string: defaults to comma
 *    'enclosure': string: defaults to double quote
 *    'escape_char': string: defaults to "\"
 * @return (none)
 * 
 * @todo In future an option  to map field names to the names to use in the header row.
 * 
 * @todo This does a fetchAll() then processes them. For really large tables that might run out of
 *    memory, so we ought to use cursor methods.
 *
 * @todo URGENT Add $this->quoteIdentifier()
        --> Consider a $this->quoteIdentifierArray()   (and share with insert/update/etc.)
 */
public function exportAsCsv($fp,$options){
$dbh = $this->get();

if(!array_key_exists('data',$options)){
    if(!array_key_exists('q',$options)){
        if(array_key_exists('fieldsToInclude',$options)){
            if(is_array($options['fieldsToInclude']))$fields = implode(',',$options['fieldsToInclude']);    //TODO: ought to escpae them
            else $fields = $options['fieldsToInclude'];
            }
        else $fields = '*';
        $q = "SELECT {$fields} FROM {$options['tablename']}";
        if(array_key_exists('orderBy',$options))$q.="ORDER BY ".$options['orderBy'];
        $options['q'] = $q;
        }

    $statement = $dbh->prepare($options['q']);
    $statement->execute(array());
    $rows = $statement->fetchAll(\PDO::FETCH_ASSOC);
    }
else{
    $rows = $options['data'];
    }

$fieldsToExclude = array_key_exists('fieldsToExclude',$options)?$options['fieldsToExclude']:array();
$delimiter = array_key_exists('delimiter',$options)?$options['delimiter']:',';
$enclosure = array_key_exists('enclosure',$options)?$options['enclosure']:'"';
$escape_char = array_key_exists('escape_char',$options)?$options['escape_char']:"\\";
$timestampFormat = array_key_exists('timestampFormat',$options)?$options['timestampFormat']:"Y-m-d H:i:s";
$msFormat = array_key_exists('msFormat',$options)?$options['msFormat']:"%.3f";
$headerRow = array_key_exists('headerRow',$options)?$options['headerRow']:false;

foreach($rows as $row){
    foreach($fieldsToExclude as $field)unset($row[$field]);
    if(array_key_exists('rowProcess',$options))$row = $options['rowProcess']($row,$options);
    if(array_key_exists('columnProcess',$options)){
        foreach($options['columnProcess'] as $k => $F){
            $row[$k] = $F($row[$k],$options);
            }
        }
    if(array_key_exists('timestampFields',$options)){
        foreach($options['timestampFields'] as $k){
            if(!is_numeric($row[$k]))continue;
            $row[$k] = date($timestampFormat,$row[$k]);
            }
        }
    if(array_key_exists('timestampMsFields',$options)){
        foreach($options['timestampMsFields'] as $k){
            if(!is_numeric($row[$k]))continue;
            $secs = fmod($row[$k], 60.0);
            $fmt = $timestampFormat;
            $fmt = str_replace('s',sprintf($msFormat,$secs),$fmt);
            $row[$k] = date($fmt,$row[$k]);
            }
        }
    if(array_key_exists('i18nJsonFields',$options)){
        foreach($options['i18nJsonFields'] as $k){
            $v = json_decode($row[$k],true);
            if(is_array($v)){
                foreach($options['i18nPreference'] as $lang){
                    if(array_key_exists($lang,$v)){$v=$v[$lang];break;}
                    }
                }
            $row[$k]=$v;
            }
        }
        
    if($headerRow !== true){    //I.e. an array (pre-defined), or false (use array keys of first row)
        if($headerRow === false)$headerRow = array_keys($row);
        fputcsv($fp, $headerRow, $delimiter, $enclosure, $escape_char);
        $headerRow = true;  //So it is only done once
        }
        
    fputcsv($fp, $row, $delimiter, $enclosure, $escape_char);   //Does the \n at the end
    }
}

/**
 * A wrapper around exportAsCsv() that sends back a csv file as an attachment
 * 
 * @param Array $options @see exportAsCsv()
 * @param $withHeaders If true then it does content-type and no-cache headers
 * @param $filename If set then it also adds a Content-Disposition header. (Ignored if !$withHeaders)
 * 
 * @internal The Cache-Control header is http/1.1, and the Pragma is http/1.0.
 */
public function exportAsCsvStdout($options, $withHeaders=false, $filename=null){
if($withHeaders){
    header("Content-Type: text/csv");  //Or 'Content-Type: application/excel'
    if($filename !== null)header('Content-Disposition: attachment; filename="' + $filename + '"');
    header("Cache-Control: no-cache, no-store, must-revalidate"); // HTTP 1.
    header("Pragma: no-cache");
    header("Expires: 0");
    }
$fp = fopen('php://output', 'w');
$this->exportAsCsv($fp,$options);
fclose($fp);
}

/**
 * 
 * @param array $options @see exportAsCsv()
 */
public function exportAsCsvString($options){
$fp = fopen('php://temp', 'r+');   //"r+" means open for reading and writing.
$this->exportAsCsv($fp, $options);
rewind($fp);
$s = fread($fp,PHP_INT_MAX);
fclose($fp);
return $s;
}


/**
 * This compares an object before and after some changes; useful for the update functions.
 * 
 * It is designed for use in the onChange() callback of RouteWithList, but is of
 * general use when testing to see if an object has been updated and, if so, how.
 * 
 * @param Mixed $d1 The instance before. Expected to be an assoc. array, but if a string, it is deserialized.
 * @param Array $d2 The instance after. Expected to be an assoc. array, but if a string, it is deserialized.
 * 
 * @internal This could have gone in RouteWithList too. I put it here as it feels
 *   like it is closer to the insert and update functions in this class; i.e. it might
 *   be used without RouteToList, whereas the reverse is very unlikely to be true.
 */
public static function extractChanges($d1,$d2){
if(is_string($d1))$d1 = unserialize($d1);
if(is_string($d2))$d2 = unserialize($d2);

$changes = array();
foreach($d2 as $k => $v){   //First look at the new data, to see what has been added/changed
    if(!array_key_exists($k,$d1))$changes[$k] = $v;
    else if($v != $d1[$k])$changes[$k] = $v;
    //else no change
    }
foreach($d1 as $k=>$v){ //Then check old data, to see if anything was removed.
    if(!array_key_exists($k,$d2))$changes[$k] = null;
    }
    
return $changes;
}


/**
 */
public static function extractChangesVerbose($d1,$d2){
if(is_string($d1))$d1 = unserialize($d1);
if(is_string($d2))$d2 = unserialize($d2);

$changes = array();
$changeReport = "";
foreach($d2 as $k => $v){
    if(!array_key_exists($k,$d1))$changeReport.="Key added:$k:".print_r($v,true);
    else if($v != $d1[$k])$changeReport.="Changed:$k:From ".print_r($d1[$k],true)." to ".print_r($v,true);
    else continue;
    $changes[$k] = $v;
    }
foreach($d1 as $k=>$v){
    if(!array_key_exists($k,$d2))$changeReport.="Key removed:$k:Previous value:".print_r($v,true);
    else continue;
    $changes[$k] = null;
    }

return array($changes,$changeReport);
}

}   //End of class PDOWrapper



