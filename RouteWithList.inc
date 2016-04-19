<?php
/**
* QQ API: A higher-level Route base class than BasicRoute, which
* assumes you want to send back data objects to the client, and
* also allows for some of them to always be loaded.
*
* @copyright QQ Trend Ltd, 2014
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

include_once "BasicRoute.inc";

/**
*/
abstract class RouteWithList extends BasicRoute{
/**
* Unix time (as float, to microsecond accuracy) when constructor was called
*
* @internal This is public in case a unit test, or some sophisticated usage, requires
*    setting it directly.
*
* @internal This is done here, rather than just calling time() each time
*   we need it, partly for unit testing control, and partly so it is consistent
*   through the whole processing. (E.g. if measured in seconds and it rolled
*   over from one second to the next between two calls, it could give strange
*   behaviour).
*
* @internal Done as float, in case it is needed to be accurate. When used
*   in functions where int is expected, the microseconds part will simply
*   be truncated away. An explicit cast should not be needed.
*/
public $startTime;

/**
* This is what is sent back to the client (normally, as JSON)
*
* The 'list' element is a special one.
*/
protected $d=array(
    'list'=>array(),
    );

/**
* Key is the special var name (the $name parameter given to addSpecial),
* and the value is the string returned by serialize(), at the time addSpecial()
* was called.
*/
private $serializedSpecials=array();

//---------------------------
/**
*/
function __construct(){
parent::__construct();
$this->startTime = microtime(true);
}

/**
* This starts tracking of the variable, which should already exists as $this->$name.
* 
* You call this in the constructor (or preProcess()).
* Then if, after action(), the value of $this->$name has changed, then onChange will
* be called (passing $name).
*
* IMPORTANT: You should never add data for $this->$name to $this->d['list'][] inside
* action() once you've called this function: that is one of the jobs postProcess() and
* then onChange() will do.
*
* IMPORTANT: If using this functionality, your derived class must define onChange() (the
* base class version throws a SystemException
*/
protected function addSpecial($name){
$this->serializedSpecials[$name] = serialize($this->$name);
}

/**
* Does the output, as well as checks for special values changing.
*/
protected function postProcess(){
parent::postProcess();  //NB. At time of writing this does nothing.
foreach($this->serializedSpecials as $name=>$before){
    $after = serialize($this->$name);
    if($before != $after)$this->onChange($name,$after,$before);
    }
Routing::output($this->d);
}

/**
* This is needed to be defined in the derived class if (and only if) you are also calling addSpecial().
* 
* @see PDOWrapper::extractChanges() for a useful helper function (for when $after/$before represent
*   a DB class).
*/
protected function onChange($name,$after,$before){
throw new SystemException("Derived class should've defined onChange()");
}

}   //End of class RouteWithList

?>