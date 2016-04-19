<?php
/**
* These are functions that scheduling can built upon, as they deal with
* the planned availability of a resource.
*
* NOTE: all functions here assume the PHP timezone is UTC. I.e. date() will always
* return the same string as gmdate().
*
* @copyright QQ Trend Ltd, 2014, 2015
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/


namespace QQAPI;

include_once "timezone.inc";
include_once "exception.inc";

/**
 * Use a custom exception type, so it can be handled specially if needed.
 *
 * @todo Surely we can re-use one of the QQ exception classes (i.e. for the detailedMessage bit).
 *      I.e. we could just extend it, but not add anything.
*/
class evaluateAvailabilityObjectException extends \Exception{
/** 
* Extra information about the problem.
* Can be a string or an array.
*/
public $detailedMessage;

/** */
public function __construct($message,$detailedMessage="") {
parent::__construct($message);
$this->detailedMessage=$detailedMessage;
}

};



/**
* @param Array $byWeekday Keys for sun..sat. Must be lowercase, three characters. Note: "thu", not "thur"
* @param Array $exceptionDays Keys in YYYYMMDD form. Can be an empty array.
* @param String $timezone The timezone the above data is for.
*        One of the entries returned by timezone_identifiers_list().
*        E.g. "Asia/Tokyo" or "America/New_York", etc.
*        @todo Might not be working correctly with timezones using daylight-saving.
* @param String $startDate The first day to calculate for. (Any format that strtotime() can read.)
*        NOTE: in this version we expect this to always be today.
*        This is mainly here for unit test controlability at the moment.
* @param Number $maxDays How many days in the future to calculate for.
*        This only needs to be far enough in the future for the max possible allowed search or booking.
*        The shorter the better: less CPU effort, less storage needed, less bandwidth to pass it around.
* @param String $lastDayConsidered Output parameter: it is set to the last day we
*         did the calculation for. This could be considerably after the last entry in $summary (e.g.
*         if byWeekdays is not used, and just one or two cherry-picked entries in exceptionDay).
* @return Array The values are alternating open/close times.
*        The first one is always the first opening time. (If in the future it implies it is shut currently;
*        if it is in the past, then it does not really matter.)
*        So [t1,t2,t3,t4] means open from t1..t2, closed from t2..t3, open from t3..t4, then closed
*        from t4 until the end of time.
*        If there are no opening times at all, then a blank array is returned.
*        NOTE: on error an exception is thrown.
*
* @internal In future to support other timezones (and summertime), we need
*   to calculate the timezoneAdjust . @see http://www.php.net/manual/en/function.timezone-offset-get.php
*
* @todo As we return something called "summary", rename to summarizeAvailabilityObject() ?
*
* @todo In future allow passing in startTimestamp and endTimestamp, and using them as
*   a range limit on what is created.  (And, at that point, get startDate from startTimestamp,
*   and calculate maxDays from endTimestamp - startTimestamp ---> No, only do that if
*   $maxDays is not set (or zero). If it is set, use the smaller range defined by both maxDays and 
*   endTimestamp.
*   ---> ALTERNATIVE: It is possible this function does not need to know about startTimestamp
*    and endTimestamp at all; the calling function could just use them to set $startDate and $maxDays?
*    ---> That misses out on the fact that startTimestamp contains a time part too. E.g.
*        if byWeekday says 0800-2200 for 'mon', but startTimestamp might be for 13:00 on
*        a Monday.
*/
function summarizeAvailabilityObject($byWeekday,$exceptionDays,$timezone,$startDate,
    $maxDays,&$lastDayConsidered){
if($maxDays<=2)throw new ErrorException("maxDays is $maxDays, but should be 3+");

ksort($byWeekday);  //Just for the next assert (key order does not matter for the rest of the function)
if(array_keys($byWeekday)!=array('fri','mon','sat','sun','thu','tue','wed'))throw new evaluateAvailabilityObjectException("bad_weekdays",$yWeekday);

$dayZeroTimestamp=strtotime($startDate);   //Gets the timestamp at midnight (in UTC at this stage)
if(!$dayZeroTimestamp)throw new ErrorException("Bad startDate: $startDate");

$summary=array();  //Becomes an array, storing pairs
$prevStart=null;$prevEnd=null;  //Set as we go, to allow merging overlapping regions.

for($day=0;$day<$maxDays;++$day){
    //Make a date object: we want a YYYYMMDD string, and we want a dow string.
    $t=$dayZeroTimestamp+($day*86400);
    $datestamp = date("Ymd",$t);
    $lastDayConsidered = $datestamp;

    $tzOffset = getTzOffset($timezone, $datestamp);

    if(array_key_exists($datestamp,$exceptionDays)){
        $csvList = $exceptionDays[$datestamp];
        }
    else{
        $dow = strtolower( date("D",$t) );    //"sun", etc.
        $csvList = $byWeekday[$dow];
        }

    if(!$csvList)continue;   //I.e. closed all day

    $result = parseMultipleTimeRange($csvList);
    if(!is_array($result)){  //E.g. $result == "bad_format"
        throw new evaluateAvailabilityObjectException($result,array($datestamp,$day,func_get_args()));
        }

    //This line turns the midnight-offsets into times in UTC
    array_walk( $result, function(&$value,$key,$dayStart){
        $value += $dayStart;
        }, $t - $tzOffset );

    if(!$summary){
        $summary=$result;   //First entries
        $prevStart=$result[count($result)-2];
        $prevEnd=$result[count($result)-1];
        }
    else{   //Consider them one pair at a time
        for($ix=0;$ix<count($result);$ix+=2){
            $start=$result[$ix];
            $end=$result[$ix+1];
            if($start==$end){
                //Special case, use to mark a complete day off (in the exception list)
                continue;
                }
            assert($start<$end);  //Expected start/end pairs
            if($start < $prevStart){  //Today's starts before yesterdays! (Not impossible, e.g. if
                //"yesterday's" was actually "24:30-26:00", etc.)
                $summary[ count($summary)-2 ] = $start;
                $prevStart=$start;  //NB. $prevStart not changed.
                //TODO: There is one flaw here - if $start is actually overlapping the region before the "last" one!!
                //   This is possible, but only in a psychotic kind of way. The fix requires a loop to keep going back
                //   and back.
                if($end>$prevEnd){  //The end is past the end, so we also need to extend
                    $summary[ count($summary)-1 ] = $end;
                    $prevEnd=$end;  //NB. $prevStart not changed.
                    }
                //else no need touch $summary[ count($summary)-1 ] or $prevEnd.
                }
            elseif($start <= $prevEnd){    //Overlap (or touching) with previous entry. Join them together.
                $summary[ count($summary)-1 ] = $end;
                $prevEnd=$end;  //NB. $prevStart not changed.
                }
            else{   //No overlap
                $summary[]=$start;
                $summary[]=$end;
                $prevStart=$start;
                $prevEnd=$end;
                }
            }
        }    
    }

return $summary;
}


