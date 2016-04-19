<?php

namespace QQAPI;

include_once "application.inc";
include_once "exception.inc";
include_once "RouteBase.inc";

/**
* This does most of the work for a route that handles changing a member email address
*
* The point of this is that we want to be sure they have given a valid email address, that
* belongs to them, and not mis-typed it. 
* We assume the user is already logged in, so do not need to show them a captcha (but there
* is an option to enable this; in that case you need to include securimage.php, or your equivalent
* captcha library).
* There is the option to ask them to give their password again, though (to avoid someone
* hijacking an account, when an account has been left logged in on a public computer, etc.)
*
* It takes form inputs in $POST.
*
* NOTE: unlike signup and forgotpw, the /2/ stage is the final one, the step that updates the DB.
*
*
* It assumes main.html.inc exists, and is a full HTML page, which works with a variable called $mode;
* If you want to use a different scheme, override showForm() in your derived class.
* NOTE: as the user is already logged, you do not need to use the $step='', and therefore do not need to
* to define main.html.inc nor override showForm(). Just have an existing form on the site submit to /em/
* with email1 set to the old email address, and email2 set to the new email address.
* (However you may want to override to show the confirmation - default behaviour is to redirect
*  to the default location, which may not be helpful.)
*
* It deals with two databases:
*   signups: Contains the "signups" table (see docs in QQAPI's DBSignups.inc)
*   users: Contains the "users" table. We expect to be able to do an 'email' lookup, and to be
*       able to set the email field, and optionally to fetch the hashed password.
* It is fine for these two tables to be in the same database.
* We need read and write access to both tables.
*
* When the form is submitted (i.e. step=="em"), an entry is created in the signup table:
*    status is set to "pending"
*    created is set to current time ($this->startTime)
*    email is set from the form input
*    code is created from other fields.
*    origin is any tracking special-offer codes, etc. that were in the initial URL
*    extra is any additional information.
*
* @todo How to confirm they are validly logged in at the moment? Is that our job, or the job
*   of the derived class.
*
* @todo Not using origin/extra fields yet.
*/
abstract class RouteChangeEmailBase extends RouteBase{
/**
* Unix time (as float, to microsecond accuracy) when constructor was called
*
* @see RouteWithList for design decisions
*/
public $startTime;

/** Required by class Routing */
public $needUser = true;

/** An instance of DBSignups, assigned by initDB() */
protected $DBsignups;

/** An instance of the user DB, assigned by initDB() */
protected $DBusers;

/**
* If setting this to true then the following changes happen:
*       a $step of 'captcha' is processed (otherwise it would be rejected with an exception)
*       checkCaptcha() and showCaptchaImage() will be called (these are both in the base class, and
*           if using securimage.php then there is nothing extra to do)
*/
protected $useCaptcha = false;

//--------------------------------

/**
*/
function __construct(){
//parent::__construct();
$this->startTime = microtime(true);
}


/**
*
*
* @internal I considered finding a way to validate the old email address. E.g. requiring $this->initDB() to return
*   the current entry. However, initDB() validating they are logged in should be sufficient.
*/
public function handle($params){
if(count($params)>=1)$step=$params[0];
else $step = Application::getString('step','');
$validSteps = array('','em','2');
if($this->useCaptcha)$validSteps[]='captcha';
if(!in_array($step,$validSteps))throw new ErrorException('Bad step.',array(),"step={$step}");

if($step=='captcha'){
    assert($this->useCaptcha);  //I.e. implied by the $validSteps check above
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
    $row = $this->DBsignups->processChangeEmailCode($code,$this->startTime);  //Throws if any problem. Throws
        //RedirectException if already processed.

    //NB. we assume the initDB() call confirmed they are logged in and valid. (We have no information from
    //   the URL other than the code.)
    $msg="Invalid or expired change email action. Please start the change email process again.";    //Defined once, so all failure messages will be the same (deliberately).

    $oldEmail = $row['origin']; //The old email address was stored in the origin column of the signups table.
    $newEmail = $row['email'];  //This is where we just sent the email to. They clicked the URL, so we know
        //it arrived and that the email address is valid.

    //Update the users table
    $this->DBusers->setUserEmail($oldEmail, $newEmail);
    
    $user = $this->DBusers->findUserByEmail($newEmail);
    $_SESSION['user'] = $user;

    //And we are done!
    if(substr(Application::$format,0,4)=='json'){
        echo json_encode(array('updated'=>true))."\n";
        }
    else{   //If not using json, then redirect to top page
        $this->showForm("changed");
        }
    return;
    }

//If we reach here, step is "" (or not given, or some unexpected value)
$this->showForm("changeemail");
}

/**
* This is called when they've told us the new email address they want to use.
*
* There are two form inputs expected: current email address (email1) and new email
* address (email2).
*
* @internal I wondered about a sleep(5) option here, to prevent email harvesting, or other
*   kinds of abuse. But, because they need to be logged in here, rather than anonymous, there
*   is less potential (though still some potential, so consider it in future).
*/
private function processEmStep(){
$currentEmail = Application::getString('email1');
$newEmail = Application::getString('email2');
if($this->useCaptcha)$this->checkCaptcha();  //Will throw if bad. If it returns it is okay.

if($currentEmail == $newEmail)throw new ErrorException('Old and new email are the same.');

$this->validateEmail($currentEmail);   //Throws ErrorException if problem
$this->validateEmail($newEmail);   //Throws ErrorException if problem

$this->initDB();
$row = $this->DBusers->findUserByEmail($currentEmail);
if($row === false)throw new ErrorException('Unknown email(%1$s)',array($currentEmail));

$row2 = $this->DBusers->findUserByEmail($newEmail);
if($row2 !== false)throw new ErrorException('Given email is already in use. Contact support if this should not be the case.',array($newEmail),"User tried to change their address to {$newEmail} but it is already in use. Their current DB row is:".print_r($row,true)."; the DB row for the new email address they wanted is:".print_r($row2,true));  //We are safe embedding email address, and data, here, as both addresses have been validated, and both sets of data are already in the DB (so presumably have received a certain amount of validation).

$code = $this->DBsignups->addOrRefreshRequest('changeemail', $newEmail, $this->startTime, array(
    'origin' => $currentEmail
    ) );
$this->sendChangeEmailEmail($newEmail, $code);

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
* @param String $mode "changeemail" This is the initial form. The later stages do not call
*     this function,they just communicate over Ajax. However it is called with "changed"
*     when the DB has been successfully updated.
*/
protected function showForm($mode){
if($mode == "changed")throw new RedirectException();
if(Application::$format!='html')throw new ErrorException('Must use an HTML url.',array(),"Format is ".Application::$format.", but must be html");
include("main.html.inc");
}

/** */
abstract protected function sendChangeEmailEmail($newEmail, $code);

/** 
* This function has to set $this->DBSignups (should never be a need to call $this->DBsignups->createTable()
*   though, as the implication is we have an existing user here) and it has to set $this->DBUsers.
*
* NOTE: no return value.
*
* NOTE: as there is the assumption that the current user is logged in, and valid, this function call
* would be a good place to check that, and throw an exception if it is not the case.
*
* It is called from handle() just before the DB is actually needed. This should be a minor
* saving in server load, as it means the DB connection need not be done before inputs have
* been validated.
*/
abstract protected function initDB();


}
