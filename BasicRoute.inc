<?php
/**
* QQ API: A simple base route class
*
* @copyright QQ Trend Ltd, 2013, 2014
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

include_once("RouteBase.inc");

/**
* This is a convenience function for deriving route classes from.
*
* The Routing class only requires a route class to have a handle($input) function,
* where $input is an array of URL parameters.
* (It will also set 'action', 'originalAction' and 'U', so those variables are defined
*  in this class too, more for documentation than anything - they are not actually
*  used by this base class.)
*
* The Routing class also looks for needUser, so that is defined here, as true, too.
* A constructor should set it to false if not needed.
*
* For routes derived from this class, they instead have to set $this->params[] in
* the constructor and have an action(...) function.
*
* This base class also provides some hooks: preProcess() and postProcess(). They
* should be treated just like a constructor, by calling the parent version first (or
* possibly last, depending on what you are doing in them).
*/
abstract class BasicRoute extends RouteBase{

/**
* A derived class can set this to false if a user validation is not needed.
* But the default is that we need a validated user before we will even call
* handle(), let alone action(). So if using routes with this set to false then
* you most likely want a custom Routing::$do_early_user_validation (that either
* always returns false, or that uses $obj->action to return false for only
* the routes that need a user).
*/
public $needUser = true;

/**
* The name of the route.
*
* Useful for common code in an intermediate base class, being shared
* by some similar routes.
*/
public $action = null;

/**
* Like $action, but Routing allows some rewriting, so this tells
* the name as it was found in the URL.
*/
public $originalAction = null;

/**
* If set to true then it will append to a log file ("logs/route.xxx.log" where xxx
* is $action) the values it is about to pass to $action. I.e. it does it after
* preProcess() and validation().
*
* @internal No flexibility given for the log filename: this is mainly for quick-and-easy
*  troubleshooting. If you need something complicated write it yourself.
*  The code is in validateInputs() so that something more friendly could be done in future
*  like adding the variable name.
*/
public $logActionInputs = false;

/**
* The currently logged on user.
*
* NOTE: if $needUser is false, this will be null
* Otherwise it should always be a valid User object by the time handle() (and therefore action())
* is called. The format of the User object (even whether it is Object, Array, String, etc.) is
* application-specific.
*/
public $U = null;

/**
* Parameters, in order they should be passed to $this->action.
* 
* Each value is an associative array with these keys:
*     name: Text. This is the name of the POST variable we look for. 
*             Also used when generating documentation.
*     comment: Human-readable text. Just used when generating documentation.
*     default: If not specified then it is required. (All required parameters should
*       come first; if not then the default on earlier optional parameters would never
*       get used.)
*       NOTE: if you specify default, but set it to null, then null is passed to
*       action() (i.e. it will still count as an optional parameter).
*     type: If set then it is cast to this ('int','float', etc.).
*       If not set it gets passed as a string.
*       See below for special types. You can define your own in the validateInput() hook.
*       A blank type is treated like 'string'.
*     min, max: For numeric types it will range-check against this.
*     minchars/maxchars: String types range-check length against this.
*     minSize/maxSize: For array types, to check array size.
*     categories: Used by 'category' type.
*
* The supported types are:
*   string, id: Passed along as-is. Uses minchars/maxchars.
*   regex: Like string, but matched again 'regex' (which is required)
*   yyyymmdd: Like string, but but specifically for a datestamp in that exact format. (It
*      checks the mm and dd parts are correct. Year just has to be 4 digits, currently.)
*   comma: Split on comma, pass an array (NB. it does not
*       consider escaping; i.e. only use this when your values don't
*       contain comma or double quotes!)
*   int0plus, int1plus: expects a positive integer.
*   float: Expects any number (it is cast to float; non-numbers will end up as 0)
*   bool: Y/1/T/true give true, and everything else gives false.
*   array: Expects a ready-made array. Uses minSize/maxSize.
*   category: Like string, but only certain values accepted. Uses categories.
*   null: For placeholders. null given to action.
*   strtotime, gmstrtotime: uses strtotime() to parse a date (and the value passed is
*       the unix time integer, in local machine time).
*       On a server when loal time is UTC, these usually behave identically.
*       However use strtotime() if the user might specify the timezone explicitly.
*       (If the user specifies explicit timezone with gmstrtotime, behaviour is undefined...)
*       NOTE: strtotime might cope with a unix timestamps (secs since 1970 integer), but
*       gmstrtotime will fail with it.
*
* @todo How about "timestamp" that requires "YYYY-MM-DD HH:MM:SS" format, but
*       just does the validation then passes along a string?
*                datafeed for how to interpret this.
* 
* @todo Add another type 'comma_trim', that will also run trim() on each element?
*     (I.e. use preg_split() for that). Or perhaps we should define filters, and actually
*     run trim in there?
*
* @todo The original design had a validate member, a function to validate/modify it. As
*     not used/needed so far I've removed it. Easy to add in later if needed.
*/
public $params = array();

//----------------------------------

/**
* Constructor (does nothing here)
*
* @internal This exists primarily so each derived class can always do
*   parent::__construct();  and not have to worry.
*/
function __construct(){}

/**
* Parse/validate the given inputs then call $this->action()
*
* NOTE: it is possible to mix $params and Application::$POST (e.g. mix values in the
* URL with POST data. The $params entry dominates the $POST value.)
* E.g. if we take 3 parameters: feedname, ix1, ix2, we might include
* the feedname as part of the URL, but given ix1 in POST data, and leave
* ix2 to be the default.
* IMPORTANT: Parameters are taken from $param in order. So there is
* no way to use $_POST for first parameter and then get the second parameter
* from the URL. If you need that much flexibility then use POST for everything.
*
* @param Array $inputs Simple array of URL parameters. Could be empty.
*
* @todo Remove one of strtotime or gmstrtotime
* @todo Error-checking on strtotime, so we look for ===false.
* @todo Error-checking on bool, instead of treating "everything else" as false. I.e. just "Nn0Ff" and perhaps blank string.
* @todo Error-checking on id: numeric only.
*/
public function handle($inputs){
if(!$this->preProcess($inputs))return;
$args = $this->validateInputs($inputs);
call_user_func_array(array($this,'action'),$args);
$this->postProcess();
}

/**
* Hook before input parameters are processed
*
* @param Array $inputs The URL parameters. This function is allowed to modify them.
* @return Bool Return true. Throw exception on error.
*     Only return false if doing something clever, output has
*     already been done, and you do not want the rest of route processing to continue.
*/
protected function preProcess(&$inputs){
return true;
}

/**
* Hook after action() is called.
*
* No return value. Throw exception on error.
*
* @internal Not usually any point in overriding it in a leaf class (might as well
*    put your code in action()), so it is mainly for an intermediate base class to
*    have common code shared by a set of routes. E.g. The Routing::output() function
*    can be called from here, debug info. could be conditionally added to
*    the output, etc.
*/
protected function postProcess(){}


/**
* Extracts the parameters from a mix of URL inputs, POST data, and defaults,
* and also validates each (throwing an exception on the first problem).
*
* If $this->params has not been set then it will always return a blank array,
* whatever is in $inputs or POST data.
*
* @param Array $inputs @see handle()
* @return Array Simple array of the parameters, in the order specified by $this->params.
*/
private function validateInputs($inputs){
$args=array();  //The filtered, defaulted, params we pass to action() are built up in this.
foreach($this->params as $ix=>$d){
    if(array_key_exists($ix,$inputs))$v = $inputs[$ix];
    elseif(array_key_exists($d['name'],Application::$POST))$v=Application::$POST[$d['name']];
    elseif($d['type'] == 'null')$v = null;    //Special case (implicit default)
    else{   //Not given. See if there is a default
        if(!array_key_exists('default',$d)){
            throw new ErrorException('Required parameter (%1$s) not given',array($d['name']));
            }
        $args[]=$d['default'];
        continue;   //No need to fall through and validate it
        }

    switch($d['type']){
        case 'comma':$args[]=explode(',',$v);break;
        case 'int0plus':
            $v=(int)$v;
            if($v<0)throw new ErrorException('%1$s: Must be positive integer. (not %2$s)',
                array($d['name'],$v));
            $args[]=$v;
            break;
        case 'int1plus':
            $v=(int)$v;
            if($v<1)throw new ErrorException('%1$s: Must be positive integer (1+). (not %2$s)',
                array($d['name'],$v));
            $args[]=$v;
            break;
        case 'float':$args[]=(float)$v;break;
        case 'strtotime':$args[]=($v=='')?null:strtotime($v);break;
        case 'gmstrtotime':$args[]=($v=='')?null:strtotime($v.' UTC');break; //TODO: this is redundant,as
            //the script should always be running in UTC timezone. Therefore this line is identical
            //to the previous one. Is there any point having both, with that in mind?   --> $v might contain explicit timezone
        case 'bool':
            if(!$v)$args[]=false;
            else $args[]=(strpos('Yy1Tt',$v[0])!==false);
            break;
        case 'regex':   //Like string, but has 'regex' instead of minchars/maxchars
            if(!preg_match($d['regex'], $v)){
                throw new ErrorException('%1$s: Invalid format.', array($d['name']));
                }
            $args[]=$v;
            break;
        case 'yyyymmdd':   //Treated as string, but looking for this exact datestamp format.
            if(!preg_match('/^(\d{4})(\d{2})(\d{2})$/', $v, $dateParts)){
                throw new ErrorException('%1$s: bad datestamp. Should be YYYYMMDD.', array($d['name']));
                }
            $month = $dateParts[2];$day = $dateParts[3];
            //if(array_key_exists('minyear',$d)){...}
            //if(array_key_exists('maxyear',$d)){...}
            switch($month){
                case 1:case 3:case 5:case 7:case 8:case 10:case 12:
                    if($day < 1 || $day > 31)throw new ErrorException('%1$s: bad datestamp. Should be YYYYMMDD. You gave %2$d for DD.', array($d['name'], $day));
                    break;
                case 4:case 6:case 9:case 11:
                    if($day < 1 || $day > 30)throw new ErrorException('%1$s: bad datestamp. Should be YYYYMMDD. You gave %2$d for DD.', array($d['name'], $day));
                    break;
                case 2:
                    //TODO: doesn't get into leap year detection yet.
                    if($day < 1 || $day > 29)throw new ErrorException('%1$s: bad datestamp. Should be YYYYMMDD. You gave %2$d for DD.', array($d['name'], $day));
                    break;
                default:
                    throw new ErrorException('%1$s: bad datestamp. Should be YYYYMMDD. You gave %2$d for MM.', array($d['name'], $month));
                }

            $args[]=$v;
            break;
        case 'string':case 'id':case '':
            //NB. "minchars" rather than "minlength" to emphasize it is UTF8 characters, not bytes.
            if(array_key_exists('minchars',$d) && mb_strlen($v) < $d['minchars']){
                throw new ErrorException('%1$s: Must be at least %2$d characters (you gave %3$d).',
                    array($d['name'],$d['minchars'],mb_strlen($v)));
                }
            if(array_key_exists('maxchars',$d) && mb_strlen($v) > $d['maxchars']){
                throw new ErrorException('%1$s: Max of %2$d characters (you gave %3$d).',
                    array($d['name'],$d['maxchars'],mb_strlen($v)));
                }
            $args[]=$v;
            break;
        case 'array':
            if(!is_array($v))throw new ErrorException('%1$s: Must be an array.',array($d['name']));
            if(array_key_exists('minSize',$d) && count($v)<$d['minSize']){
                throw new ErrorException('%1$s: Must have at least %2$d element(s) (only found %3$d).',
                    array($d['name'],$d['minSize'],count($v)));
                }
            if(array_key_exists('maxSize',$d) && count($v)>$d['maxSize']){
                throw new ErrorException('%1$s: Must have no more than %2$d element(s) (had %3$d).',
                    array($d['name'],$d['maxSize'],count($v)));
                }
            $args[]=$v;
            break;
        case 'category':
            //TODO: should we normalize $v, e.g. trim() and strtolower() ?
            //  ---> Decide based on flags (or callback??) in $d
            if(!in_array($v, $d['categories'])){
                if(@$d['canShowCategories'])throw new ErrorException( '%1$s: Must be one of: %2$s.',
                    array($d['name'], implode(',',$d['categories'])) );
                else throw new ErrorException( '%1$s: unexpected choice given.', array($d['name']) );
                }
            $args[] = $v;
            break;
        case 'categories':  //Use this for a csv list, but unlike comma each entry must be known.
            //Blank entries are quietly ignored.
            $categories=[];
            foreach(explode(',',$v) as $opt){
                $opt=trim($opt);
                if($opt=='')continue;
                if(!in_array($opt, $d['categories'])){
                    if(@$d['canShowCategories'])throw new ErrorException( '%1$s: Must be one of: %2$s.',
                        array($d['name'], implode(',',$d['categories'])) );
                    else throw new ErrorException( '%1$s: unexpected choice given.', array($d['name']) );
                    }
                $categories[]=$opt;
                }
            if(array_key_exists('minchoices',$d) && count($categories) < $d['minchoices']){
                throw new ErrorException('%1$s: Must give at least %2$d choices (you gave %3$d).',
                    array($d['name'],$d['minchoices'],count($categories)));
                }
            if(array_key_exists('maxchoices',$d) && count($categories) > $d['maxchoices']){
                throw new ErrorException('%1$s: No more than %2$d choices (you gave %3$d).',
                    array($d['name'],$d['maxchoices'],count($categories)));
                }
            $args[]=$categories;
            break;
        case 'null':    //This exists for placeholders
            //TODO: if $v is being given should we throw an error
            //      ---> make this customizable based on some entry in $d
            $args[] = null;
            break;
        default:
            $args[] = $this->validateInput($d,$v);  //Normally throws a SystemException
        }
    }

if($this->logActionInputs){
    Logger::log("logs/route.{$this->action}.log", print_r( $args , true) );
    }

//If we reach here then all inputs are fine, and have been prepared in $args.
return $args;
}



/**
* Hook to allow expanding validateInputs() with custom data types.
*
* Base-class behaviour is to throw an exception. (It throws a system
* exception as it indicates putting an entry in $this->params that
* is not supported; i.e. bad user input never causes this.)
*
* Usage would be something like this:
*       protected function validateInput($d,$v){
*           if($d['type']=='myType'){
*               ...validate it here...
*               return $v;
*               }
*           parent::_validateInput($d,$v);
*           }
*
* NOTE: it cannot be used to override the behaviour of the types
* supported in the base class. If you have a need for that then
* call it a different type name! (You can modify $this->params[] in
* either the constructor or preProcess().)
*
* @internal That example usage *does* allow overriding behaviour of
*  a custom type defined in an intermediate base class. There is
*  no way to stop that, but it should be avoided.
*
* @internal I deliberately did not make $d and $v reference parameters. They
*  are never used again once this function returns.
*/
protected function validateInput($d,$v){
throw new SystemException('%1$s: Unknown type: "%2$s"',array($d['name'],$d['type']));
}


}   //End of class BasicRoute

?>