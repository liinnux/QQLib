<?php
/**
* QQ API: A global settings class
*
* @copyright QQ Trend Ltd, 2013
* @author Darren Cook <darren@dcook.org>
* @license MIT
*
* @internal This class is designed to hold some globals
*   that don't really belong as static variables in the other classes,
*   and some static functions related to them.
*   Also to keep things together (e.g. $errorTemplateFile is only
*   used in Exception class, but needs to be in sync with $format).
*   Also to control access to $_POST (for unit testing, or to allow it to be $_REQUEST)
*   as well as making specifying defaults cleaner.
*/

namespace QQAPI;

class Application{
/** 
* This should be set as soon as we know it (in Router code), so that errors can be output correctly.
* Blank string means output as plain text.
*
* At the time of writing "json", "xml", "txt" and "html" are supported, with "txt" being the default.
* "HTML" expects a template file.
*
* Note: partial matching is used, so "json", "jsoncsv", "json_this_and_that" all get treated
* as json. Ditto for "xml" and "html".
*
* @internal Originally I had the idea of $format=='' would mean no preference. It would
*  then allow us to use a User preference for the default. However that makes everything
*  else so complicated for a relatively obscure use-case, that I've decided to not handle it
*  here. (If needed, we could track if $format was set from a default or not.)
*/
static public $format='';

/**
* Used if '' passed to setFormat().
* Note: if changing it to 'html' then self::$errorTemplateFile must also be set.
*/
static public $defaultFormat='txt';

/**
* Only needed for $format=="html".  It will be processed as a PHP file, so
* can be very flexible, but at a minimum it should contain: <?= $message ?>
*/
static public $errorTemplateFile='';

/**
* Unit tests should initialize this to array(): that will then prevent calls to header()
* and instead the headers will be appended to this.
*
* @todo I believe there are no unit tests using this at the current time.
*/
static public $unit_test_header_calls=null;

/**
* This exists primarily so a unit test can replace it with its own data.
*
* Almost the first thing your real code should do is assign $_POST to be this variable.
* All routes should get parameters from here, Application::$POST, not from $_POST or $_REQUEST.
*/
static public $POST = array();

//----------------------


/**
* Helper function to set the correct content-type, as well
* as headers needed for CORS, and anything else.
*
* NOTE: this will give an error if any output has already done. However
* until then it is fine to call it more than once: by default a header
* of a same name will be replaced. This is useful to allow a user preference
* to overload something (i.e. we call it once based on URL, but then if the
* URL was not explicit, once we've loaded the user information we look in
* there for the information).
*
* @todo The CORS stuff feels like it belongs elsewhere. Or rename this function
*   to setHeaders() or something like that.
*
* @todo Could validate against known formats, and complain immediately.
*   ---> Not just known formats, but supported. E.g. if a system *only* wants
*       to support "json", then we need a way (a public static class var) to specify this.
*   --> Maybe we also need control over the default, instead of '' being the default and meaning text/plain?
*/
public static function setFormat($format){
if($format=='')$format=self::$defaultFormat;

self::$format=$format;

$hs="Access-Control-Allow-Origin: *";   //Otherwise it won't work as AJAX from within firefox. see http://stackoverflow.com/questions/1171490/jquery-xmlhttprequest-and-status-code-0   //TODO: opening up security like this may be undesirable!!
if(!is_array(self::$unit_test_header_calls))header($hs);
else self::$unit_test_header_calls[]=$hs;

if(substr($format,0,3)=='xml')$hs="Content-Type: text/xml";
elseif(substr($format,0,3)=='csv')$hs="Content-Type: text/csv";
elseif(substr($format,0,4)=='json')$hs="Content-Type: application/json";
elseif(substr($format,0,4)=='html')$hs="Content-Type: text/html";
else $hs="Content-Type: text/plain";    //Use 'txt' as the default
if(!is_array(self::$unit_test_header_calls))header($hs);
else self::$unit_test_header_calls[]=$hs;
}




/**
* Gets a parameter from POST data (actually Application::$POST)
*
* If not given (and default not set) it will throw an exception
*
* NOTE: use getArray() or getString() where possible, for the extra checking.
* You will need this function when a parameter could be array or string.
*
* @internal See http://stackoverflow.com/q/68651 for why we have to convert
*   requests for "x.y" into "x_y".
*   NOTE: this means unit tests must also use underlines when setting POST!!
*/
public static function get($name,$default=null){
$name=str_replace('.','_',$name);
$v = array_key_exists($name,self::$POST)?self::$POST[$name]:$default;
if($v === null)throw new ErrorException("Bad or missing %$1s",array($name));
return $v;
}

/**
* Like get() but it expects an array so will throw if not one.
* By default it also expects it to have 1+ items.
*/
public static function getArray($name,$minSize=1,$default=null){
$name=str_replace('.','_',$name);
$v = array_key_exists($name,self::$POST)?self::$POST[$name]:$default;
if(!is_array($v))throw new ErrorException('Bad or missing %1$s',array($name));
if(count($v)<$minSize)throw new ErrorException('Too few items in %1$s',array($name));
return $v;
}

/**
* Like get() but it expects a string
*
* The point of getString() vs. using get() is that get() can return arrays; if
*   you are not expecting an array to be passed in then use this function, so that if an
*   array is passed in it will complain.
*/
public static function getString($name,$default=null){
$name=str_replace('.','_',$name);
$v = array_key_exists($name,self::$POST)?self::$POST[$name]:$default;
if($v === null || !is_string($v))throw new ErrorException('Bad or missing %1$s',array($name));
return $v;
}

/**
* Like get() but it expects a series of digits, and can range-check them for you
*
* @internal ctype_digit() is called before cast to (int) as otherwise bad values
*   end up as zero.
*/
public static function getInt($name,$min=null,$max=null,$default=null){
$name=str_replace('.','_',$name);
$v = array_key_exists($name,self::$POST)?self::$POST[$name]:$default;
if($v === null || !ctype_digit($v))throw new ErrorException('Missing or non-integer %1$s',array($name));
$v = (int)$v;
if($min!==null && $v<$min)throw new ErrorException('%1$s is %2$d; minimum is %3$d',array($name,$v,$min));   //Safe to put $v in the string as it passed ctype_digit() above
if($max!==null && $v>$max)throw new ErrorException('%1$s is %2$d; maximum is %3$d',array($name,$v,$max));   //Safe to put $v in the string as it passed ctype_digit() above
return $v;
}

/**
* Like getInt() but it allows decimal places too.
* 
* Scientific notation is rejected. Leading decimal point is allowed (e.g. ".01" and "0.01" are the same),
* 
* Note: PHP 7 rejects hex strings, but earlier versions might accep them.
*
* @internal ctype_digit() is called before cast to (int) as otherwise bad values
*   end up as zero.
*/
public static function getDouble($name,$min=null,$max=null,$default=null){
$name=str_replace('.','_',$name);
$v = array_key_exists($name,self::$POST)?self::$POST[$name]:$default;
if($v === null || !is_numeric($v))throw new ErrorException('Missing or non-decimal %1$s',array($name));
$v = (double)$v;
if($min!==null && $v<$min)throw new ErrorException('%1$s is %2$d; minimum is %3$d',array($name,$v,$min));   //Safe to put $v in the string as it passed is_numeric() above
if($max!==null && $v>$max)throw new ErrorException('%1$s is %2$d; maximum is %3$d',array($name,$v,$max));   //Safe to put $v in the string as it passed is_numeric() above
return $v;
}



}