<?php

namespace QQAPI;

include_once "logger.inc";

/**
* The IPN handler comes back to notify you about a transaction. It has already happened by this point,
* so you cannot reject it; all you can do is validate with PayPal that it is correct, which is what this
* function does.
*
* Note: to use this you need to set notify_url in your HTML form (the data that is posted to PayPal
* with the order). Alternatively you can set it in your paypal profile.
*
* Remember this callback can come back much later (usually it is instant though), so you
* cannot rely on any session data.
*
* You have to send back a blank message to PayPal. You should not spend more than 20 seconds
* processing the IPN message.
*
* Bear in mind that you may get the IPN notification twice. So being prepared for double notifications
* is important. @see DBPayments::insertRowForPayPalIPN() where this is handled by just trying to
* insert, and if it fails due to a duplicate, then it returns false, so you know not to process it any further.
 * 
 * @internal Recommended IPN validation steps:
 *   1. If using custom, validate that.
 *   2. Check "business" matches the expected shop email address.
 *   3. If not sending a physical product, expect "shipping" and "handling" to be zero.
 *   4. If only a single item, not a multiple-purchases-in-your-shopping-cart kind of site,
 *      then expect only a single entry. I.e. mc_gross == mc_gross1.
 *   5. Expect mc_currency to match your support currencies.
*/
class PayPalHelper{
/**
* If using the log() functions then this is where the log file is found,
* and the {{TYPE}} part gets replaced with the log type. If another
* {{...}} string is found it gets treated as a date() format string. Multiple entries
* are possible, so you can do "paypal.{{Ymd}}.{{TYPE}}.{{H}}.log".
*/
static public $logTemplate = "logs/paypal.{{TYPE}}.{{Ymd_H}}.log";

/** Just for unit tests (i.e. when $logTemplate === null) */
static public $logData = array();

/**
* Gets set by getAndValidateInput(), and contains all the data PayPal sent by IDN.
*/
public $data = array();

/**
* Gets set by getAndValidateInput(), and will contain either "VERIFIED" or "INVALID"
*
* You are unlikely to need this, unless ignoring the return value of getAndValidateInput().
*/
public $verificationResult = "";

//------------------------------------------------

/**
* Do the response to PayPal, and find out if this is valid request.
*
* It uses test_ipn, in the input, to decide if we are talking to the real server
* or the sandbox. Obviously the application validation should afterwards check
* this was the expected result.
*
* @return bool True if "VERIFIED" was returned by PayPal. False means "INVALID"
*   returned, while an exception is possible if some connection error.
*   You can also find the return value from PayPal in $this->verificationResult.
*
* @internal
*  At recommendation of PayPal, we used the code found here as a base:
*    https://gist.github.com/xcommerce-gists/3440401#file-completelistener-php
*  However the code to do with magic quotes handling is removed (never there as of php 5.4.x,
*  but should've been off in all servers for years before that).
*
*  As one of the comments suggested it might be reducible to this:
    $postdata = 'cmd=_notify-validate&';
    $postdata.= file_get_contents('php://input');

    $URL = "https://www.paypal.com/cgi-bin/webscr";
    $opts = array('http' => array(
        'method' => 'POST',
        'header' => 'Content-type: application/x-www-form-urlencoded',
        'content' => $postdata)
        );
    $context = stream_context_create($opts);
    $result = file_get_contents($URL, false, $context);

* See http://stackoverflow.com/q/14008067/841830 for discussion of why it does not use $_POST,
* and instead processes php://input itself.
*/
public function getAndValidateInput(){
$this->getInputData();
return $this->confirmWithPayPalServer();
}


/**
* This logs to self::$logTemplate, a multi-line message, that includes all the data PayPal sent.
*
* Remember that this contains end-user private data (such as name, address and email address,
* as well as listing what they are purchasing), so these logs should be kept as private as possible.
* Consider emailing their existence to administrators, but not actually emailing the data (or using
* encrypted emails).
*
* It uses Logger::log(), which means it might also write to the emergency logs if the main
* log destination has a fatal problem.
*
* NOTE: generally the "invalid" and "error" logs can be expected to be empty and so need 
*   to be checked regularly (e.g. emailed to the administrator at least hourly). The "bad_IPN" and
*   "duplicate" logs also ought to be blank, but only need to be checked if a suspiciously large number
*   of entries, or for suspiciously regular entries. The "success" log is just there for troubleshooting,
*   as (at least if using DBPayments) all the information should already be in your DB.
*
* @param $type
*       "bad_IPN": paypal rejected the data
*       "invalid": failed our own validation (e.g. "custom" data was bad, required addrews field was missing, etc.)
*       "duplicate": Detected as a transaction ID that has already been processed.
*       "error": This is for system errors, such as a DB write failed.
*       "success": all worked
*    Note: this is not validated, so you can introduce new names.
* @param $msg This is for custom information about the problem. It is optional.
*/
public function log($type, $msg=""){
$s = date("Y-m-d H:i:s:").$msg."\n";
foreach($this->data as $k=>$v)$s.=$k.":".print_r($v,true)."\n";

if(self::$logTemplate === null){    //For unit tests, to save touching the file system
    if(!self::$logData)self::$logData=array();
    if(!array_key_exists($type, self::$logData))self::$logData[$type]=$msg;
    else self::$logData[$type].=$msg;
    }
else{
    $fname = str_replace('{{TYPE}}', $type, self::$logTemplate);
    $fname = preg_replace_callback('/{{(.+?)}}/', function($parts){
        return date($parts[1]);
        }, $fname);
    Logger::log($fname, $s);
    }
}


//-------------------

/**
* Intializes $this->data from php://input
*/
private function getInputData(){
//Get post data, and initialize $this->data
$raw_post_data = file_get_contents('php://input');
$raw_post_array = explode('&', $raw_post_data);
$this->data = array();
foreach ($raw_post_array as $keyval) {
    $keyval = explode ('=', $keyval);
    if (count($keyval) == 2)$this->data[$keyval[0]] = urldecode($keyval[1]);
    }
}

/**
* Sends back the POST data, but with  'cmd=_notify-validate' included first
*
* It uses CURL.
* 
* As a quick sanity check it assumes at least business and txn_id must  have been given. 
*
* @return bool True if "VERIFIED" was returned by PayPal.
*
* @internal If curl does not come with root certificates, you'd need to supply
*  your own certificate and include this before the curl_exec() call:
*       curl_setopt($ch, CURLOPT_CAINFO, dirname(__FILE__) . '/cacert.pem');
*/
private function confirmWithPayPalServer(){
if(!$this->data || count($this->data)==0)throw new ErrorException("No data given.");
if(!array_key_exists('business',$this->data) && !array_key_exists('txn_id',$this->data)){
    throw new ErrorException("Expected fields in input are missing.");
    }
    
$req = 'cmd=_notify-validate';
foreach ($this->data as $key => $value) {        
    $value = urlencode($value);
    $req .= "&$key=$value";
    }

$url = (array_key_exists('test_ipn',$this->data) && $this->data['test_ipn']) ?
    'https://www.sandbox.paypal.com/cgi-bin/webscr' :
    'https://www.paypal.com/cgi-bin/webscr' ;

$ch = curl_init($url);
curl_setopt($ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
curl_setopt($ch, CURLOPT_POST, 1);
curl_setopt($ch, CURLOPT_RETURNTRANSFER,1);
curl_setopt($ch, CURLOPT_POSTFIELDS, $req);
curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, 1);
curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, 2);
curl_setopt($ch, CURLOPT_FORBID_REUSE, 1);
curl_setopt($ch, CURLOPT_HTTPHEADER, array('Connection: Close'));

if( !($res = curl_exec($ch)) ) {
    $msg = "Failed to send IPN data to paypal:".curl_error($ch);
    curl_close($ch);
    throw new SystemException($msg);
    }

curl_close($ch);
$this->verificationResult = $res; //$res is either "VERIFIED" or "INVALID"
return ($res == "VERIFIED");
}

}
