<?php
/**
* QQ API: This builds on the availability objects concept to add scheduling support.
*
* The basic ideas are:
*     Resource availability (handled by the functions in availability.inc)
*     A resource
*     An event  (i.e. a period of time when a resource is being used)
*
* E.g. for a library, the resources are books, the events are periods of borrowing.
* E.g. for a meeting room, the resource is the room, the events are periods when the room is
*       booked for a meeting.
*
* There is currently no consideration about partial usage: a resource can be in one
* of 3 states at any point in time:
*       - fully used (i.e. has an event)  [book is out] [room is booked]
*       - fully available (no event)   [book is in library]  [room not booked]
*       - unavailable (no event, but not available)   [library is closed] [office is closed]
*
* The distinction between 2 and 3 is what availabilty.inc is about. The distinction
* between 1 and 2 is what the functions in this file are about.
*
* We use the term bookings to describe events, as it makes it a bit clearer.
*
* @copyright QQ Trend Ltd, 2014
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;
include_once "availability.inc";

/**
* This merges the availability object information of a given resource
* with the list of bookings for that resource.
* 
* It effectively takes $summary, and reduces some of the available time.
*
* @param Array $summary This is the data returned by summarizeAvailabilityObject().
*       I.e. an array of time pairs, for the start/end time of each available time.
* @param Array $bookings Each is an associative array with at least these elements:
*       'id', 'start', 'end'
*       NOTE: we cope with it being in any order, and containing bookings in the
*       past. Use makeScheduleFromAvailabilityAndFutureSortedBookings() if
*       $bookings is already sorted, and all items in future.
* @return Array Almost the same format as $summary, but we return triples instead
*       of pairs. I.e.    start, end, bookingId
*     (where bookingId is null for an "available" period)
*
* @internal Adjoining periods are not merged, unlike with the availability in $summary,
*   because booking IDs are always unique. I.e. there never are adjoining periods, in
*   that sense. (The exception is when describing periods of availability and the booking
*   ID is null. In that case adjoining periods with null should never exist, because
*   they won't have existed in $summary, either. However that is not guaranteed by
*   the code here.)
*/
function makeScheduleFromAvailabilityAndBookings($summary, $bookings, $now=null){
if(!$now)$now = time();
$futureBookings = array();
foreach($bookings as $B){
    $s = $B['start'];
    $e = $B['end'];
    if($e <= $now)continue;  //Skip old bookings
    $futureBookings[] = $B;
    }
usort($futureBookings, function($Bx,$By){   //Sort by start time, earliest first
    return $Bx['start'] > $By['start'];
    });
return makeScheduleFromAvailabilityAndFutureSortedBookings($summary, $futureBookings);
}

/**
* Does hard work for makeScheduleFromAvailabilityAndBookings()
*
* @todo I think I could be taking better advantage of the knowledge that the bookings
*   are in sorted order. (E.g. if($s >= $b) could be break instead of continue.)
*/
function makeScheduleFromAvailabilityAndFutureSortedBookings($summary, $bookings){
$d=array();

for($ix = 0;$ix < count($summary);){
    $a = $summary[$ix++];   //Start of availabilty
    $b = $summary[$ix++];   //End of availability

    $next = $a;

    foreach($bookings as $B){
        $s = $B['start'];
        $e = $B['end'];
        $id = $B['id'];
        assert($e > $s);assert($id>0);
        if($e <= $a)continue;   //Booking is before this period of availability
        if($s >= $b)continue;   //Booking is after this period of availability

        //If we reach here, this booking is in this availability period
        if($s < $next || $e > $b)throw new SystemException("Booking falls outside availability period"); //Should be impossible. I.e. data inconsistency. (might mean $bookings wasn't actually sorted?)

        if($s != $next){    //There is a gap, where it is not booked
            $d[] = $next;$d[]=$s;$d[]=null;
            }
        $d[] = $s;$d[] = $e;$d[] = $id;
        $next = $e;
        }

    if($next<$b){   //There is an unbooked period at the end
        $d[] = $next;$d[] = $b;$d[] = null;
        }

    }

return $d;
}


/*
* @todo Need an incremental update function. I.e. we have a new
*    booking, and an existing $schedule object.
*    The new booking has to go in and insert new items in the array.
*    --> Ideally the same function would also do the incremental update
*       of $timeSlices.
*    ----> Well, two functions probably makes more sense, as they carry
*       very little in common??
*/


