<?php
/**
* QQ API: Logging support
*
* @copyright QQ Trend Ltd, 2012, 2013
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

/**
* A class for writing to a log file.
*
* In future it might support other log destinations, but for the moment
* it only handles the local filesystem (with locking).
*
* One notable feature is the backup destination, which is used when
* something goes wrong.
*
* Another is that file-write problems get appended to the message.
*/
class Logger{
/**
* Where to log messages (one file per message) when something is
* wrong with the main logging destination.
*
* Timestamp, and a random number, get appended.
* Make sure web server (or whatever process is running this script) has
* write access to the given directory.
*/
public static $backupDestination = 'logs/emergency.';

/**
* Does the actual writing of given message to given logfile.
*
* NOTE: if we have write problems, we append them to $msg. We also
* append "---", on a line of its own, at the end.
*
* It uses file-locking (with the flock() function). (Therefore do not use this over NFS.)
* It will take a total of 2.8s to get a lock before giving up (if $max_tries=5 and $sleep_us=200000).
*
* If it fails to open the file, or fails to get the lock, it will instead create a logfile
* of the format:   emergency.NNN.XX.log
* NNN is the timestamp (14 digits: YYYYMMDDHHMMSS). XX is a random number.
*
* @internal I originally considered having it fallback on sending an email when it
*    could not log. But:
*        a) If a diskspace problem I'll rely on other scripts to detect that (and hopefully earlier)
*        b) If it goes crazy I'd rather deal with 10,000 tiny log files than 10,000 emails.
*        c) Following on from b), it it will always be less load.
*        d) A once/minute cron job to send out emails is easier to deal with. It could also
*            do a check that log files can be created.
*
* @internal Changed fopen mode from "at" to "ab" to satisfy unit tests (vfsStream) more
*   than anything else. It also makes more sense, as I'd rather just "\n" was stored even
*   when the code is run on Windows.
*
* @todo All logging ends up here, so $fname could also describe some other logging
*   destination, not just a file. E.g. "tcp://10.0.0.1/myapp"
*/
public static function log($fname,$msg,$maxTries=5,$sleepMicroseconds=200000){
$fp=@fopen($fname,"ab"); //NB. We also use fseek() below, to make sure we're at the end.
if($fp){
    for($tries=1;$tries<=$maxTries;++$tries){
        if($tries>=2)usleep($sleepMicroseconds*$tries);  //0.4s, 0.6s, 0.8s, 1.0s. (I.e. 2.8s before it gives up.)
        if(!flock($fp,LOCK_EX|LOCK_NB)){    //Returns false if someone else has a lock
            $msg.="+++Failed to get a lock. Will sleep and try again. tries=$tries.\n";   //TEMP - don't really need to know this
            continue;
            }
        //If we reach here, we have an exclusive lock on $fp. 
        $e=fseek($fp,0,SEEK_END);
        if($e!==0)$msg.="+++Failed to fseek to end of {$fname}\n";
        else{
            $e=fwrite($fp,$msg."---\n");
            if($e===false)$msg.="+++Failed to fwrite at end of {$fname}\n";
            else{   //Success
                fclose($fp);    //Also releases the lock
                return;
                }
            }
        break;  //Don't try to get the lock again. Just give up.
        }
    fclose($fp);    //Also releases the lock (if we got one)
    }
else $msg.="+++Failed to open {$fname} for appending\n";

//If we reach here then either fopen failed, or flock failed.
$fname=self::$backupDestination.date('YmdHis').'.'.rand(100,999).'.log';
$fp=fopen($fname,"wb");
if(!$fp)return; //We'll have to just give up.
fwrite($fp,$msg."---\n");
fclose($fp);
}

}


?>