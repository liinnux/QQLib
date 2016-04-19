<?php

namespace QQAPI;

include_once "application.inc";
include_once "exception.inc";
include_once "RouteBase.inc";

/**
* This does most of the work for a route that handles processing a forgotten password.
*
* It takes form inputs in $POST.
*
* It assumes main.html.inc exists, and is a full HTML page, which works with a variable called $mode;
* If you want to use a different scheme, override showForm() in your derived class.
*
* It deals with two databases:
*   signups: Contains the "signups" table (see docs in QQAPI's DBSignups.inc)
*   users: Contains the "users" table. We expect to be able to do an 'email' lookup, and to be
*       able to set the password field.
* It is fine for these two tables to be in the same database.
* We need read and write access to both tables.
*
* When the first form is submitted (i.e. step=="em"), an entry is created in the signup table:
*    status is set to "pwpending"
*    created is set to current time ($this->startTime)
*    email is set from the form input
*    code is created from other fields.
*    origin is any tracking special-offer codes, etc. that were in the initial URL
*    extra is any additional information.
* 
* Note: in class Authenticator terms, canCache is assumed. I.e. a 'user' entry is placed in
* the session (during the 'update' stage), rather than 'username' and 'hashpw'. (No technical
* reason, so support for alternative approaches could not be added later.)
*
* @todo IMPORTANT In FlexDesk, we want to make sure the user is not logged in. If they are
*    we'd never process the forgotpw route. Instead send them to the top page.
*   ---> Or can that be generic enough that we can do it in this route? I.e. just look for something in the session?
*
* @todo Not using origin/extra fields yet.
*
* @todo We assume securimage.php has been included. I wondered if we should keep a copy in this library?
* 
* @internal I considered creating the full "logged-in" session when processing '2', rather than when
*   processing 'update'. This might have been an advantage if there are background ajax processes
*   that expect the user to be logged on.
*   But there is also a potential security threat, though quite subtle: an attack who had temporary
*   control of a victim's email, could use the URL as a one-off log-in token. They could then make some change
*   with that login, or just view private information. Because they didn't change the password, the
*   breach can go completely unnoticed. I.e. by forcing the user to set a new password before they
*   can view any data, or change anything. As (by implication) they didn't know the users password,
*   they have to change it, and it can therefore no longer be discreet.
* 
* @internal I don't like that we use findUser() in authenticator.inc, but don't use it here, instead
*    using initDB() to set $this->DBusers. But, perhaps, by design, this route is more restrictive
*    than class Authenticator, and the extra flexibility of findUser() is never needed?
*    (If we did use findUser(), we could set canCache to control that behaviour. But, again, is
*    there any use case for it?)
*/
abstract class RouteForgotpwBase extends RouteBase{
/**
* Unix time (as float, to microsecond accuracy) when constructor was called
*
* @see RouteWithList for design decisions
*/
public $startTime;

/** Required by class Routing */
public $needUser = false;

/** An instance of DBSignups, assigned by initDB() */
protected $DBsignups;

/** An instance of the user DB, assigned by initDB() */
protected $DBusers;

//--------------------------------

/**
*/
function __construct(){
//parent::__construct();
$this->startTime = microtime(true);
}


/**
*/
public function handle($params){
if(count($params)>=1)$step=$params[0];
else $step = Application::getString('step','');
if(!in_array($step,array('','em','2','update','captcha')))throw new ErrorException('Bad step.',array(),"step={$step}");

if($step=='captcha'){
    $this->showCaptchaImage();
    return;
    }

if($step=='em'){
    $this->processEmStep();   //throws ErrorException if problem
    return;
    }

if($step=='2'){
    if(count($params)>=2)$code=$params[1];
    else $code = Application::getString('code');
    $this->initDB();
    $row = $this->DBsignups->processForgotpwCode($code,$this->startTime);  //Throws if any problem. Throws
        //RedirectException if already processed.

    if(!isset($_SESSION))session_start();
    $_SESSION['changepw_row']=$row;
    $this->showForm("newpw");    //Show a form, with email filled in, and code in a hidden, and ask for password
    return;
    }

if($step=='update'){
    $msg="Invalid or expired change password action. Please follow the link in the email again, or start the change password process again.";    //Defined once, so all failure messages will be the same (deliberately).
    if(!array_key_exists('changepw_row',$_SESSION))throw new ErrorException($msg,array(),"changepw_row not found in SESSION");
    $srow = $_SESSION['changepw_row'];
    if( !array_key_exists('email',$srow) || !array_key_exists('code',$srow) )throw new SystemException("changepw_row found in session, but missing key fields. Should be impossible? Here is full contents of session:".print_r($_SESSION,true));

    $email = $srow['email'];
    $code = $srow['code'];

    if($email != Application::getString('email','') ||
       $code != Application::getString('code'))throw new ErrorException($msg,array(),"email/code from form don't match that in session. Here is all post data:".print_r(@Application::$POST,true)."And here is srow:".print_r($srow,true));

    $this->initDB();
    $row = $this->DBsignups->getProcessingEntry($email,$code);

    switch($row['status']){
        case 'pwprocessing':break; //Expected.
        case 'pwprocessed':throw new RedirectException();    //Already signed up. Go to top page of site, so they can login.
        default:    //E.g. 'bademail', 'deleted'. Should be impossible.
            throw new SystemException("Found row for code=$code, but status is wrong. Here is full row:".print_r($row,true));
        }

    //Now check passwords been given
    $pw1 = Application::getString('password');
    $pw2 = Application::getString('password2');
    $this->validatePassword($pw1); //Throws if a problem
    if($pw1 != $pw2)throw new ErrorException("Passwords do not match. Reinput both.",array(),"Here is all post data:".print_r(@Application::$POST,true));   //Note: should never happen if front-end validation is working!
    
    //Update the users table
    $this->DBusers->setUserPassword($email, $pw1);
    
    $user = $this->DBusers->findUserByEmail($email);
    //TODO: I suppose we could validate password got set correctly. No real need, that would
    //   indicate serious system problems, so unlikely to already have reached this point.

    //Update the signups table
    $this->DBsignups->changeStatus($row, $this->startTime, 'pwprocessing', 'pwprocessed');

    //Set up the session so that the user is logged in.
    $_SESSION['loginTime'] = $this->startTime;
    $_SESSION['user'] = $user;

    //And we are done!
    if(substr(Application::$format,0,4)=='json'){
        echo json_encode(array('updated'=>true))."\n";
        }
    else{   //If not using json, then redirect to top page
        throw new RedirectException();
        }
    return;
    }

//If we reach here, step is "" (or not given, or some unexpected value)
$this->showForm("forgot");
}

/**
*
*/
private function processEmStep(){
//TODO: do a sleep(5) here to prevent abuse (i.e. trying to get a list of email addresses that exist)
    ///---> Control that with a static var? E.g. we don't really want it with unit tests.

$email = Application::getString('email');
$this->checkCaptcha();  //Will throw if bad. If it returns it is okay.

$this->validateEmail($email);   //Throws ErrorException if problem
$this->initDB();
$row = $this->DBusers->findUserByEmail($email);
if($row === false)throw new ErrorException('Unknown email(%1$s)',array($email));

$code = $this->DBsignups->addOrRefreshRequest('forgotpw', $email, $this->startTime);
$this->sendForgotpwEmail($email, $code);

if(substr(Application::$format,0,4)=='json'){
    echo json_encode(array('emailSent'=>true))."\n";
    }
else{	//Actually, assume we never reach here!
    echo "Email sent. Please check your inbox!";
    }
}

//----------------------------------------------------------
//--- Functions to override --------------------------------
//  (if abstract they must be defined by the child class; if not then it is optional)


/**
* Called when we want to show either of the forms
* 
* @param String $mode "forgot" for the initial form where email and captcha is asked,
*      "newpw" for the second form where the new password is asked.
*     NOTE: for "newpw", it can get information, such as email, from $_SESSION['changepw_row'].
*        Also note that newpw form must submit email and code as hiddens.
*
* @internal The format check is because it never makes sense to call over ajax. The first form is
*       is standalone, and the second is also an html (though coming from a URL in an email).
*/
protected function showForm($mode){
if(Application::$format!='html')throw new ErrorException('Must use an HTML url.',array(),"Format is ".Application::$format.", but must be html");
include("main.html.inc");
}

/** */
abstract protected function sendForgotpwEmail($email, $code);

/** 
* This function has to set $this->DBSignups (should never be a need to call $this->DBsignups->createTable()
*   though, as the implication is we have an existing user here who has forgotten their pw)
* and it has to set $this->DBUsers.
*
* NOTE: no return value.
*
* It is called from handle() just before the DB is actually needed. This should be a minor
* saving in server load, as it means the DB connection need not be done before inputs have
* been validated.
*/
abstract protected function initDB();


}
