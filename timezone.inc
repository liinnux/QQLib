<?php
/**
* QQ API: A utility class for timezones
*
* @copyright QQ Trend Ltd, 2015
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;


/**
* @param string $when Something that strtotime() can interpret
*   (@see http://php.net/manual/en/datetime.formats.php )
*/
function getTzOffset($timezone, $when = "now"){
$dateTimeZone = new \DateTimeZone($timezone);
$UTC = new \DateTimeZone("UTC");
$dateInUTC = new \DateTime($when, $UTC);
$tzOffset = $dateTimeZone->getOffset($dateInUTC);
if($timezone == "Asia/Tokyo")assert($tzOffset == 9*3600);   //TODO: only valid after 1951, actually.
return $tzOffset;
}




/**
* @internal Based on code found in comments at http://php.net/manual/en/function.timezone-offset-get.php
*  NOTE: you generally want to give UTC as the first parameter, and your timezone of interest
*	as the second, if you want to find out how many seconds you are ahead of UTC.
*   ---> Well, might as well use getTzOffset() in that case.
*/
function get_timezone_offset($remote_tz, $origin_tz, $when = "now") {
$origin_dtz = new \DateTimeZone($origin_tz);
$remote_dtz = new \DateTimeZone($remote_tz);
$origin_dt = new \DateTime($when, $origin_dtz);
$remote_dt = new \DateTime($when, $remote_dtz);
$offset = $origin_dtz->getOffset($origin_dt) - $remote_dtz->getOffset($remote_dt);
return $offset;
}




/**
* Uses an address to try and decide the timezone.
*
* Currently it only bases its decision on country, so can be wrong for 'ca', 'us' and any
* other countries that cover multiple timezones.
*
* Note: this function has nothing to do with daylight savings: the returned string can be
* given to get_timezone_offset(), or similar, along with a date, to get that.  
*
* @param Mixed $address If an array, then it must have at least a 'country' element. (Actually, in future,
*       if there is just lat/long, that could be sufficient?) The country is the two letter iso code.
*       If it is a string, then it is assume to be a country code.
* @return String One of the entries returned by timezone_identifiers_list().
*       E.g. "Asia/Tokyo", "Europe/London", "UTC"
*       On any problem we return "UTC".
*
* @see http://download.geonames.org/export/dump/
*    The big countries.zip file contains all the answers to make this function
*    comprehensive.
*    Also http://en.wikipedia.org/wiki/List_of_time_zones
*    and http://www.twinsun.com/tz/tz-link.htm  (Olson codes)
*
* @todo I suppose we could support object in future, looking at $address->country.
*
* @internal I initially considered taking more fields. E.g. $lat/$long, or $state.
*     Actually U.S. state is not enough, see: http://www.timetemperature.com/tzus/indiana_time_zone.shtml
*     However state + county should always be enough. (And will be more reliable than lat/long)
*	 This is why we decided to take an associative array of address, for fullest flexibility.
*/
function decideTimezone($address){
if(is_array($address))$country = @$address['country'];
elseif(is_string($address))$country = $address;
else return "UTC";  //Unsupported data type

switch($country){
    case 'jp':return 'Asia/Tokyo';break;
    case 'sg':return 'Asia/Singapore';break;
    case 'hk':return 'Asia/Hong_Kong';break;
    case 'zh':return 'Asia/Shanghai';break;  //Note: "Beijing" not in DB! TODO use other info?
    case 'in':return 'Asia/Calcutta';break;
    case 'uk':return 'Europe/London';break;
    case 'fr':return 'Europe/Paris';break;
    case 'de':return 'Europe/Berlin';break;
    case 'ca':return 'America/NewYork';break;    //TODO: need more information!! I.e. state. or lat/long
    case 'us':return 'America/NewYork';break;    //TODO: need more information!! I.e. state. or lat/long
    }
return "UTC";
}