/**
* Parse a csv list of HH:MM-HH:MM pairs.
*
* Note: touching/overlapping regions are allowed, and they get merged into one.
*
* @param String $csvList
* @return Mixed On error String One of the parseTimeRange() codes, or one of:
*         out_of_order
*         overlap
*      On success: Array of offset pairs, in seconds after midnight.
*      E.g. "0000-0100,0200-0400" will return array(0,3600,7200,14400)
*
* @internal Originally overlapping, or out of order, time ranges, were not allowed.
*   But now we use two passes, and sort after the first pass, we no longer have this restriction
*/
function parseMultipleTimeRange($csvList, $overlapAllowed = true){
$list=array();  //Key is start time, value is end time. When a duplicate key we choose higher end time. Times are
    //in minutes since midnight
foreach(explode(',',$csvList) as $range){
    $d=parseTimeRange($range);
    if(is_string($d))return $d;
    assert(count($d)==2);
    list($start,$end)=$d;
    if(array_key_exists($start,$list))$list[$start] = max($list[$start], $end);
    else $list[$start]=$end;
    }
ksort($list,SORT_NUMERIC);

$lastValue=null;
$results=null;
foreach($list as $start=>$end){
    if(!$results){
        $results=array($start,$end);
        $lastValue = $end;
    }else{
        if($start > $lastValue){   //Note: equals does not count as a problem.
            $results[] = $start;
            $results[] = $end;
            $lastValue = $end;
            }
        elseif($start < $lastValue && !$overlapAllowed)return "overlap";
        else{  //When adjacent/overlapping, turn them into one longer period
            //By using max() we also cope with the case of 0000-1800,1500-1600
            $results[ count($results) - 1 ] = $lastValue = max($end,$lastValue);
            }
        }
    }

return $results;
}


