<?php
/**
* QQ API: This is a class for route classes to derive from, as it gives all
*    routes useful utility functions. (But it is also optional - a route can choose
*    not to derive from this, and it will still be a valid route.)
*
* @copyright QQ Trend Ltd, 2015
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

/**
* Defines some utility functions that can be useful to all route classes.
*
* The idea is BasicRoute derives from this (as do other special route classes that do not
* derive from BasicRoute). The other reason to do this is for unit testing - if a unit test
* wants to override some of this behaviour (e.g. stop emails actually being sent!) then
* it can do it if they are in the route class; if they were in a static utility class this would
* be more complex.
*
* @internal This class is not abstract. It could therefore be instantiated if you needed
*   these functions outside a route class. (However I've left the functions as protected, rather
*   than making them public, at least for the moment.)
*
* @internal They are normal, not static, even though (generally) they are not dealing with
*   any class data.
*
* @todo The UTF8 encoding of $toName and $fromName is completely untested.
*/
class RouteBase{

/**
* Used by validatePassword().
*
* An array, where first element is the policy, and the remainder are parameters.
* Typically the second element is the minimum password length.
* Remember to make sure this policy is consistent with any validation happening
* on the front-end.
*
* @internal I decided to make this static, as a password policy it likely to be site-wide, not
*   vary from route to route. (And it might be easier to set a public static.)
*   Of course, typically only one or two routes deal with passwords anyway, so the difference
*   will be minor, either way.
*/
public static $passwordPolicy = array("simple", 6);

/**
* The name of the X-Mailer header to insert.
*
* Make sure this is header-ready (i.e. if exotic characters used you need base64_encode() it yourself, first).
* If you want no X-Mailer header, make this blank.
*/
public static $Mailer = "QQTrend API Mailer";

/**
* Send a plain-text email, using UTF-8.
*
* It has no return value, but will throw if some problem.
*
* NOTE: Make sure $subject, $toEmail and $fromEmail are set; this function will still send
* an email if they are not, but it will be bad.
*
* @internal One reason this exists is so unit tests can replace it more easily.
*
* @param string $toEmail We assume this has been validated. So it is up to the calling
*   code if commas are allowed to specify multiple recipients. (If each of those comma-separated
*   email addresses should have a name with it, then pre-encode that into $toEmail, and
*   leave $toName as null.)
* @param string $body The string to send as an email. It is sent as-is (in particular, it is not
*   trimmed, so any leading or trailing LFs will be sent).
* @param bool $isHTML If true then it expects $body to be in HTML (which should also contain a
*       UTF-8 meta tag).
*/
protected function sendUTF8Email($toName,$toEmail,$fromName,$fromEmail,$subject,$body, $isHTML = false){
$s= "=?utf-8?b?".base64_encode($subject)."?=";
$headers = "MIME-Version: 1.0\r\n";
if($fromName)$headers.= "From: =?utf-8?b?".base64_encode($fromName)."?= <".$fromEmail.">\r\n";
else $headers.= "From: ".$fromEmail."\r\n";
if(self::$Mailer)$headers.= "X-Mailer: ".self::$Mailer."\r\n";
//$headers.= "Reply-To: $reply\r\n";  
if($isHTML)$headers.= "Content-Type: text/html;charset=utf-8";    //NB. no \r\n, as it is the last header
else $headers.= "Content-Type: text/plain;charset=utf-8";    //NB. no \r\n, as it is the last header

if($toName)$to = "=?utf-8?b?".base64_encode($toName)."?= <".$toEmail.">";
else $to = $toEmail;

if(!mail($to, $s, $body, $headers))throw new ErrorException('_server_bad_email',array(),"Failed on call to mail(). to=$to; s=$s; headers=$headers; body=$body;");
}


/**
* This is like sendUTF8Email(), but expanded so it sends both plain text and HTML text together.
*
*
* NOTE: Make sure $subject, $toEmail and $fromEmail are set; this function will still send
* an email if they are not, but it will be bad.
*
* @see https://www.campaignmonitor.com/resources/will-it-work/ for instructions on what CSS and other
*   tags will work in HTML emails.  NOTE: make sure inline CSS is used. This means really inline, as gmail
*   apparently strips <style> sections out of both <head> and <body>. @see http://premailer.dialect.ca/ for
*   a free tool to do this job for you (and some others).
*
* @internal I considered having this function do some of the scaffolding around $htmlCopy, but
*   I think people will make their HTML as a full web page, then expect to just copy-and-paste the
*   whole thing (perhaps after running through a filter). So this wouldn't really help anything, and might
*   even cause more effort for users.
*/
protected function sendUTF8BothEmail($toName,$toEmail,$fromName,$fromEmail,$subject,$textCopy,$htmlCopy){
$mimeBoundary = md5(time()); 

$s= "=?utf-8?b?".base64_encode($subject)."?=";
$headers = "MIME-Version: 1.0\r\n";
if($fromName)$headers.= "From: =?utf-8?b?".base64_encode($fromName)."?= <".$fromEmail.">\r\n";
else $headers.= "From: ".$fromEmail."\r\n";
//$headers.= "Reply-To: $reply\r\n";  
if(self::$Mailer)$headers.= "X-Mailer: ".self::$Mailer."\r\n";
$headers.= "Content-Type: multipart/alternative; boundary={$mimeBoundary}";

if($toName)$to = "=?utf-8?b?".base64_encode($toName)."?= <".$toEmail.">";
else $to = $toEmail;

$body = "--{$mimeBoundary}\r\nContent-Type: text/plain; charset=\"utf-8\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n$textCopy";
$body.= "\n--{$mimeBoundary}\r\nContent-Type: text/html; charset=\"utf-8\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n$htmlCopy";
$body.= "\n--{$mimeBoundary}--\r\n";

if(!mail($to, $s, $body, $headers))throw new ErrorException('_server_bad_email',array(),"Failed on call to mail(). to=$to; s=$s; headers=$headers; body=$body;");
}


/**
* This is a high-level function for sending email responses to users.
*
* See email_user_readme.txt for documentation that can be used as the basis of
* instructions given to end users (the ones who make the email copy).
*
* @param string $emailTemplateDir This must end in a forward slash (unless doing something clever)
* @param string $label The base part of the filename we look for
* @param string $lang 'en', 'ja', etc.
* @param array $settings These are the strings to insert into the email. We also expect
*     the subject, fromemail and fromname defaults to be found in here. (The email copy is allowed
*     to modify them.)   NOTE: lowercase keys required. (If a setting is being ignored chances are
*     you gave a key with camelCase or uppercase.
*     NOTE: Keys must be A-Z,a-z,0-9,_- only.
*
* @todo I wonder if sendUTF8Email() and sendUTF8BothEmail() should take
*       $settings and get all their settings from there?
*       --> I.e. not much backwards-compatability to break, and it means extra headings could be added
*           quite easily. It also means error-checking could be moved into those functions (as we could output
*           settings and get quite useful error reporting).
*       ---> However, at this stage, it is a refactor, as the code already does everything we need it to.
*
* @internal We process text first, then HTML; the subject/from/etc. are expected to be defined in the .txt
*   file, but might be used in the html file.  (The downside is if subject/from/etc are in both then the ones
*   in the html file will dominate, which makes it harder to see when the two files go out of sync.)
*/
protected function sendTemplateEmail($emailTemplateDir, $label, $lang, $toName, $toEmail, $settings){
if(!$toEmail)throw new SystemException("toEmail is missing for email. label=$label");

$settings['toemail'] = $toEmail;
$settings['toname'] = $toName;

$txtFname = $emailTemplateDir.$label.'.'.$lang.'.txt';
$textCopy = $this->processEmailTemplate($txtFname, $settings);
$htmlFname = $emailTemplateDir.$label.'.'.$lang.'.html';
$htmlCopy = $this->processEmailTemplate($htmlFname, $settings);

$subject = @$settings['subject'];
$fromName = @$settings['fromname'];
$fromEmail = @$settings['fromemail'];

if(!$subject)throw new SystemException("Subject is missing for email. htmlFname=$htmlFname; txtFname=$txtFname; settings=".print_r($settings,true));
if(!$fromEmail)throw new SystemException("fromemail is missing for email. htmlFname=$htmlFname; txtFname=$txtFname; settings=".print_r($settings,true));

if(!$textCopy){ //Sending HTML-only
    if(!$htmlCopy)throw new SystemException("Neither text nor html copy given for label=$label. htmlFname=$htmlFname; txtFname=$txtFname; settings=".print_r($settings,true));
    $this->sendUTF8Email($toName,$toEmail,$fromName,$fromEmail,$subject,$htmlCopy, /*$isHTML=*/true);
    }
else{
    if(!$htmlCopy){ //Plain text only
        $this->sendUTF8Email($toName,$toEmail,$fromName,$fromEmail,$subject,$textCopy);
        }
    else{   //Both text and html
        $this->sendUTF8BothEmail($toName,$toEmail,$fromName,$fromEmail,$subject,$textCopy,$htmlCopy);
        }
    }
}


/**
* Helper for sendTemplateEmail()
*
* @param $fname The file to use. If it does not exist, "" is returned.
* @param Array $settings The values we insert. NOTE: lowercase keys expected.
* @return String The copy. NB. $settings may also have been modified.
*        The returned string will be trim()-ed. This is important as often you will have
*        lines at the top of the template file to set subject, from_name, etc. and when
*        these get removed they can leave behind a blank line. 
*
* @internal If {{key:value}} is given, but value is blank, we still allow the update (and if
*   it was a required string it will cause an error). I.e. better to cause an error than do the
*   wrong thing because a user accidentally deleted something or didn't understand what
*   a setting was for.
*/
private function processEmailTemplate($fname, &$settings){
if(!file_exists($fname))return "";
$s = file_get_contents($fname);

$s = preg_replace_callback('/{{([-A-Za-z0-9_]+):(.+?)}}/', function($matches) use (&$settings){
    $key = strtolower($matches[1]);
    $value = trim($matches[2]);
    switch($key){
        case 'subject':case 'from_name':case 'from_email':
            $settings[$key] = $value;
            break;
        //Ignore other values
        }
    return "";  //Always remove these ones from the copy
    }, $s);

$s = preg_replace_callback('/{{([-A-Za-z0-9_]+)}}/', function($matches) use (&$settings){
    $key = strtolower($matches[1]);
    if(!array_key_exists($key,$settings))return "";
    return $settings[$key];
    }, $s);

$s = preg_replace('/\xEF\xBB\xBF/','',$s);  //Remove UTF-8 BOM, if it somehow got inserted.
    
return trim($s);
}


/**
* Does various checks that the email address is okay.
*
* It throws if bad, so return means success.
*
* It does checkdnsrr(). If you don't want it to properly validate (e.g. in a unit
* test) then override it in your derived class.
*
* @internal filter_var() had some issues in 5.3.3 and 5.2.14. Also
*   the regex was improved in php 5.3. Try and use a version of PHP
*   after 5.3.3.
*
* @todo Does filter_var() make sure no commas in $email? If not we need
*   to do that ourselves.
*     ---> Add a unit test specifically for that.
*
* @internal This partly exists to make it easy to mock (as we don't always want to do checkdnsrr()).
*   But, of course, it is also useful code to share.
*
* @internal Note: email address not placed in error message if it fails the basic validation,
*   (e.g. it may be a clever hack attempt). However not a problem in error messages after that (i.e. by
*   that point we know it has no weird characters in).
*
* @todo (Can be future version): Two blacklists, one of email domains, the other
*   of specific (full) email addresses. They can be exact, or done as regexes.
*/
protected function validateEmail($email){
if(!filter_var($email, FILTER_VALIDATE_EMAIL))throw new ErrorException('_server_bad_email',array(),"email=$email, failed the basic filter");
$parts = explode("@",$email);$domain = array_pop($parts);
if (!checkdnsrr($domain, 'MX')) throw new ErrorException('_server_bad_email',"Passed basic filter, failed the DNS check (domain=$domain)");
}




/**
* Validate the given password, as strong enough.
*
* NOTE: this function should never have anything to do if the front-end validation is working. But
*   it is very important it is in sync with front-end code!
*
* Normally its behaviour is controlled by $passwordPolicy, but a derived class can override it
* for full control.
*
* The currently supported policies are:
*   simple: anything goes, just a minimum length check. E.g. "aaaa" satisfies simple4, "password"
*       satisfies simple8.
*   variety: cannot be all numbers, all lowercase, or all uppercase. No restrictions on characters.
*       "a123" satisfies variety4, "Password" satisfies variety8.
*
* Here is how to use jquery-validate for simple6 password:
        rules:{
			password: {
				//required: true,   //Done with HTML tag
				minlength: 6
                },
			password2: {
				//required: true,
				//minlength: 6, //Implied by equalTo
				equalTo: "#password"
                },
            ...
        messages:{
            password:{
                required:T("Please choose a password"),
                minlength:T("Your password must be at least 6 characters long")
                },
            password2:{
                equalTo:T("Please enter the same password as above"),
                },
            },
            ...
*
* (The T(...) is to translate error messages.)
*
* And here is how to implement the variety8 password policy:
        rules:{
			password: {
				//required: true,   //Done with HTML tag
                PWvariety:true,
				minlength: 8
                },
			password2: {
				//required: true,
				//minlength: 8, //Implied by equalTo
                //PWvariety:true, //Implied by equalTo
				equalTo: "#password"
                },
            ...
        messages:{
            password:{
                required:T("Please choose a password"),
                minlength:T("Your password must be at least 8 characters long")
                PWvariety:T("Password must not be all lowercase, nor all uppercase, nor all digits.")
                },
            password2:{
                equalTo:T("Please enter the same password as above"),
                },
            },
            ...
*
* Then you need to have defined this method:
    $.validator.addMethod("PWvariety", function(pw) {
        if(/^[A-Z]+$/.test(pw))return false;
        if(/^[a-z]+$/.test(pw))return false;
        if(/^[0-9]+$/.test(pw))return false;
        return true;
        });
*
* @todo Consider a new policy that uses a pw strength indicator, with a minimum value
*     See http://stackoverflow.com/a/1614890/841830
*     See http://bassistance.de/jquery-plugins/jquery-plugin-password-validation/
*
* @internal I considered a 2nd param, which would be the policy. But I decided a class variable
*   would be fine: it is unlikely a route ever needs to deal with two passwords, let alone two different
*   policies.
*/
protected function validatePassword($pw){
if(self::$passwordPolicy[0] == "simple"){
    if(strlen($pw) < self::$passwordPolicy[1])throw new ErrorException('Password is not long enough.');
    return;
    }
if(self::$passwordPolicy[0] == "variety"){
    if(strlen($pw) < self::$passwordPolicy[1])throw new ErrorException('Password is not long enough.');
    $msg = "Password must not be all lowercase, nor all uppercase, nor all digits.";
    if(preg_match('/^[A-Z]+$/', $pw))throw new ErrorException($msg);
    if(preg_match('/^[a-z]+$/', $pw))throw new ErrorException($msg);
    if(preg_match('/^[0-9]+$/', $pw))throw new ErrorException($msg);
    return;
    }
throw new SystemException("Unsupported password policy:".print_r(self::$passwordPolicy,true));
}



/**
* Sends back a captcha image
*
* It does its own content-type header; normally you want to exit after calling this function.
* Note that securimage.php uses PHP sessions.
*
* This default version uses securimage.php, which should already have been included.
* It customizes the charset to use only lowercase and numbers, with all confusing characters removed.
*
* Use this if you want to be only reject i,j,l,o,q,0,1
*   $securimage->charset = 'abcdefghkmnprstuvwxyz23456789';
*
* A derived class could also override this function to use a different captch mechanism.
*
* @internal If we wanted audio consider:
*   $securimage->outputAudioFile();  //Alternative for audio!
* @internal See securimage_show.php for some more customization options
*/
protected function showCaptchaImage(){
mt_srand(time());
$securimage = new \Securimage();
$securimage->charset = 'abcdefghkmnprtuvwxyz234679';  //lowercase and digits only, no i,j,l,o,q,0,1,s,5,8
$securimage->perturbation=0.5; //Nicer than the default of 0.75
$securimage->font_ratio = 0.50; //As we use lowercase, make bigger than default of 0.4
//$securimage->line_color=new Securimage_Color('#ff0000');
//$securimage->num_lines = 2; //Only 2 lines on top
//$securimage->use_transparent_text = true;$securimage->text_transparency_percentage = 50;
$securimage->show();   //Does the content headers too
}


/**
* The complement to showCaptchaImage().
*
* If using securimage.php, this default version should be fine.
* If not using captcha at all, override this to just return, and do nothing.
* NOTE: it assumes your html form has put the captcha code into "captcha_code".
*/
protected function checkCaptcha(){
$securimage = new \Securimage();
$code = Application::get('captcha_code','');
if(!$code || $securimage->check($code)===false)throw new ErrorException('Bad captcha.',array(),"Gave '".$code."' Here is full session:'".print_r($_SESSION,true)."'");
}



}   //End of class RouteBase

?>