/**
* The time slices are time periods followed by a list of resource IDs. This adds
* the current resource id to each time slice where it has partial or full
* availability.
*
* @internal NOTE: if the time slices are chosen to be the same size as
*   bookingDurationGranularityInSecs (which is also assumed to
*   be bookingStartGranularityInSecs), then there is never the concept
*   of partial: a resource is either completely unavailable for a slice
*   or completely available for it.
*   @todo Does this function become easier to implement if we
*       assume that?
*       --> I wonder if we need to be taking those two numbers as parameters?
*           Then we could validate against them, but also perhaps optimized based on them.
*
* @param Array $timeSlices A simple array (but in sorted time order)
*   where each element is a sub-array with at least two entries.
*   [0] is the start of the time slice, [1] is the end of the time slice
*   [2] onwards are resource IDs.
* @param Number $rid The resource that $schedule is for.
* @param Array $schedule This is the return data from makeScheduleFromAvailabilityAndBookings()
*    I.e. a single array of sets of three pieces of data: start, end, bookingId, where bookingId
*    is null to indicate availability.
*
* @internal This function should be able to cope with time slices of different length.
*   All it requires is that the time slices are in time order, otherwise it will go very weird.
*  (We could have it work if $timeSlices was in random order, but it would require
*   that we loop through the whole of $schedule for every time slice, which would
*   require much more CPU.)
*/
function addResourceAvailabilityToTimeSlices(&$timeSlices, $rid, $schedule){
$ix = 0;    //Where we've looked up to in $schedule
$lastIx = count($schedule);
foreach($timeSlices as $n => $slice){
    $s = $slice[0];
    $e = $slice[1];

    for(;$ix<$lastIx;$ix+=3){
        $a = $schedule[$ix];
        $b = $schedule[$ix+1];
        if($schedule[$ix+2] === null){  //I.e. if this is an available block in the schedule.
            //NB. the "<" and ">", not "<=" and ">=" are deliberate and important.
            if( ($a < $e)   //The start of this block come before end of this time slice.
                && ($b > $s)    //The end of this block came after start of this time slice.
                ){
                $timeSlices[$n][] = $rid;
                break;  //An available block can cover 2+ time slices, so try this block
                    //on the next time slice. (I.e. don't increment $ix)
                }
            }
        //else it is a booked period, and we don't care about it
        if($b > $e)break;   //This will be part of the next time slice
        }
    
    if($ix >= $lastIx)break;    //We've finished processing all the resource's availability objects, so no point
        //looking at any remaining time slices.
    }

}


/**
* Prepare the $timeSlices object, ready to use with addResourceAvailabilityToTimeSlices().
*
* @param Number $startTime In secs. This will become the start time of the first slice
* @param Number $endTime In secs. This will become the end time of the last slice. (unless
*       $endTime - $startTime is not an exact multiple of $sliceLengthInSecs).
* @param Number $sliceLengthInSecs
*
* @todo We now have two functions taking &$timeSlices as a reference
* parameter. This suggests we should be refactoring into a class.
*
* @internal We take $timeSlices as a parameter, so that we could add blocks
*  of different sliceLength. E.g. 15 minute blocks for the next 48 hours, then 6 hour
*  blocks beyond that.
*/
function scheduleInitializeTimeSlices(&$timeSlices, $startTime, $endTime, $sliceLengthInSecs){
for($t=$startTime; $t<$endTime; $t += $sliceLengthInSecs){
    $timeSlices[] = array($t, $t+$sliceLengthInSecs);
    }
}


/**
 * Organizes the schedule for a single resource
 * 
 * @param Array $resource The object describing the resource. It is expected to at least
 *       have these keys: id, availabilityId.
 *       The availabilityId tells us the available times for that resource.
 * @param Array $bookingsForOneResource All bookings for $resource
 * @param Array $availabilityObjects All availability objects. $resource['availabilityId'] tells
 *      us which one to use.
 *
 * @todo REFACTOR: how about change $resource to be $rid (i.e. just the ID), and pass in
 *      $availabilityObjects[$resource['availabilityId']] rather than all of them?
 *     --> However that just leaves the last 2 lines of this function, so is it worth it?!
 * @todo The final line here could be
 *       makeScheduleFromAvailabilityAndFutureSortedBookings()
 *      if $bookingsForOneResource has already been filtered and sorted.
 *      (Just quicker, no other reason.)
 */
function makeScheduleForOneResource(&$timeSlices, $resource, $bookingsForOneResource, $availabilityObjects){
$availabilityId = $resource['availabilityId'];
if(!array_key_exists($availabilityId, $availabilityObjects)){
    throw new SystemException("AvailabilityId ($availabilityId) of a resource has no corresponding availabilityId. Here are all availabilityObjects:".print_r($availabilityObjects,true));
    }

$availability = $availabilityObjects[$availabilityId];
$schedule = makeScheduleFromAvailabilityAndBookings($availability[0]['summary'], $bookingsForOneResource);
addResourceAvailabilityToTimeSlices($timeSlices, $resource['id'], $schedule);
}





/**
 * A function to help with troubleshooting the return of makeScheduleForVenue()
 *
 * Time ranges will be in the given timezone; they are assumed to be in UTC in $d.
 *
 * @param $resourceName You can make the output more understandable for the application, by changing
 *     this to "books", "rooms", "cars", etc.
 */
function timeSlicesAsString($d, $timezone, $resourceName = "resources"){
$tzOffset = getTzOffset($timezone);
$s='';
foreach($d as $label => $ranges){
    $s.=$label."\n";
    foreach($ranges as $d){
        $s.=gmdate("Y-m-d H:i:s",$d[0] + $tzOffset) . " - " . gmdate("Y-m-d H:i:s",$d[1] + $tzOffset);
        $s.= ": ".(count($d)-2)." ".$resourceName;
        $s.=" ({$d[0]} - {$d[1]})\n";
        }
    }
return $s;
}

