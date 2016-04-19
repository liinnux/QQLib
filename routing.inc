<?php
namespace QQAPI;

include_once "authenticator.inc";

/**
* This handles the processing of a collection of routes.
*
* A router is of the form /action/var1/var2.format
* which maps to calling a function.

* var1 and var2 can be specified as optional.
* The format is things like "xml", "json", etc.
*
* Note: we don't aim for extra complexity: no regex support, no
* support for giving parameters in different order. By the latter I
* mean some systems support URLs like: /action/y/56/z/99/x/123
* In this system you either do:  /action/123/56/99  or you specify
* them as post variables.
*
* However we do offer support for servers where you do not
* want to use .htaccess and the RewriteEngine. In this case the
* full action string is given in the "a" parameter.
*
* NOTE: Authorization. The prepare() function grabs authorization details
* and stores them, but does not do the DB query (so username/password is
* not validated just yet).
* Then in handle() is when we do the DB query to get the User object (but
* only if the router requires authorization). This means unvalidated users
* can cause routes to get loaded, and their pre-processing to be done.
* If all routes require authorization, then you can force early authorization
* and it will check for a valid user before checking for a valid route.
* @see $do_early_user_validation
* 
*
* @todo There are two basic ideas here.
*   SIMPLE: the routers job is just to parse the URL into an array.
*      It passes the array of parameters to the function, and that function
*      checks for required parameters, sets defaults, etc.
*      NB. The action part cannot contain forward slashes.
*      NB. Requires use of a call to function_exists(), and cannot call
*            class methods (not even static ones).
*
*   INVOLVED: Here each function we are going to use is pre-registered,
*      with the function name to call (default to "action_XXX"), the required parameters,
*      the optional parameters and their defaults.
*      We could also specify a special exception to throw.
*      It can also do validation on the parameters (range-checking, type-checking,etc.)
*      NB. The action part can contain forward slashes, as we specify
*      the function to call.
*      NB. The given function could also be an anonymous function.
*      NB. One function can handle multiple actions (or we can use the anonymous
*          function to call a common function but inserting a special parameter, etc.)
*/
class Routing{
/**
* If returns true then it loads the user object (and therefore does validation) in the
* prepare() stage; if false it waits for the handle() stage (and if it does not
* need authorization it may not end up doing it).
*
* The default is to always return true, except for the couple of built-in special routes
* (signup, logout) where requiring a logged-in user makes no sense.
* (@see body of prepare() for where this default is defined)
*
* It acts as an extension of $this, so $this->action, etc. are available.
*
* @internal At first glance it seems like a logoutRoute would always have a logged-in
*  user. But if reload is called you want it to behave the same, not give an error.
*/
public $do_early_user_validation = null;

/**
* For when using a fixed prefix on the URL that is not part of the action.
*
* For a dedicated domain, where this script is in the top-level, this is simply "/"
* If it is being kept under "example.com/testing/" then this would
* be set to "/testing/", and then http://example.com/testing/action/77/123.json
* will be processed correctly.
*/
public $url_prefix='/';

/**
* This is the name of the logout route.
*
* It is currently used in two main ways. First, with $specialHTMLSupport,
* it counts as an exception, so is allowed to be processed as HTML.
* (Similarly, early authentication is never done.)
* Second, if you set the name here (e.g. "logout"), but do not call
* addRoute("logout"), then an internal implementation will be used.
*
* NOTE: should be the lowercase version of the route name.
*/
public $logoutRoute=null;

/**
* Key is an action, i.e. the first part of the URL (the bit between the
* URL prefix and the first slash).
* Value is an object with this function:
*      void handle($params)
* $params come from the URL; Application::$POST is also available, and
*  that will be a reference to the $_POST global (except in unit tests).
*/
public $routes=array();


/**
* By setting this you set up a special mode, where there is an HTML file
* that is loaded initially, and you expect all subsequent calls to be JSON.
*
* When this is not set, basically any route can be called as any format.
* When it is set, only JSON is allowed, except for special routes:
*    $specialHTMLRoutes
*    $logoutRoute
*
* The first check is that only URLs ending in ".json", ".html" or "/" are
* allowed; everything else gets rejected.
* Then user is authenticated, which may involve sending the user back
* the login html page.
* Then for "/" and ".html" (if not the special routes), we instead send
* back the HTML (or PHP) file specified here. If it ends in "*.php" or "*.inc"
* it is include()-ed as PHP, otherwise it is treated as static HTML.
*
* @internal There is quite a bit of overlap with the $loginPHP + loginHTML variables
*   of the Authenticator class. However I have not found a graceful way to
*   deal with both. Also, those are only needed for session logins, where as
*   $specialHTMLSupport could be used with basic auth too.
*
* @internal I originally had a more flexible approach, where each route could
*  specify the types it can work with. But this is the only usage case, so far, where
*  type matters. So it seemed best to just add the functionality that was needed, and no more.
*/
private $specialHTMLSupport;


/**
* Routes listed here behave differently when using $specialHTMLSupport, in
*   that they are not json only; also they not only do not require the user
*   to be logged in, they will throw exception if they are.
* 
* You should put here: signup, forgotpw and any callbacks from remote servers
* (e.g. payment gateways). There is no need for logout to be here (see $logoutRoute).
*
* NOTE: each entry should be the lowercase version of the route name.
*
* NOTE: if $do_early_user_validation returns true then this will
* be ignored (i.e. user validation will always take place in the handle()
* function, and never in the prepare() function).
*/
private $specialHTMLRoutes = array();

/**
* Set to 'main' when coming from the commandline.
*
* This is currently used to turn all the commandline params (other than
* the action) into POST variables. (Otherwise feed commands cannot
* be used from the commandline.)
*/
private $requestSource=null;

/** An instance of Authenticator
*
* @todo In future it will just need to be an object with a certain interface.
*/
private $authenticator=null;


/** Just used to pass data from prepare() to handle() */
private $url;

/** The first part of the URL. Set in prepare(), used in handle() */
private $action;

/** The remaining parts of the URL. Set in prepare(), used in handle() */
private $params;

//------------------------------------

/**
* @param String $classname This is used in three ways:
*     * There should be a file called route/XXXX.inc
*     * That file should have a class XXX {}
*     * The first part of the URL.
*   It is usually in CamelCase. The URL will be used case-insensitively,
*   but it is encouraged that the URL will be lowercase.
*   E.g. call this addRoute("Happy"), to load a class Happy found
*   in a file called route/Happy.inc, and accessed with a URL of
*   http://example.com/happy or similar.
*
*    NOTE: the class XXX is typically a user-defined class derived from
*      class Route. But the bare minimum requirement is that it has this function:
*           function handle($params){}   (no return value)
*   E.g. if $classname is "adduser" or "AddUser" then the URL will be:
*         http://example.com/adduser/fred/28.xml
*/
public function addRoute($classname){
$this->routes[strtolower($classname)]=$classname;
}

/**
* Like addRoute(), but for adding a set in bulk
*
* @param Array $list Simple list of strings. (Need not be an array - any
*    container where foreach() works!)
*/
public function addRoutes($routes){
foreach($routes as $classname)$this->addRoute($classname);
}


/**
* Just like setting routes, call this before calling handle().
*
* @param String $fname Either a PHP or static HTML file to include
*   when a user requests a route but as HTML.
*/
public function addSpecialHTMLSupport($fname, $specialRoutes){
$this->specialHTMLSupport = $fname;
$this->specialHTMLRoutes = $specialRoutes;
}


/**
* Call this first.
*
* There is no need for any routes to be set by this point.
*
* @param Object $authenticator An instance of Authenticator (or a similar object
*   that has useCommandline() and getUser() public functions).
*
* @internal REDIRECT_URL was working fine on Ubuntu 10.04, Ubuntu 11.10 and
*   Mint 16 (i.e. Ubuntu 13.10). But it is simply not being set on Ubuntu 14.04, and I've
*   seen reports of it not being set on earlier versions. So I suspect it is not the Apache
*   or PHP version, but some subtle difference in configuration in Apahce (or PHP).
*   As I cannot track it down I now favour using SCRIPT_URL, which appears to be the replacement.
*/
public function prepare($authenticator){
$this->authenticator=$authenticator;

//Get the URL from the various choices
if(array_key_exists('argc',$GLOBALS)){
    if(!array_key_exists(2,$GLOBALS['argv']))throw new ErrorException('Give user then action given on commandline');
    $url=$GLOBALS['argv'][2];
    $this->authenticator->useCommandline($GLOBALS['argv']); //Will throw if commandline auth is not allowed.
    $this->requestSource='main';    //TODO: redundant now we have the next line?? --> No, not quite yet, as it is still used later on to move commandline params to POST data
    Exception::$details['requestSource']='main';
    }
else{   //Access via a web server: get the action from the URL
    if(array_key_exists('SCRIPT_URL',$_SERVER)){
        $url=substr($_SERVER['SCRIPT_URL'],strlen($this->url_prefix));
        Exception::$details['SCRIPT_URL']=$_SERVER['SCRIPT_URL'];
        }
    elseif(array_key_exists('REDIRECT_URL',$_SERVER)){
        $url=substr($_SERVER['REDIRECT_URL'],strlen($this->url_prefix));
        Exception::$details['REDIRECT_URL']=$_SERVER['REDIRECT_URL'];
        }
    else{
        if(!array_key_exists('a',$_REQUEST))$url='';
        else{
            $url=$_REQUEST['a'];
            Exception::$details['REQUEST[a]']=$_REQUEST['a'];
            }
        }
    Exception::addToDetails(array('REMOTE_ADDR','HTTP_USER_AGENT',
        'REQUEST_METHOD','REQUEST_TIME','PHP_AUTH_USER','HTTPS','REQUEST_URI'),$_SERVER);
    }

//Look for ".json", etc. at the end of it. If there then remove it.
//NOTE: if not giving it the format and the final parameter contains dots, then
//     part of that parameter will get removed (both messing up that parameter and
//     giving a bogus format). The solution is for the user to either explictly state the
//      desired format (highly recommended), or finish the URL with a "/".
//Note: as soon as setFormat() has been called we can safely throw an exception; before
//     then it will output an error in text format, which might be undesirable.
if(preg_match('/^(.+?)\.([a-z0-9]+)$/i',$url,$parts)){
    $this->url=$parts[1];
    Application::setFormat($parts[2]);
    }
else{
    $this->url=$url; 
    Application::setFormat('');
    }

$this->authenticator->setUrl($this->url);

$this->params=explode('/',$this->url);
assert(count($this->params)>=1);
$this->action=strtolower(array_shift($this->params));

if($this->do_early_user_validation === null){   //Provide a default if not defined yet
    $this->do_early_user_validation = function(){
        return (!in_array($this->action,$this->specialHTMLRoutes) && $this->action != $this->logoutRoute);
        };
    }

if($this->do_early_user_validation->bindTo($this,$this)->__invoke()){   //For bindTo() (which allows the
    //anon function to use $this, including private vars), @see http://php.net/manual/en/closure.bindto.php.
    //@see http://stackoverflow.com/a/14063179/841830 for why we need __invoke().
    $this->authenticator->getUser(); //Throws if some auth problem
    }
}


/**
* Called after prepare()
*
* Make sure addRoute() has been called for each route we want to handle, by this point.
*
* Note: we don't support regexes for route names, but by giving a function for defaultRoute
* you could simulate that behaviour quite easily.
*
* Note: specialHTMLSupport handling is done without checking if the route exists. I.e.
* if using that then anyone typing a random URL will still get given the HTML page. That
* HTML page is then assumed to process the URL, as JSON, at which point it will be told
* the URL is bad, and then the HTML page can give a suitable default or error message.
* (This is important, because it is possible the front-end file will be using the History API, and
*  putting local actions in the URL that have no corresponding back-end Route.)
*
* @param Mixed $blankRoute If the route ($this->action) is the empty string, then it is set to this.
*       (If this parameter is not set then an ErrorException is sent.)
* @param Mixed $defaultRoute If either $this->action is not set, or the
*     action is not found in $this->routes, then this is used. If it is null then
*     an ErrorException is thrown.
*     If a string then it is used as the replacement route. (And if $defaultRoute is not found
*       then a SystemException is thrown instead!) NOTE: should be lowercase.
*     If a function then it is called to get the name of the route to use. This function
*       returns a string (of a route that must exist). It takes these parameters:
*          ($this->action, $this->params, $this->routes)
*       $params can be taken by reference and modified.
*     NOTE: routes can discover the originally requested (and not found) route in
*        $this->originalAction (where $this is the route class).
*     NOTE: $this->action and $this->originalAction are lowercase version.
*
* @todo Add unit tests for the function form. (And the string form, for that matter.)
*
* @internal Remember that filenames are case-sensitive (at least on Linux), but
*   classnames are case-insensitive in PHP. If the route says "happy", then the
*   filename *must* be route/happy.inc, but the class inside it can be happy, Happy
*   HAPPY, or even hApPy. It is recommended though that the class is called "Happy",
*   and therefore that the route and filename are also "Happy".
*
* @todo There is a slight API imbalance here. We look for $obj->needUser inside
*    the route to control its behaviour. But we rely on $this->specialHTMLRoutes to
*    decided if we need to call isLoggedIn().
*/
public function handle($blankRoute=null,$defaultRoute=null){
$originalAction=$this->action;
if($blankRoute!==null && $this->action=='')$this->action=$blankRoute;

if($this->specialHTMLSupport && Application::$format=='html'
    && !in_array($this->action,$this->specialHTMLRoutes) && $this->action!=$this->logoutRoute
    ){
    $this->handleSpecialHTMLSupport();
    return;
    }

if(!array_key_exists($this->action,$this->routes)){
    if($this->action==$this->logoutRoute){
        $this->logoutRouteImplementation();
        return;
        }
    if(!$defaultRoute)throw new ErrorException('Unknown action (%1$s)',array($this->action),"Available routes:".implode(',',array_keys($this->routes)));
    if(is_callable($defaultRoute))$this->action=$defaultRoute($this->action,$this->params,$this->routes);
    else $this->action=$defaultRoute;
    if(!array_key_exists($this->action,$this->routes))throw new SystemException("Default route ({$this->action}) not found. the originally requested action (".$originalAction.") was also not found.");
    //else the default route is good, fall through and use it.
    }

$classname=$this->routes[$this->action];
$fname="route/".$classname.".inc";
if(!file_exists($fname))throw new SystemException("Missing action file ({$this->action}).");

include_once($fname);
$obj=new $classname;
$obj->action=$this->action;
$obj->originalAction=$originalAction;

if($obj->needUser){
	$obj->U=$this->authenticator->getUser();    //Does authentication if it wasn't already done
        //in prepare(). So if some auth problem it will throw at this point.
    }

if($this->requestSource=='main')$this->moveParamsToPost($this->params,$obj->params);  //TODO: see below

$obj->handle($this->params);
}

//----------------------------


/**
* Helper for handle(): currently used when we want the commandline
* args to be treated as post data, rather than get data (e.g. so we can use
* the Feed actions).
*
* IMPORTANT this modifies Application::$POST. It assumes that variable
* already exists as an array. (Default PHP-CLI behaviour is to create it as a blank
* array.)
*
* NB. This is limited:
*   1. it can only be used for single-field data feeds, where the field name 
*       is value. (If you give extra commandline args they are ignored.)
*   2. Optional parameters in the Feed* commands are not available (as they
*       interfere with the more important POST vars). Currently that is just
*       'prefix' which is never relevant to using Feed from the commandline anyway.
*
* @param Array $params The commandline arguments. Note: it will always be
*     reset to a blank array, even if not all parameters were used.
* @params Array $action_params This is the 'params' element of a Route instance.
*     The outer array is a simple array. Each element is an associative array, where
*     the 'name' key is the important one as it tells us the POST variable name to look for.
*
* @todo URGENT I'm not sure this belongs here? I.e. the $action_params is specific to Route.inc, which
*    isn't part of the public (to-be-released) QQAPI...
*    ---> Well, first, maybe Route.inc will become part of that API, so no problem there.
*/
private function moveParamsToPost(&$params,$action_params){
foreach($action_params as $ix=>$v){ //Handle special cases (default params not supported)
    if($v['name']=='prefix' || $v['name']=='mode'){unset($action_params[$ix]);continue;}
    }
$cnt=count($action_params);
foreach($params as $ix=>$v){
    if($ix==$cnt)$k='value';	//Last commandline param is stored as $POST['value']
    elseif($ix<$cnt)$k=$action_params[$ix]['name'];
    else break;	//Ignore any extras
    Application::$POST[$k]=$v;
    }
$params=array(); //Just to blank
}


/**
* Helper for handle()
*
* We know, if we are here, that it is HTML, and we know it is not one of the special routes.
* We also know the user is logged-in and valid.
*
* @todo The random 5-digit code, which can be embedded in a the page (only
*   when using PHP), and is also placed in the session, is a little security paranoia.
*   I suspect it is completely redundant, as it is basically duplicating the session cookie?
*   Therefore I've commented it out.
*   If we did start using it, we also need code somewhere that checks for it being set.
*   I suppose this could be as simple as, in Authenticator:
*       if($_SESSION && array_key_exists('alreadyLoggedInCode',$_SESSION)){
*           ...look for code in the POST data, and throw an error if not found?
*           ...but if found, then remove it from the session (i.e. it is a one-use code)
*           --->For extra paranoia, also check $this->action matches $_SESSION['alreadyLoggedInAction']
*               (which would also need to be set below)
*           }
*   
* @todo The above comment about "know they are logged-in" is wrong if not using early authentication.
*   Could do this:
*       if($obj->needUser){
*            $obj->U=$this->authenticator->getUser();
*            }
*   Does nothing if already authenticated, might throw if they could not be authenticated.
*/
private function handleSpecialHTMLSupport(){
if(preg_match('/[.](php|inc)$/i',$this->specialHTMLSupport)){
    //if(isset($_SESSION))$_SESSION['alreadyLoggedInCode']=$code=mt_rand(1,99999);
    $action = $this->action;
    include($this->specialHTMLSupport);
    }
else echo file_get_contents($this->specialHTMLSupport);
}


/**
* Built-in logout route, to save each application having to implement it.
*
* To use this, set $this->logoutRoute (e.g. to "logout"), and make sure
* addRoute("logout") is not called.
*
* Currently only supports session auth.
* If JSON it sends back "loggedout" (set to true).
 * 
 * @internal The clearAnyTokenCookie() call was not moved to logoutSession() simply
 * as it refers to a $this variable. (Probably fixable - sort it out later.)
*/
private function logoutRouteImplementation(){
$this->authenticator->onLogout();
self::logoutSession();
Authenticator::clearAnyTokenCookie($this->url_prefix);
if(Application::$format=='html'){
    if(array_key_exists('token',$_GET)){
        throw new RedirectException("/?token=".urlencode($_GET['token'])); //Send user to top page, but with the login token
        }
    throw new RedirectException("/"); //Send user to top page
    }
self::output(array('loggedout'=>true)); //I.e. client then needs to do the redirect
}


/**
 * Used by logoutRouteImplementation(), but can also be used if another route needs to
 * do a logout.
 * 
 * We use `@` on session_destroy() as if it does not exist, we don't care. (It seems
 * to happen in some unit tests, at least.)
*
* @see http://www.php.net/session_destroy
*
* @todo I wonder if it ought to be in Authenticator class? Then it can use
*       the type, and know if it is dealing with session, basic auth, etc.
* @todo Ought to support basic auth.
 */
public static function logoutSession(){
if(!headers_sent() && ini_get("session.use_cookies")) {
    $cookieParams = session_get_cookie_params();
    setcookie(session_name(), '', 1 /*Very old time*/,
        $cookieParams["path"], $cookieParams["domain"],
        $cookieParams["secure"], $cookieParams["httponly"]
        );
    }
@session_destroy();
}

//--------- Helper output functions ------------

/**
* Use this if using Routing::specialHTMLSupport and JSON communication only.
* (I.e. HTML requests should never get here, only JSON.)
*
* Note: handles UTF-8.
*
* @internal Given that we always expect 'json' in this function (i.e. a SystemException
*  should *never* be thrown in a production application!)
*   ---> This is why I've commented it out
*/
/*
protected static function outputJSONOrThrow($d){
if(Application::$format=='json')echo self::mb_json_encode($d);
else throw new SystemException('Unexpected format ('.Application::$format.')');   //System exception as we'd have looked for bad formats much earlier, in Routing, and reported to the user at that point.
}
*/

/*
* Use for responses that are not simple boolean
*
* @param Mixed $response An array, string, etc.
*/
public static function formatResult($response){
switch(Application::$format){
    case 'json':case 'jsoncsv':return self::mb_json_encode($response);
    case 'xml':case 'xmlcsv':return xml_encode($response);
    default:return print_r($response,true);
    }
}

/**
*/
public static function output($response){
echo self::formatResult($response);
}


/**
* @todo Probably this belongs in a more general QQAPI Utility class?
*
* @internal There appears to be a problem with the previous code (i.e. the code
* for php 5.3 and earlier) where it will: a) sometimes not leave UTF-8 characters
* unescaped; b) it modifies the original data (i.e. escapes the original data
* strings too).
* Therefore it is recommended this code be used on php 5.4 or later, where possible.
* (In simpler situations, or where not using kanji, etc., it should still be fine on php 5.3)
*/
private static function mb_json_encode($d){
if(PHP_VERSION_ID >= 50400)return json_encode($d,JSON_UNESCAPED_UNICODE);
array_walk_recursive($d, function (&$item, $key) { if (is_string($item)) $item = mb_encode_numericentity($item, array (0x80, 0xffff, 0, 0xffff), 'UTF-8'); });
return mb_decode_numericentity(json_encode($d), array (0x80, 0xffff, 0, 0xffff), 'UTF-8');
}

//-------------------------------------------------


/**
* A high-level example of typical usage.
*
* Note: will never return.
* Note: if any exception it will be caught and reported.
*
* @param Array $routes The available actions. @see addRoute()
* @param Callback $getUser Will return a user object. @todo NOT USING YET.
*       @todo Not sure if I don't instead want an object with various parameters and functions?
* @param String $realm Optional. Only needed if using basic auth. @todo Move it into Authenticator class, and then take
*		Authenticator parameters as parameters here?
* @param Function $do_early_user_validation Assigned to Routing's $do_early_user_validation. The default
*		means all routes, except logout, require authentication.
*
* @todo Parameters to control if using PHP sessions? Or, maybe the routes should decide that?
*     And/or the $findUser callback???
*/
public static function runAndExit($routes,$realm=null, $do_early_user_validation = null){
if($realm!==null)MustAuthenticateException::$realm=$realm;
Application::$POST = $_POST;

try{

$A=new Authenticator(array(
    'findUser'=>'findUser',
    'type'=>'basic',
    'allowCommandline'=>true,
    ));
$R=new Routing();
$R->do_early_user_validation = $do_early_user_validation;
$R->prepare($A);  //User authentication done here if Routing::$do_early_user_validation returns true
$R->addRoutes($routes);
$R->handle();  //User authentication done here, if needed

}catch(ErrorException $e){
    $e->reportProblemLogAndExit();
}catch(SystemException $e){
    $e->reportSystemErrorLogAndExit();
}catch(MustAuthenticateException $e){
    $e->requestBasicAuthAndExit();
}catch(RedirectException $e){
    $e->performRedirectAndExit();
}catch(Exception $e){   //Typically this is PDOException if it ever happens.
    Exception::reportOtherExceptionAndExit($e);    //Report same way as for SystemException.
}

exit;   //If we reach here the route exited cleanly.
}


}

?>