/**
* Parse HH:MM - HH:MM   (colons are optional, as is whitespace)
*
* Validate the second time comes after the first.
* HH can be 00..29. MM can be 00..59. (Both start and end times can be
* after midnight - this might be most natural for certain venues.)
*
* @return Mixed String with error code if a problem. These errors:
*      "bad_format": Not numbers, bad characters, whitespace in wrong place, etc.
*      "backwards": Second time is before first time.
*      "equal": Second time is same as first time.
*      "bad_first": Start time is out of range
*      "bad_second": End time is out of range
*    On success: Array:
*      [0]: start time in seconds since midnight
*      [1]: end time in seconds since midnight
*    NOTE: as the latest valid time is 29:59, and times cannot be the
*    same, the highest possible return range is "29:58-29:59", which
*    will return array(107880,107940)
*/
function parseTimeRange($s){
if(!preg_match('/^\s*(\d{2}):?(\d{2})\s*-\s*(\d{2}):?(\d{2})\s*$/',$s,$parts))return "bad_format";
$h1=(int)$parts[1];
$m1=(int)$parts[2];
$h2=(int)$parts[3];
$m2=(int)$parts[4];
if($h1==0 && $h2==0 && $m1==0 && $m2==0)return array(0,0);  //Special case
if($h1<0 || $h1>29 || $m1<0 || $m1>59)return "bad_first";
if($h2<0 || $h2>29 || $m2<0 || $m2>59)return "bad_second";
$t1=($h1*3600)+($m1*60);
$t2=($h2*3600)+($m2*60);
if($t1==$t2)return "equal";
if($t2<=$t1)return "backwards";
return array($t1, $t2);
}


/**
*
*
* Normal usage is to call this function repeatedly for a set of ranges, to build
* up a list of entries. Then at the end call extractFromSummaryOnGivenDays()
* using array_keys($days). @see makeSureRangesAreInsideAvailabilitySummary()
* which creates that typical usage.
*
* As example of the reason for this function, imagine we have these two entries
* in exceptionDays, in JST:
*          2013-12-10: 22:00-26:00
*          2013-12-11: 08:00-16:00
* Then if start/end are set to 2013-12-11 01:30..03:00 (if in JST) then $days
* will end up with just:
*     '20131211'
* And $summary, which is all done in UTC, will change from:
*   2013-12-10: 13:00-17:00; 2013-12-10 23:00 - 2013-12-11 07:00
* to:
*   2013-12-10: 13:00-18:00; 2013-12-10 23:00 - 2013-12-11 07:00
*
* @param Number $start,$end The range to consider (in UTC, secs since 1970)
* @param Array $summary Each pair of entries is a range of opening time. Again in UTC, unix time.
*       I.e. this is the data returned by evaluateAvailabilityObject().
* @param String $timezone @see extractDaysFromRange()
* @param Array $days @see extractDaysFromRange()
*
* @return Boolean
*      If it fits, no problems, then it returns true. $summary and $days are untouched.
*      If it does not fit, it returns false, and $summary (and $days) will have been modified.
*      If it comes after everything in $summary, then it behaves like not fitting, and
*      returns false and $summary has been altered (and $days).
*
* @todo UNIT TEST

What about if we have in summary:
   6hrs on each of  four day
  start/end covers the whole block of days 2..4, with something on either side.

(I.e. I'm wondering if it is really safe to return from inside the for() loop here once we get
the first match?)

* @internal It is okay to do array_splice() even when inside a for($ix...) loop, because
*   we will always return immediately,  without doing any more iterations.
*/
function makeSureRangeIsInsideAvailabilitySummary($start,$end,&$summary,$timezone,&$days){
$cnt = count($summary);
for($ix=0;$ix<$cnt;$ix+=2){
    $a=$summary[$ix];
    $b=$summary[$ix+1];
    if($start>$b)continue;
    if($start>=$a && $end<=$b)return true;  //Simple
    //If we reach here, we know $start<=$b, i.e. some overlap, but it does not
    //fit completely inside. We also know $start was not inside the previous period (or $ix==0).
    if($start<$a){
        if($end<$a){    //Need to insert a new period
            array_splice($summary,$ix,0,array($start,$end));
            extractDaysFromRange($start,$end,$timezone,$days);  //Pass in whole period
            return false;
            }
        //else Need to shift back the start
        extractDaysFromRange($start,$summary[$ix],$timezone,$days); //NB. We pass in just the
            //bit we have had to add.
        $summary[$ix]=$start;
        }
    if($end>$b){    //Need to extend the end
        extractDaysFromRange($summary[$ix+1],$end,$timezone,$days); //NB. We pass in just the
            //bit we have had to add.
        $summary[$ix+1]=$end;
        }
    return false;
    }
//If we reach here it means $start comes after the last entry in $summary
$summary[]=$start;
$summary[]=$end;
extractDaysFromRange($start,$end,$timezone,$days);  //Pass in whole period
return false;
}


