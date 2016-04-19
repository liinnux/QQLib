<?php
/**
* QQ API: Exception handling
*
* @copyright QQ Trend Ltd, 2012, 2013
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

include_once "application.inc";
include_once "logger.inc";

/**
* Base class for our exceptions
*
* All the logic and vars are in this base class: the derived classes are (mostly) just descriptive.
*
* What it adds beyond the basic PHP exception class is:
*   * Formatting as text, json, xml, etc.
*   * An optional reference to a user object.
*   * Logging
*   * Backup logging (for when main log destination cannot be opened, etc.)
*   * Extra info that can be inserted in messages.
*   * Extra message just put in the logs.
*   * Some design-for-testing support
*
* NOTE: if you want certain information from requests included, then
* use self::$details. This can be accessed directly, but the better way is to
* call Exception::addToDetails().
*
* @internal It has a number of static vars and functions, as it also handles reporting.
*   I suppose we could move all those static vars to either a base class, or a helper
*   utility class.
*/
class Exception extends \Exception{

/**
* This is used to translate the messages that can be given to the constructor,
* as well as the couple of internal messages.
*
* If null, the default, then no translations are used. This is fine for English.
* Otherwise set it to an array where the key is the English, and the value is
* the string in the target language.
*
* Note: it can also be used to override the default messages. E.g. for more
* verbose messages assign this:
*    array(
*       'System error'=>'System error. Try again later.',
*       'Please authenticate'=>'Please authenticate. If you see this in a browser, then please click reload to authenticate.',
*       ),
*
* The keys can be anything, so do not need to be in the base language. E.g. they
* can be error codes:
*   array(
*       '001'=>'You must give %1$d choices',
*       '002'=>'That is an invalid value for email',
*       );
* However this tends to make the point where the exception is thrown harder to read. So
* it is best saved for cases where the message is very long. (It is fine to mix real strings
* and error codes!)
*
* @see self::translate()
* 
* @internal For simplicitly, documentation assumes the base language is
*  English. The code does not require this, as the error-code example shows,
*  though the hard-coded strings in this class are in English.
*/
static public $strings=null;


/**
* An optional set of key/value pairs that describe the request.
*
* For instance with a typical web access, we'll put keys in here for
* IP address, username, user agent.
*
* @internal you can make any guarantee on the order that entries will be
* output in. Use ksort(Exception::$details) if you want to be sure
* of the order (e.g. for unit test asserts).
*/
static public $details=array();

/**
* This is where normal errors are written. E.g. ErrorException would go here.
*/
static public $normalErrorLog="logs/normal_error.log";

/**
* This is where the more serious system errors are written.
*
* This is the log we'd be checking and emailing to an admin regularly.
*/
static public $systemErrorLog="logs/system_error.log";

/** 
* Extra information about the problem, to only be shown to the system administrator
* 
* If a string it is written to the logfile as-is; if not then it is run through print_r(),
* so that arrays and objects get converted to a string format.
*/
public $detailedMessage;

//---------------------------------
/**
* 
* @param String $message This is the message that will be shown to the user,
*     as well as written to the log.
*     It can contain custom information (see below). Remember to use single quotes!
*     To have it translated into user's language, assign self::strings to be an array
*     based on their language preference.
* @param Array $params Parameters to replace inside $message
*   (which should be a sprintf format string if using this array).
*   They'll be inserted in order with sprintf's %N$d and %N$s format,
*   where N is 1..N, corresponding to the 0..N-1 index in $params.
*   (Actually we use vsprintf, not sprintf.)
*   IMPORTANT: If using this, remember you must give the $message in single quotes. Or, if you use
*   double quotes, you need to escape the dollar signs. I.e. either of these will work:
*       throw new ErrorException('Hello %2$s, %1$s',array('People','World'));
*       throw new ErrorException("Hello %2\$s, %1\$s",array('People','World'));
*     ("Hello World, People" is output for both.)
* @param String $detailedMessage This is extra information, written just to the
*    logfile, and not returned to the user.
*    NOTE: the logfile will automatically get file, line number, the stack trace,
*    as well as all the entries in self::$details. So $detailedMessage does not
*    need to be used for any of those.
*/
public function __construct($message,$params=array(),$detailedMessage="") {
parent::__construct(@vsprintf(self::translate($message),$params));
$this->detailedMessage=$detailedMessage;
}

/** 
* Call this when we want to output the message to the user (i.e. to tell them
* more exactly what went wrong).
*/
public function reportProblemLogAndExit(){
self::encodeAndOutputMessage($this->message);
$this->logMessage(self::$normalErrorLog);
exit;
}

/**
* @see reportProblemLogAndExit
*
* @internal This exists more for unit testing than anything else!
*/
public function reportProblemLogButDoNotExit(){
self::encodeAndOutputMessage($this->message);
$this->logMessage(self::$normalErrorLog);
}

/**
*/
public function reportSystemErrorLogAndExit(){
self::encodeAndOutputMessage('System error. Try again later.');
$this->logMessage(self::$systemErrorLog);
exit;
}

/**
*
* For escaping, we assume json_encode() escapes all special characters.
* For text format, we also do no escaping at all.
* For XML, we do escaping, and also send the XML version tag.
*
* Text format is the default when $format is unrecognized.
*
* @param String $message The message to show to the user. It should already
*     be in the desired language, and have had any parameters already inserted.
*
* @todo Need to output a suitable header? Or will Router already have done that?
*     ---> Perhaps we can detect if header already sent?
*        ---> When used from commandline we don't want the header sent though...
*   ====> It seems most of the time header() won't have been sent. There is a Router function
*         to help, but it is current non-static and protected...
*/
protected static function encodeAndOutputMessage($message){
if(substr(Application::$format,0,4)=='json'){
    echo json_encode(array('timestamp'=>gmdate("Y-m-d H:i:s").'Z','error'=>$message))."\n";
    }
elseif(substr(Application::$format,0,3)=='xml'){
    $msg=str_replace(array( "&","<", ">", "\"", "'"),
        array("&amp;","&lt;", "&gt;", "&quot;", "&apos;"), $message);  //Encode for XML
    echo '<?xml version="1.0" encoding="UTF-8"?>'."\n";
    echo '<response timestamp="'.gmdate("Y-m-d H:i:s").'Z">'.
        '<error>'.$msg.'</error>'.
        '</response>'."\n";
    }
elseif(substr(Application::$format,0,4)=='html'){
    if(!Application::$errorTemplateFile || !file_exists(Application::$errorTemplateFile)){
        Logger::log(self::$systemErrorLog,"format is html, but templateFile (".Application::$errorTemplateFile.") not set.");
        echo '<html><head><title>Error</title></head><body>'.htmlspecialchars($message).'</body></html>';
        }
    else{
        include_once(Application::$errorTemplateFile);
        }
    }
else echo gmdate("Y-m-d H:i:s").'Z:'.$message."\n";
}


/**
* Use this for other exceptions, so we report and log them consistently.
*/
public static function reportOtherExceptionAndExit($e){
self::encodeAndOutputMessage(self::translate('System error.'));
$msg=date("Y-m-d H:i:s T");
$msg.=':'.(string)$e."\n";    //Let the exception describe itself
foreach(self::$details as $k=>$v)$msg.="$k=$v\n";
Logger::log(self::$systemErrorLog,$msg);
exit;
}


/**
* Appends full information on the exception to a logfile.
*
* The entry will take up multiple lines in the log file. Each entry starts with a
* timestamp, and finishes with "---" on a line by itself. However it is meant
* to be human-readable more than machine-readable.
*/
private function logMessage($fname,$maxTries=5,$sleepMicroseconds=200000){
$msg=date("Y-m-d H:i:s T").':'.$this->message."\n";
if($this->detailedMessage!=""){
    if(is_string($this->detailedMessage))$msg.=$this->detailedMessage."\n";
    else $msg.=print_r($this->detailedMessage,true);
    }
foreach(self::$details as $k=>$v)$msg.="$k=$v\n";
$msg.="File=".$this->getFile()."; Line=".$this->getLine()."\n";
$msg.=$this->getTraceAsString()."\n";
Logger::log($fname,$msg,$maxTries,$sleepMicroseconds);
}

/**
* Convenience function for adding a bunch of entries to the $details array (and
* not complaining if they are not in $d). It can be called multiple times.
*
* Example usage:
*    Exception::addToDetails(array('REMOTE_ADDR','HTTP_USER_AGENT',
*        'REQUEST_METHOD','REQUEST_TIME','PHP_AUTH_USER','HTTPS','REQUEST_URI'),$_SERVER); 
*
* @param Mixed $keys An array of keys to take from $d and copy to self::$details.
*         It quietly ignores keys that are not found in $d. (Values that exist but
*         are blank strings are copied, however.)
*         $keys can also be a single string, if there is only one key of interest.
* @param Mixed $d Can be array or object. E.g. $_SERVER is typical.
*        If an object then it is simply cast to an array. (Only public vars will be available.)
* 
*/
public static function addToDetails($keys,$d){
if(is_object($d))$d=(array)$d;  //Allow $d to be an object
if(!is_array($keys))$keys=array($keys); //So it will work when $keys is a string
if(is_array($d)){
    foreach($keys as $k){
        if(array_key_exists($k,$d))self::$details[$k]=$d[$k];
        }
    }
}

/**
* Does translations of exception messages (before inserting parameters).
*
* @see self::$strings
*/
static protected function translate($s){
if(!self::$strings)return $s;
if(!array_key_exists($s,self::$strings))return $s;
return self::$strings[$s];
}

}

