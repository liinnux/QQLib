<?php
namespace QQAPI;

/**
* This is the default implementation of the authenticator interface
* the Routing class expects.
*
* NOTE: If using multiple apps on the same domain (or just for some
* obscurity) you should call session_name() before the first call to
* session_start(). (If using session.auto_start=true in php.ini,
* then you will also need to put this in each .htaccess file: 
*      php_value session.name myNewName
* Stick to alphanumeric for the session name (avoid periods, but underlines are okay).
*/
class Authenticator{
/**
* Set to the user object.
*
* Returned by getUser(), set on-demand (i.e. the first time getUser()
* is called). If an auth problem then an exception is thrown, so to get
* authentication failures happening at a predictable time (e.g. before
* doing any notable processing effort), call getUser() once, in advance.
*
* Must be an array or an object.
*/
private $user=null;

//-- Following variables set by constructor and used by process().
// Some exist mainly for unit testing, as the defaults are normally what is desired.

/**
* Callback to get the user. It will be given username and password,
* and should return some data (array or object) to assign to $this->user. If invalid
* it should throw an exception, or return null.
* 
* (Currently, returning null will cause an ErrorException telling them to check
* their login credentials. You might instead want to be throwing MustAuthenticateException or
*  RedirectException)
*
* The parameters it is given are:
*    $username
*    $password
*    $hashpw
* Normally one of $password and $hashpw is null, and the other is set. If both
* are null it implies automatic login is allowed.
* If you give both, then $password is used, and $hashpw is assigned to.
* $hashpw is allowed for when the hash can be pre-calculated, or is being stored
* in the session, etc.
*/
private $findUser;

/**
 * A callback for when a logout is done.
 * 
 * If using DBUsers.inc, or a class derived from that, this is where you would
 * call recordLogout().
 * 
 * @see onLogout()  (which is called by Routing's default implementation of logout route).
 * @see onLogoutRecordLogout() for example usage (which will be fine for most applications).
 */
private $onLogoutCallback = null;

/**
* How to authenticate.
*
* 'session' or 'basic'.
*
* @see allowCommandline, which in combination with a call to useCommandline()
*   will override this.
*/
private $type;

/**
* @type boolean
*/
private $allowCommandline=false;

/**
* Either this or loginHTML needs to be set for type==session
*
* If this is set it is output with include(), so it can contain any combination
* of HTML and PHP code.
*/
private $loginPHP=null;

/**
* Either this or loginPHP needs to be set for type==session
*
* It is output as-is, no processing of contents.
* Use this when someone not fully trusted might be updating the login html file (otherwise
* the flexibility of $loginPHP will be better).
*/
private $loginHtml=null;

/** Defaults to $_SERVER */
private $SERVER=null;

/**
 * Defaults to $_SESSION 
 *
 * This exists to allow unit-testing to change it (through the 'SESSION' key in the $options
 * given to the constructor).
 * 
 * Note: findUserHelperUsingSessionsRecordLogin() will reset this. So you need to re-assign
 * after using that function.
 * 
 * @internal Switched from private to public, as then it can be re-assigned, and no real
 *   need for it to have been private.
 */
public $SESSION=null;

/** 
* The username to login as.
*
* Set by useCommandline(), and can only be set if allowCommandline was
* set to true in the options.
*/
private $commandlineUser=null;

//----------------------------------------------

/**
*/
public function __construct($options){
$this->findUser=$options['findUser'];   //TODO: assert it is a function
$this->type=$options['type'];   //TODO: assert it is "basic" or "session"
if(array_key_exists('onLogout',$options))$this->onLogoutCallback=$options['onLogout'];
if(array_key_exists('allowCommandline',$options))$this->allowCommandline=$options['allowCommandline'];
if(array_key_exists('loginPHP',$options))$this->loginPHP=$options['loginPHP'];
if(array_key_exists('loginHtml',$options))$this->loginHtml=$options['loginHtml'];
if(array_key_exists('SERVER',$options))$this->SERVER=$options['SERVER'];
if(array_key_exists('SESSION',$options))$this->SESSION=$options['SESSION'];
}


/**
* @return Mixed The user object
*/
public function getUser(){
if(!$this->user)$this->process();   //Either sets $this->user, or throws.
return $this->user;
}


/**
* Behaviour depends on $this->type, and this kind of a cut-down version of getUser()
*
* @return Bool 
*/
public function isLoggedIn(){
if($this->commandlineUser)return true;  //Commandline user is always logged-in
if($this->type=='session'){
    //NOTE: we assume session_start() already called.
    //NOTE: I'm assuming session also implies a custom login scheme, not using basic auth, oAuth, etc.
    if($this->SESSION===null){
        if(!isset($_SESSION))throw new SystemException('No _SESSION superglobal. Make sure session_start() has been called.');
        $this->SESSION=&$_SESSION;  //The "&" is essential, otherwise when we set entries in
            //$this->SESSION, they don't get reflected in the real session!
        }
    if(!is_array($this->SESSION))throw new SystemException('Session not set, or incorrect type.');
    if(array_key_exists('loginTime',$this->SESSION)){
        //TODO: ought to also validate 'user' is in there (or both 'username' and 'hashpw')
        return true;
        }
    return false;
    }
elseif($this->type=='basic'){
    if($this->SERVER===null)$this->SERVER=$_SERVER;
    if(!isset($this->SERVER['PHP_AUTH_USER']) || !isset($this->SERVER['PHP_AUTH_PW']))return false;
    //Now to be sure they are logged-in, we need to validate the credentials
    $username=$this->SERVER['PHP_AUTH_USER'];
    $password=$this->SERVER['PHP_AUTH_PW'];
    $this->user=call_user_func($this->findUser,$username,$password);   //Throws on error
        //(Or it can also return null, if the generic message is all that is needed.)
    if($this->user)return true;
    return false;
    }
else throw new SystemException('Unsupported auth type:'.$this->type.'. Should be session or basic, currently.');
}


/**
* Call this if using the commandline.
*
* If allowCommandline has not been set, then it will throw an exception instead.
*
* @param $argv If not given then it will default to $GLOBALS['argv']. (Only really
*     needs to be set explicitly when unit testing, or doing something clever.)
*     NOTE: it assumes the username is in $argv[3].
*
* @todo This could be refactored to be called "usePasswordlessLogin()", and
*    allowCommandline renamed to allowPasswordlessLogin, and the parameter
*    could just be the username!
*/
public function useCommandline($argv){
if(!$this->allowCommandline)throw new SystemException('Commandline usage is disallowed by Authenticator settings. Set allowCommandline in the options to allow it.');
if(!$argv || !array_key_exists(1,$argv))throw new SystemException('Username should be 1st commandline parameter, but not there. Here is argv:'.print_r($argv,true));
$this->commandlineUser=$argv[1];
}


/**
* This is needed for the type==session auth (and maybe others in future).
* It allows them to go on to the page they need, after being authenticated.
*
* @todo Despite the name it is not the URL! Maybe call it $originalRequest, $theRequest, etc.?
 It is a substr of $_SERVER['REDIRECT_URL'],
* or possibly the 'a' in $POST or $GET data. (It also excludes the ".json", etc. extension.)
*/
public function setUrl($url){
$this->originalUrl = $url;
}

/**
 * Called by Routing's default logout route implementation.
 */
public function onLogout(){
if($this->onLogoutCallback)call_user_func($this->onLogoutCallback); 
}


//----------------------------------------------

/**
* Either called at the end of prepare(), or in handle()
*
* So the Route object may or may not have been created by this point.
*
* The code will look in argv if called from commandline, or look
* in PHP_AUTH_USER/PHP_AUTH_PW if using basic auth.
*
* @param String $authType These types currently supported:
*    'main': Get username as first parameter on $argv
* @param Array $argv Where to get commandline data. Will default to $GLOBAL['argv'].
*    (Never needs to be set: just here for unit testing.)
*/
private function process(){
if($this->commandlineUser){
    $this->user=call_user_func($this->findUser,$this->commandlineUser,null,null);   //Throws on error
        //(Or it can also return null, if the generic message is all that is needed.)
        //The two null params means no password is needed.
    }

elseif($this->type=='session'){
    //NOTE: we assume session_start() already called.
    //NOTE: I'm assuming session also implies a custom login scheme, not using basic auth, oAuth, etc.
    if($this->SESSION===null){
        if(!isset($_SESSION))throw new SystemException('No _SESSION superglobal. Make sure session_start() has been called.');
        $this->SESSION=&$_SESSION;  //The "&" is essential, otherwise when we set entries in
            //$this->SESSION, they don't get reflected in the real session!
        }
    if(!is_array($this->SESSION))throw new SystemException('Session not set, or incorrect type.');
    if(!array_key_exists('loginTime',$this->SESSION)){  //Does not have a valid authorized session yet.
        if(array_key_exists('username',Application::$POST)){
            $this->processSessionLogin(Application::$POST['username'], @Application::$POST['password']);
        }elseif(array_key_exists('token',$_GET)){
            //As we often didn't come from login form we need to initialize the session
            $this->SESSION['originalUrl']=$this->originalUrl;
            $this->SESSION['originalPOST']=array();
            $this->processSessionLogin(null, $_GET['token']);
        }elseif(array_key_exists('token',$_COOKIE)){
            //As we didn't come from login form we need to initialize the session
            $this->SESSION['originalUrl']=$this->originalUrl;
            $this->SESSION['originalPOST']=array();
            $this->processSessionLogin(null, $_COOKIE['token']);
        }else $this->giveLoginFormAndExit();
        }
    else{
        if(array_key_exists('user',$this->SESSION)){    //Easy - we store the whole user object in the session
            $this->user = $this->SESSION['user'];
            }
        else{   //This means we fetch fresh user data on each request
            if(!array_key_exists('username',$this->SESSION) || !array_key_exists('hashpw',$this->SESSION)){
                //Indicates session corruption rather than user error or hack attempt. Close their session
                //   and send to the default page.
                session_destroy();
                throw new RedirectException();
                }
            $this->user = call_user_func($this->findUser,$this->SESSION['username'],null,$this->SESSION['hashpw']); //Throw if error (or it might return null)
            }
        }
    }
elseif($this->type=='basic'){
    if($this->SERVER===null)$this->SERVER=$_SERVER;
    if(isset($this->SERVER['PHP_AUTH_USER']) && isset($this->SERVER['PHP_AUTH_PW'])){
        $username=$this->SERVER['PHP_AUTH_USER'];
        $password=$this->SERVER['PHP_AUTH_PW'];
        }
    else throw new MustAuthenticateException();  //TEMP - implies we want basic auth. But that exception name ought to say that more explicitly.
    $this->user=call_user_func($this->findUser,$username,$password);   //Throws on error
        //(Or it can also return null, if the generic message is all that is needed.)
    }
else throw new SystemException('Unsupported auth type:'.$this->type.'. Should be session or basic, currently.');

if(!$this->user)throw new ErrorException('Please check your authentication details and try again.');

//TODO: if $this->user has a preference for format *AND* $this->format=='', then:
//   $this->setFormat(....);  (PROBLEM: "$this" refers to the Routing class, not here!!!)
//                     BUT, I also don't want to have to run that check each time getUser() is called...
//                  ---> Or maybe it is okay, as getUser is only called in two places, and only
//                      from inside the Routing class.

}



/**
* Helper for process()
*
* Uses $this->SESSION and Application::$POST
* 
* @param string $username The 'username' in $_POST. This will be null for a token login
* @param string $password The 'password' in $_POST (or the token, for a token login)
*
* @todo There is a problem here: if they leave the login form for too long the PHP session
*   expires. Then there are no values. When this happens they get a confusing error message
*   about "Missing data in session. Login again.".
*   Proposed Solution:
*       If the request is HTML, simply show them the HTML form again (with a fresh session,
*           and fresh values for OriginalURL,etc.)
*       If the request is JSON the front-end has to intercept this particular error message
*           and force a browser reload. That would (I think) force a fresh session to be created.
*/
private function processSessionLogin($username, $password){
if(!array_key_exists('originalUrl',$this->SESSION))throw new ErrorException('Missing data in session. Login again.',null,'originalUrlParts not found');
if(!array_key_exists('originalPOST',$this->SESSION))throw new ErrorException('Missing data in session. Login again.',null,'originalPOST not found');

$this->user = call_user_func($this->findUser,$username,$password,null);
if(!$this->user)throw new ErrorException('Please check your authentication details and try again.');

$this->SESSION['loginTime'] = time();    //Only on success
$obj = (array)$this->user;  //In case user is an object, i.e. this just normalizes it to be an array.

if(@$obj['canCache']){  //The returned object must explicitly allow caching
    $this->SESSION['user']=$this->user;
    }
else{
    $this->SESSION['username']=$username;
    $this->SESSION['hashpw']= @$obj['hashpw'];
    if(!$this->SESSION['hashpw'])throw new SystemException('hashpw not in user object');
    }
}


/**
* Called when they don't have a logged-in session yet, and are not sending
* login credentials, so we have to show the login form.
*/
private function giveLoginFormAndExit(){
$POSTVarsToLookFor=array('action'); //TODO: this has to be customizable?
//NOTE: we assume when $this->SESSION refers to $_SESSION that assigning this
//  way will alter global session.
//TODO: ought to refer to $this->SESSION here? But will the actual session get set in that case?
//----> Worry about that when we come to write unit tests. It does not matter until then...
$this->SESSION['originalUrl']=$this->originalUrl;
$this->SESSION['originalPOST']=array();
foreach(Application::$POST as $k=>$v)if(in_array($k,$POSTVarsToLookFor))$this->SESSION['originalPOST'][$k]=$v;

//TODO: if $format is explictly "json", etc. then send a special message
//   --> OPT 1: look for $format in Exception class?
//                --->feels wrong place for it.
//         OPT 2: a static in this class too?
//                ---> Then we have the same data in different places
//         OPT 3: A new global class, where we can put $format, and that we don't mind being an dependency of Exception.
//   else if 'html', or nothing specified, then send an HTML header, and do the following:
//---> Worry about that later. Currently session login will always be using HTML!!!

header("Content-Type: text/html");
if($this->loginPHP!==null)include($this->loginPHP);
elseif($this->loginHTML!==null)echo file_get_contents($this->loginHtml);
else throw new SystemException('Neither loginPHP nor loginHTML has been set, but trying to use session logins.');
exit;   //TODO: unreasonable? Well, throwing an exception would just end up doing an exit anyway.
}

/**
 * @param String $path The path of the cookie to clear. It is essential this exactly matches
 *    how it was set. (E.g. giving "/subdir" when the cookie was "/" means the "/" cookie will
 *    not be cleared, but also vice versa, giving "/" will not clear the "/subdir" cookie.)
 */
public static function clearAnyTokenCookie($path){
if(!array_key_exists('token',$_COOKIE))return;
setcookie('token', '', 1 /*Very old time*/, $path);    //Leave domain/secure/httponly to be defaults
unset($_COOKIE['token']);
}


/**
 * This assumes login ids are email addresses, and produces a suitable login token.
 * 
 * The structure is: VERSION, TTL, period, DOMAIN, two periods, EXTRA INFO, hyphen, HASH, hyphen, USER (part of the email)
 * The "@" symbol will not be present. The two periods are used as the post-domain separator
 * as it is illegal for a domain name to have two in a row.
 * The username part of the email address goes last, as "@" is just about the only disallowed
 * character, so finding a separator would be hard.
 * VERSION is a single character, and always "A" currently.
 * 
 * @param string $salt Extra text used in the hash. Note: if this is a constant string, then an attacker
 *     with this source code and that salt (or even without the salt, as they could brute-force discover it)
 *     could produce login URLs for anyone. So, where practical, per-user salts are recommended, and
 *     ideally, change them on each login (then it becomes a one-time-password).
 * @param string $em The email address.
 * @param string $url The URL where the token will be used. If you give this, then the token
 *    will be url-escaped, and a full URL is returned. If blank or null, then just the token
 *    is returned (and it won't have been escaped for use in a URL).
 *    NOTE: if you want any previous session to be logged out when using token login (which is often
 *    what you do want to happen), then this URL should be the logout route. See the special
 *    handling for token in Routing::logoutRouteImplementation()
 *    If you skip doing a logout, then if the user already has a logged-in session that session will
 *    be used, and the token login will be ignored. (This is bad if the token login was for another
 *    user!)
 * @param int $T This is how many seconds to allow the URL to be live for. The default of 60,
 *    means it is guaranteed to work for the next 60 second. (Max of 120s, average of 90s.)
 * @param string $extra Optional extra information to put in the URL. (also put in the hash). Defaults to "".
 *    NOTE: must not contain a hyphen. Any other characters allowed.
 * @param number $now Defaults to current time (in secs-since-1970); only need to set this for unit tests, normally.
 *
 * @internal Originally makeLoginTokenFromEmail() and validateLoginToken() were in RouteBase, but
 *   they were useful outside of route classes, so were made public static, but then it made more sense
 *   for them to be either here or in class Routing. And as they are for authentication, this class seems
 *   like the best place.
 */
public static function makeLoginTokenFromEmail($salt,$em,$url,$T=60,$extra='',$now=null){
if($now === null)$now=time();
$now = (int)($now/$T);
$em_parts = explode('@',$em);
$hash = md5($salt.$T.$em.$extra.$now);
$token = "A".$T.".".$em_parts[1] . ".." . $extra. "-". $hash . "-" . $em_parts[0];
if(!$url)return $token;
return $url."?token=".urlencode($token);
}


/**
 * The partner function to makeLoginTokenFromEmail
 * 
 * @param Function $callback Takes these params: $em, $extra and has to return $salt.
 *    Simplest version, with a constant salt, is:
 *       function($em,$extra){return "mySALT";}
 * 
 *    If you need the email returned, do it this way:
 *       function($em,$extra)use(&$email){$email = $em;return "mySALT";}
 * 
 *    As $callback might also do the user DB lookup, you might want to share that result.
 *    Do it like this: (this example also shows how $extra might be used)
 *       $user=null;
 *       function($em,$extra)use(&$user){
 *          $user = $usersDB->findUserByEmail($em);
 *          if($extra != $user['id'])throw QQAPI\ErrorException(...);
 *          //For strictest security, $usersDB->resetSalt();
 *          return $user->salt;
 *          }
 *      
 *     Where function resetSalt() would be something like this:
 *          $this->updateOneField($this->tableName, "salt", "", "email", $this['email']);
 *
 * @param number $now Defaults to current time (in secs-since-1970); only need to set this for unit tests, normally.
 * @return string Error message, or "" on success. If $callback needs to report an error, it will
 *    need to throw an exception. (Or, returning a bad salt would indirectly force an error.)
 * 
 * @internal I could've had $callback be the salt if it was given as a string. But defining
 *   the short function to do the same thing is relatively easy.
 */
public static function validateLoginToken($token,$callback,$now=null){
if($now === null)$now=time();
if(!preg_match("/^A(\d+)[.](.+?)[.]{2}([^-]*)[-]([^-]+)[-](.+)$/",$token,$parts))return "Bad token: {$token}";
/* $parts:
 * 1:TTL
 * 2:Domain
 * 3:extra info
 * 4:hash
 * 5:User part of email
 */
$T = (int)$parts[1];
$now = (int)($now/$T);
$em = $parts[5].'@'.$parts[2];
$extra = $parts[3];
$salt = $callback($em,$extra);
$hash = md5($salt.$T.$em.$extra.$now);
if($parts[4] != $hash){ //Give it up to $T seconds in the future to still be valid
    $hash2 = md5($salt.$T.$em.$extra.($now-1));
    if($parts[4] != $hash){    
        return "Bad hash. em={$em},salt={$salt},T={$T},extra=$extra,now=$now, given hash={$parts[4]},our hash={$hash} or {$hash2}";
        }
    }
//If we reach here, success!
return "";  //Blank string indicates success
}



/**
 * This is sample code for the findUser() you give to Authenticator, but also useful as-is.
 * 
 * It is a simple version that assumes all the users are already pre-loaded in a global
 * array, called 'userLookup'.
 *
 * Assumption: canCache will be set to true, and therefore $hashpw is never used.
 * Assumption: it throws ErrorException on any login problem, with this exact string:
 *     'User not found, or password is invalid.'
 */
public static function findUserHelperWithLookupTable($username,$password){
$msg='User not found, or password is invalid.'; //Same message shown to user, whatever the failure.

if(!array_key_exists($username,$GLOBALS['userLookup']))$user = false;
else $user = $GLOBALS['userLookup'][$username];

if(!$user){
    usleep(mt_rand(100,500)*1000);  //0.1s to 0.5s random sleep
    throw new ErrorException($msg,array(),"username={$username} not found");
    }

if(!password_verify($password,$user['password'])){
    usleep(mt_rand(50,450)*1000);  //0.05s less sleep than above, to allow for hashing overhead
    throw new ErrorException($msg,array(),"username={$username} found, password={$password} not matched. raw pw ={$user['password']}");
    }
$user['canCache']=true; //I.e. the whole user object can be kept in $_SESSION, no need
    //to load it from disk/DB each time. (That implies we never allow two simultaneous logins,
    //so a given user will never have more than one session.)
return $user;
}


/**
 * This is sample code for the findUser() you give to Authenticator, but also useful as-is.
 * 
 * Note: It cannot be given directly. Instead use it like this:

 $A=new Authenticator(array(
    'type'=>'session',
    'loginPHP'=>'main.html.inc',
    'allowCommandline'=>false,
    'findUser'=>use($DB) function($username,$password,&$hashpw){
        $user = Authenticator::findUserHelperUsingSessions($DB, $username,$password);
        //Any custom post-login code here (but won't reach here if a problem)
        return $user;
        },
    ));
 
 * Assumption: session login, not basic auth, etc.
 * Assumption: using a DB derived from DBUsers.
 * Assumption: canCache will be set to true, and therefore $hashpw is never used.
 * Assumption: it throws ErrorException on any login problem, with this exact string:
 *     'User not found, or password is invalid.'
 */
public static function findUserHelperUsingSessions($DB,$username,$password){
$msg='User not found, or password is invalid.'; //Same message shown to user, whatever the failure.

if(!$password)throw new ErrorException($msg,array(),"Blank pw given (username={$username}).");
if(!$username)throw new ErrorException($msg,array(),"Blank username given (but password was not blank).");

$user = $DB->findUserByEmail($username);

if(!$user){
    usleep(mt_rand(100,500)*1000);  //0.1s to 0.5s random sleep
    throw new ErrorException($msg,array(),"username={$username} not found");
    }

if(!password_verify($password,$user['password'])){
    usleep(mt_rand(50,450)*1000);  //0.05s less sleep than above, to allow for hashing overhead
    throw new ErrorException($msg,array(),"username={$username} found, password={$password} not matched. raw pw ={$user['password']}");
    }
$user['canCache']=true; //I.e. the whole user object can be kept in $_SESSION, no need
    //to load it from disk/DB each time. (That implies we never allow two simultaneous logins,
    //so a given user will never have more than one session.)

return $user;
}


/**
 * Just like findUserHelperUsingSessions(), but it also records the login, and checks for existing
 * sessions, which it will try to destroy if found.
 * 
 * Note the $now parameter is added (but will default to getting current time).
 * 
 * Remember the other login won't realize its session has been destroyed until it performs some
 * action. (That is one reason a regular heart-beat/update request is a good idea.)
 * 
 * Note: if a previous session was found this function will have effectively restarted $_SESSION.
 * That means the Application instance's $SESSION variable will need to be assigned to again.
 * 
 * @internal The first part of this function is identical to the version without recording login,
 *   so we take advantage of that and share code.
 */
public static function findUserHelperUsingSessionsRecordLogin($DB,$username,$password,$now=null){
$user = self::findUserHelperUsingSessions($DB,$username,$password);    //throws if any problem (i.e. no need to check return value)

$newSessionId = session_id();
if($user['currentSession'] != ""){
    if($user['currentSession'] == $newSessionId){
        //TODO: I think this should be logged. Can it ever happen?
        //No need to call recordLogin(), so we can return immediately.
        return $user;
        }
        
    //Uh-oh, another login exists (or they just forgot to log-off), so destroy that session.
    session_write_close();   //Make sure current session is safe.

    //Pretend to be the old session
    session_id($user['currentSession']);
    session_start();
    
    //Destroy it
    session_destroy();
    session_write_close();

    //Go back to our new session
    session_id($newSessionId);
    session_start();
    }
    
$DB->recordLogin($username, $newSessionId, $now);
$user['currentSession'] = $newSessionId;
return $user;
}


/**
 * An example of a function to give as the onLogout option.
 * 
 * @param $DB An instance of DBUsers (or derived class). It is only required to have
 *      a recordLogout() function.
 */
public static function onLogoutRecordLogout($DB){
if(!array_key_exists('user',$_SESSION))return;
$DB->recordLogout($_SESSION['user']['email']);
}



/**
 * This is sample code for the findUser() you give to Authenticator. It expands on findUserHelperUsingSessions
 * by allowing login-as (using a short-lived token found in the URL).
 * 
 * @todo Rename to be more specific, and then make alternative version to handle the cookie keep-me-logged-in (including setting the cookie)
 * 
 * ---> Could merge with previous function, and take a $mode parameter that describes what it is allowed to consider?
 * 
 */
public static function findUserHelperUsingSessionsTokenSupport($DB, $username,$password,
    $persistentCookiePath="/", $persistentCookieValidity = 1209600){
$msg='User not found, or password is invalid.'; //Same message shown to user, whatever the failure.

if(!$password)throw new ErrorException($msg,array(),"Blank pw given (username={$username}).");
if($username === null){

    //**** This is cookie handling *****
    //The regex here assumes base64 (6 chars, then 26 chars, split by a colon).
    if(preg_match('|^([A-Za-z0-9+/]{6})[:]([A-Za-z0-9+/]{26})$|',$password,$parts)){  //This is the cookie "stay-logged-in" login
        $user = $DB->findUserByToken($parts[1],$parts[2]);
        if($user === false){    //Cookie was valid, but has expired
            self::clearAnyTokenCookie($persistentCookiePath);
            throw new RedirectException();   //Go to top page, so they can login again.
            }
        if(!is_array($user))throw new ErrorException($msg,array(),$user);
        $user['canCache']=true;
        return $user;
        }
    //**** END OF cookie handling *****

    //**** This is login-as handling *****
    $user=null;
    $ret = Authenticator::validateLoginToken($password,
        function($em,$extra)use($DB,&$user){ //Returns the salt (also load the user object at this point)
            $user = $DB->findUserByEmail($em);
            //No need to clear $user['tokenloginas'] in the DB, as the login token is only valid for 60-120s anyway.
            return $user['tokenloginas'];
            }
        );
    if($ret != "")throw new ErrorException($msg,array(),$ret);
    $user['canCache']=true;
    return $user;
    //**** END OF login-as handling *****
    }
$user = $DB->findUserByEmail($username);

if(!$user){
    usleep(mt_rand(100,500)*1000);  //0.1s to 0.5s random sleep
    throw new ErrorException($msg,array(),"username={$username} not found");
    }

if(!password_verify($password,$user['password'])){
    usleep(mt_rand(50,450)*1000);  //0.05s less sleep than above, to allow for hashing overhead
    throw new ErrorException($msg,array(),"username={$username} found, password={$password} not matched. raw pw ={$user['password']}");
    }
$user['canCache']=true; //I.e. the whole user object can be kept in $_SESSION, no need
    //to load it from disk/DB each time. (That implies we never allow two simultaneous logins,
    //so a given user will never have more than one session.)

//**** This is cookie handling *****
if($username!==null && Application::getString('persistent','') == '1'){
    //NB. could assert($user['email'] == $username)
    $now = time();
    $expire = $now + $persistentCookieValidity;
    $value = $DB->createTokenGivenEmail($username, $persistentCookieValidity, $now);    //Throws a PDOException
        //if any problem updating the DB (e.g. if $username is invalid)
    setcookie("token", $value, $expire, $persistentCookiePath);
    }
//**** END OF cookie handling *****
    
return $user;
}


/**
 * An extension of findUserHelperUsingSessionsTokenSupport() that does the recordLogin() call, and clears
 * out the old session.
 *
 * @param string $username  If this is a normal string, then the user has filled in the login form,
 *    and this is the login username (e.g. their email address). And if "persistent" is in the 
 *    form input, then findUserHelperUsingSessionsTokenSupport() will have sent back a "token" cookie too.
 *    Everything is as normal. I.e. if there is an old session around, delete it.
 *    BUT, if this is null then it means they: a) do not have a session currently; b) they are using
 *    the "token" cookie to do a login. And if findUserHelperUsingSessionsTokenSupport() did not
 *    throw it means it was valid, and the token login succeeded.
 * 
 * @todo What does finding an old session mean in this case?
 *      A) Our previous login - perfectly normal, as you don't use logout with persistent login.
 *      B) Another client has logged in, and it is their login we're seeing as the "old session".
 *      C) Another client did log in, and it is their genuinely old session we're seeing. (Either
 *        they forgot to logout, or it is persistent too.)
 *    This creates a genuine problem: in cases A and C we are good: we want us to be logged in.
 *    But in case B we want the opposite: we want the other client to stay logged in, and for
 *    ourselves to get logged out.
 *    This is really tricky because B and C look almost identical.
 *    One clue we have is how long ago the loginTime was. E.g. if the login was within the last
 *    60 seconds is it reasonable to assume B is the intended login, and C is just doing an
 *    automated ajax call? Or could they have logged in on another device by mistake, shut it
 *    immediately, and come to log on from this device. That might only be 10 seconds apart, but
 *    in that case the intention is the same as case C.
 *    Another clue is the type of action. I.e. if this login is being caused by an ajax call then
 *    chances are we want to do a logout. Whereas if this login is coming from a login form (which
 *    I think never happens if $username is null??), or is requesting the main HTML, then it seems
 *    fair to assume it is case B, and the other client should be logged off.
 *    Anyway, this gets very application-specific so is hard to handle generically.
 *    For the moment I think we should leave it, and assume/hope it is cases A and C where it
 *    actually happens.     
 * 
 * 
 * @see findUserHelperUsingSessionsRecordLogin() for more details. The body of this function is
 *   (currently) basically identical to the body of that function.
 */
public static function findUserHelperUsingSessionsTokenSupportRecordLogin($DB,$username,$password,
    $now=null, $persistentCookiePath="/", $persistentCookieValidity = 1209600){
$user = self::findUserHelperUsingSessionsTokenSupport($DB,$username,$password,$persistentCookiePath,$persistentCookieValidity);    //throws if any problem (i.e. no need to check return value)

$newSessionId = session_id();
if($user['currentSession'] != ""){
    if($user['currentSession'] == $newSessionId){
        //TODO: I think this should be logged. Can it ever happen?
        //No need to call recordLogin(), so we can return immediately.
        return $user;
        }
        
    //Uh-oh, another login exists (or they just forgot to log-off), so destroy that session.
    session_write_close();   //Make sure current session is safe.

    //Pretend to be the old session
    session_id($user['currentSession']);
    session_start();
    
    //Destroy it
    session_destroy();
    session_write_close();

    //Go back to our new session
    session_id($newSessionId);
    session_start();
    }
    
$DB->recordLogin($username, $newSessionId, $now);
$user['currentSession'] = $newSessionId;
return $user;
}


}