/**
* Find which dates (in a given timezone) that a time range covers.
*
* Note: if the $end timestamp is at midnight on day XX, it does not count
* as being part of day XX, but day XX-1 instead. But 1 second past midnight
* counts as being in day XX.
*
* @param Number $start,$end Unix time (in UTC). These describe the range to consider.
*     If $start==$end, then just that point in time is considered.
*     If $end comes before $start, just $start is considered (not considered an error).
* @param String $timezone E.g. "Asia/Tokyo"
* @param Array $days The keys are YYYYMMDD strings. The values are dummy (boolean true).
*
* @todo This is actually very generic, could belong elsewhere.
* @internal When $start and $end are the same day, we set the same date in $days twice.
*    This causes no problems.
*
* @internal To support other timezones I think we need to get the offset for each $t in the inner loop!
*   I.e. the key test case would be when start and end are either side of a summertime change.
*   (But need to test both times in the year when it changes, as going in different directions.)
*   (Actually it is really hard to find a test case where getting the timezone offset for either the
*    first or last day wouldn't be good enough. But I guess if the time involved is 30 minutes before
*    or after midnight, then being one hour off could cause a day to be missed.)
*/
function extractDaysFromRange($start,$end,$timezone,&$days){
for($t=(int)$start;;$t+=86400){
    $tzOffset = getTzOffset($timezone, "@".$t);
    if($t>=$end)$date=date("Ymd",$end - 1 + $tzOffset);   //-1 is to avoid the midnight problem
    else $date=date("Ymd",$t + $tzOffset);
    $days[$date]=true;
    if($t>=$end)break;
    }
}


