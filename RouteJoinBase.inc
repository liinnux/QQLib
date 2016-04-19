<?php

namespace QQAPI;

include_once "RouteBase.inc";

/**
* This does most of the work for a route that handles a new user sign-up.
*
* It takes form inputs in $POST.
*
* It assumes main.html.inc exists, and is a full HTML page, which works with a variable called $signupForm,
* which is either 1 or 2. If you want to use a different scheme, override showSignupForm() in your
* derived class.
*
* It deals with two databases:
*   signups: Contains the "signups" table (see docs in QQAPI's DBSignups.inc)
*   users: Contains the "users" table (see below)
* It is fine for these two tables to be in the same database.
* We need read and write access to both tables.
*
* If /xxx is your derived route, then /xxx (i.e. no params) will show the sign-up form.
* That form submits to /xxx/em. When you submit that it sends an email, which has
* a link to /xxx/2/abc...  (where abc... is a long code). That will show another form
* where more questions are asked.
* It submits to /xxx/create, which is where the DB entry will be created.
*
* When the form is submitted (i.e. step=="em"), an entry is created in the signup
* table:
*    status is set to "pending"
*    created is set to current time ($this->startTime)
*    email is set from the form input
*    code is created from other fields.
*    origin is any tracking special-offer codes, etc. that were in the initial URL
*    extra is any additional information.
*
* There is nothing in this base class to do with the users DB. That is all handled in the
* derived class (as there are very likely to be lots of application-specific fields to deal with).
*
* @todo Not using origin/extra fields yet.
*
* @internal This does not derive from BasicRoute as we have no logged-in user, by definition.
*    Also, there is only parameter common to all routes, so not much saving there, and it is
*   simpler just to write our own handle().
*     (Similarly RouteWithList would not be useful as we do lots of different things, but passing
*      data back to Ajax calls is not really a part of any of them.)
*
* @todo We assume securimage.php has been included. I wondered if we should keep a copy in this library?
*/
abstract class RouteJoinBase extends RouteBase{
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
if(array_key_exists('loginTime',$_SESSION))throw new ErrorException('User is already logged-in.');

if(count($params)>=1)$step=$params[0];
else $step = Application::getString('step','');
if(!in_array($step,array('','em','2','create','captcha')))throw new ErrorException('Bad step.',array(),"step={$step}");

if($step=='captcha'){
    $this->showCaptchaImage();
    return;
    }

if($step=='em'){
    $email = Application::getString('email');
    $this->checkCaptcha();  //Will throw if bad. If it returns it is okay.
    $this->initDB();
    $this->processEmStep($email);   //throws ErrorException if problem
    if(substr(Application::$format,0,4)=='json'){
        echo json_encode(array('emailSent'=>true))."\n";
        }
    else{	//Actually, assume we never reach here!
        echo "Email sent. Please check your inbox!";
        }
    return;
    }

if($step=='2'){
    if(count($params)>=2)$code=$params[1];
    else $code = Application::getString('code');
    $this->initDB();
    $row = $this->DBsignups->processSignupCode($code,$this->startTime);  //Throws if any problem. Throws
        //RedirectException if already processed.

    if(!isset($_SESSION))session_start();
    $_SESSION['signup_row']=$row;
    $this->showSignupForm("signup");    //Show a form, with email filled in, and ask for password.
    return;
    }

if($step=='create'){
    $msg="Invalid or expired signup. Please follow the link in the email again, or start the signup process again.";    //Defined once, so all failure messages will be the same (deliberately).
    if(!array_key_exists('signup_row',$_SESSION))throw new ErrorException($msg,array(),"signup_row not found in SESSION");
    $srow = $_SESSION['signup_row'];
    if( !array_key_exists('email',$srow) || !array_key_exists('code',$srow) )throw new SystemException("signup_row found in session, but missing key fields. Should be impossible? Here is full contents of session:".print_r($_SESSION,true));

    $email = $srow['email'];
    $code = $srow['code'];

    if($email != Application::getString('email','') ||
       $code != Application::getString('code'))throw new ErrorException($msg,array(),"email/code from form don't match that in session. Here is all post data:".print_r(@Application::$POST,true)."And here is srow:".print_r($srow,true));

    $this->initDB();
    $row = $this->DBsignups->getProcessingEntry($email,$code);

    switch($row['status']){
        case 'processing':break; //Expected.
        case 'processed':throw new RedirectException();    //Already signed up. Go to top page of site, so they can login.
        default:    //E.g. 'bademail', 'deleted'. Should be impossible.
            throw new SystemException("Found row for code=$code, but status is wrong. Here is full row:".print_r($row,true));
        }

    //Now check passwords been given
    $pw1 = Application::getString('password');
    $pw2 = Application::getString('password2');
    $this->validatePassword($pw1); //Throws if a problem
    if($pw1 != $pw2)throw new ErrorException("Passwords do not match. Reinput both.",array(),"Here is all post data:".print_r(@Application::$POST,true));   //Note: should never happen if front-end validation is working!

    //Custom DB creation (in derived class)
    $this->customCreation($email,$pw1);

    //Update the signups table
    $this->DBsignups->changeStatus($row,$this->startTime,'processing','processed');

    //Set up the session so that the user is logged in.
    $_SESSION['loginTime'] = $this->startTime;

    //And we are done!
    if(substr(Application::$format,0,4)=='json'){
        echo json_encode(array('created'=>true))."\n";  //Now the client needs to do an Init request
        }
    else{   //If not using json, then redirect to top page
        throw new RedirectException();
        }
    return;
    }

//If we reach here, step is "" (or not given, or some unexpected value)
$this->showSignupForm("new");
}

/**
*/
private function processEmStep($email){
$this->validateEmail($email);   //Throws ErrorException if problem
//Add it to the database (this line throws for various reasons, including people
//   trying to request a code too quickly after a previous request, or already being
//   in the database).
$code = $this->DBsignups->addOrRefreshRequest('signup', $email, $this->startTime);
$this->sendSignupEmail($email, $code);
}

//----------------------------------------------------------
//--- Functions to override --------------------------------
//  (if abstract they must be defined by the child class; if not then it is optional)


/**
* @param String $signupForm "new" (for first form that asks email and captcha), or "signup" (asks all the other
*       sign-up questions).
*       If "signup" then $_SESSION['signup_row'] will contain the DB row.
*
* @internal The format check is because it never makes sense to call over ajax. The first form is
*       is standalone, and the second is also an html, though (coming from a URL in an email).
*/
protected function showSignupForm($signupForm){
if(Application::$format!='html')throw new ErrorException('Must use an HTML form.',array(),"Format is ".Application::$format.", but must be html");
include("main.html.inc");
}

/**
* Override this to add the user to the main user database (and any other custom steps).
*/
abstract protected function customCreation($email,$pw);

/** */
abstract protected function sendSignupEmail($email, $code);

/** 
* This function has to set $this->DBSignups (and possibly do $this->DBsignups->createTable(); if
* doing that on-demand, rather than with an installation step).
*
* NOTE: no return value.
*
* It is called from handle() just before the DB is actually needed. This should be a minor
* saving in server load, as it means the DB connection need not be done before inputs have
* been validated.
*/
abstract protected function initDB();


}