//=========================================
/**
* This is the main exception that is used for anything triggered by user input.
*
* So, this exception is thrown if the user requests a data feed that does not exist,
* or that they don't have permission for. (These mistakes are usually user error, but
* could also indicate some system problem, for instance a feed has been deleted by
* mistake, or a permission class has got removed from the user by mistake.)
*
* System admin should look at the log hourly or daily, just in case it indicates a
* more serious problem.
*/
class ErrorException extends Exception{}


//=========================================

/**
* This is for exceptions where it can only happen due to some system problem.
*
* Users are shown a "System Error, try again later" message, and system admin
* should be alerted by email (perhaps rotating and emailling a logfile once a minute
* just to avoid sending too much email).
*
* NOTE: PDOExceptions should also be treated the same way as SystemErrorException (if
* they can be triggered by user input then they should be caught and converted to
* APIErrorExceptions instead)
*
* NOTE: The message when creating the exception is only logged - it is never shown
* to the user.
*/
class SystemException extends Exception {}


/**
* Specially for when we want to trigger a basic auth login box
*
* @internal We only extend Exception so we can use encodeAndOutputMessage(). We
*   don't use anything else at all. (Well, except Application::$format of course.)
*/
class MustAuthenticateException extends Exception {
/**
* This is the realm shown in the WWW-Authenticate header. In a browser it
* will appear in the dialog box that is shown to the user.
*
* Note: it will be encoded with addslashes(), but be aware of poor browser compatibility
* with exotic strings. It is best to stick to alphanumeric plus space.
*/
static public $realm='realm';


/** */
public function __construct(){
parent::__construct("");
}

/**
*/
public function requestBasicAuthAndExit(){
header('WWW-Authenticate: Basic realm="'.addslashes(self::$realm).'"');
header('HTTP/1.0 401 Unauthorized');
self::encodeAndOutputMessage(self::translate("Please authenticate."));
exit;
}

}


/**
*/
class RedirectException extends Exception {
/**
* The prefix when $url starts with "/" (or is blank). Typically this
* is the domain name of the site. E.g. "http://example.com/" but it
* might also be a sub-section of a site, e.g. "http://example.com/user/site/";
*
* NOTE: this must always be set if your code might throw this exception
* without full URLs. However if exceptions will always be thrown with full
* URLs starting with "http://" (or whatever) then there is no need to set this.
*/
static public $urlPrefix;

/**
* This is the fully qualified URL. Set by constructor.
*/
private $url;


/** */
public function __construct($url=''){
parent::__construct("");    //No error message to store

if($url=='')$url='/';
if($url{0}=='/'){
    if(!self::$urlPrefix)throw new SystemException('RedirectException::urlPrefix not set, but given a url of'.$url);
    if(substr(self::$urlPrefix,-1)=='/')$url = self::$urlPrefix . substr($url,1);
    else $url = self::$urlPrefix;
    }

$this->url = $url;

$this->message="URL:$url";  //This is for unit tests, more than anything else.
}

/**
* @todo Should look to see if headers already sent. If so, throw a SystemException instead.
*/
public function performRedirectAndExit(){
header('Location: '.$this->url);
exit;
}

}

?>