/**
* This is used to create the exceptionDays array, based on
* the ranges found in $summary, for a given set of days.
*
* Note: the entries in $days, and the keys put into $exceptionDays, will
* be in $timezone.
*
* @param Array $summary Start/end pairs (in unix time, UTC)
* @param String $timezone E.g. "Asia/Tokyo"
* @param Array $days The list of days to use. (You can get this with array_keys() on
*      $days, from a call to extractDaysFromRange().)
* @param Array $exceptionDays Entries are added or replaced here.
*      NOTE: if an entry is already there for a given day, it is replaced. The new value
*      should be either the same as, or completely covering, the existing value.
*
* @internal Note that we might end up with adding days like:
*     '20131225'=>'0800-1600, 2200-2400',
*     '20131226'=>'0000-0200,0800-1600, 2200-2400',
*     etc.
*  Where a human might have phrased them as:
*     '20131225'=>'0800-1600, 2200-2600',
*     '20131226'=>'0800-1600, 2200-2400',
*  But getting that sophisticated is not worth it here.
*
* @internal One piece of complexity is that we want to replace the entry in $exceptionDays,
*   if previously existing. But we also want to append to a csv list (when 2+ periods in one day).
*   That is why we write to $ED first, then copy those entries over at the end.
*
* @todo The getTzOffset() uses current time. This will go wrong if $summary crosses a daylight
*   savings boundary.
*/
function extractFromSummaryOnGivenDays($summary,$timezone,$days,&$exceptionDays){
$tzOffset = getTzOffset($timezone);
$ED=array(); //Our local version of $exceptionDays (then we merge into $exceptionDays at the end)
$cnt = count($summary);
for($ix=0;$ix<$cnt;$ix+=2){
    $a=$summary[$ix] + $tzOffset;
    $b=$summary[$ix+1] + $tzOffset;
    $startDate = date("Ymd",$a);
    $endDate = date("Ymd",$b);

    if($startDate == $endDate){ //Special case is easier
        if(in_array($startDate,$days)){
            $range = date("H:i",$a).'-'.date("H:i",$b);
            if(array_key_exists($startDate,$ED))$ED[$startDate] .= ','.$range;
            else $ED[$startDate] = $range;
            }
        //else not a day of interest. Note: usually the most common case.
        }
    else{
        for($t=$a;;$t+=86400){    //There may be multiple days involved, so need a loop
            $datestamp = date("Ymd",$t);

            if(!in_array($datestamp,$days)){    //Not a day of interest. (A common case.)
                if($t>=$b)break;    //Done enough
                continue;
                }
            if($startDate == $datestamp){
                $range = date("H:i",$a).'-24:00';
                if(array_key_exists($startDate,$ED))$ED[$startDate] .= ','.$range;
                else $ED[$startDate] = $range;
                }
            elseif($endDate == $datestamp){
                $range='00:00-'.date("H:i",$b);
                //NB. we simply filter out the case where $b is at midnight, so $endDate is not actually of interest.
                if($range!='00:00-00:00')$ED[$endDate] = $range;
                }
            else $ED[$datestamp] = '00:00-24:00';
            if($endDate==$datestamp)break;  //Done enough
            }
        }
    }

foreach($ED as $datestamp=>$s)$exceptionDays[$datestamp]=$s;
}



/**
* High-level example usage for makeSureRangeIsInsideAvailabilitySummary() and
* extractFromSummaryOnGivenDays().
*
* @param Array $list Each element is a 2-element sub-array, where [0] is start time and [1] is end time.
* @param $summary, $timezone, $exceptionDays @see extractFromSummaryOnGivenDays()
* @return Boolean True means everything in the $list was already contained in $summary.
*      (This implies neither $summary nor $exceptionDays will have been modified.)
*      False means at least one entry in $list was outside of $summary; it also means that
*      $summary and $exceptionDays will have been changed.
*
* @internal We ignore the return value from makeSureRangeIsInsideAvailabilitySummary() and
*  instead just see if we end up with any entries in $days at the end of the loop.
*/
function makeSureRangesAreInsideAvailabilitySummary($list,&$summary,$timezone,&$exceptionDays){
$days=array();
foreach($list as $pair){
    makeSureRangeIsInsideAvailabilitySummary($pair[0],$pair[1],$summary,$timezone,$days);
    }
if(count($days)==0)return true;
extractFromSummaryOnGivenDays($summary,$timezone,array_keys($days),$exceptionDays);
return false;
}



/**
* Takes a summery list and prints it out in a human-readable form
*
* @todo tzOffset will be wrong if the $summary range crosses a daylight savings bounday
*/ 
function availabilitySummaryAsString($summary, $timezone, $sep=';  '){
$s='';

$tzOffset = getTzOffset($timezone);

for($ix=0;$ix<count($summary);$ix+=2){
    $s.=gmdate("Y-m-d H:i:s",$summary[$ix] + $tzOffset);
    $s.='..';
    $s.=gmdate("Y-m-d H:i:s",$summary[$ix+1] + $tzOffset);
    $s.=$sep;
    }
return $s;
}



