<?php

/**
* @return string A big int of milliseconds since 1970.
*
* @todo An int version too? Do we need one? (If not, just show here how it is done, for future reference.)
*/
function timems_string(){
$d=gettimeofday();
$timestamp=$d['sec'];
$timestamp.=sprintf("%03d",floor($d['usec']/1000)); //I.e. add three more digits for milliseconds
return $timestamp;
}

/**
* Like timems_string() but it subtracts the given number of seconds.
*
* @param Number $delay_secs The number of seconds to go back in time.
*          NOTE: in seconds, not milliseconds.
* @return string A big int of milliseconds since 1970.
*/
function timems_delayed_string($delay_secs){
$d=gettimeofday();
$timestamp=$d['sec']-$delay_secs;
$timestamp.=sprintf("%03d",floor($d['usec']/1000)); //I.e. add three more digits for milliseconds
return $timestamp;
}

/**
* This takes number of ms since 1970 (as a string) and
* returns a timestamp in the format:
*     "YYYY-MM-DD HH:MM:SS.sss ZZZ"  (where ZZZ is always "UTC")
*/
function timems_format_bigint($t){
if(strlen($t)<=3){  //Special handling for first second after midnight on Jan 1st 1970 GMT  
    $secs=0;$ms=sprintf("%03d",$t);
    }
else{
    $secs=substr($t,0,-3);$ms=substr($t,-3);
    }
return gmdate("Y-m-d H:i:s",$secs).'.'.$ms.' UTC';
}


/**
* Helper for xml_encode; calls itself recursively.
*
* @param Object $writer XMLWriter $xml
* @param Array/Object $results
*/
function xml_encode_write($writer,$results){
foreach($results as $key => $value){
    if(is_numeric($key))$key='n'.$key;
    if(is_array($value)){
        $writer->startElement($key);
        xml_encode_write($writer, $value);
        $writer->endElement();
        }
    else $writer->writeElement($key, $value);
    }
}

/**
* This is quite straightforward use of XmlWriter. The top-level
* XML tag is <response>
*
* NOTE: it is important that all the keys are already valid XML elements.
* I.e. they must not start with a number or a punctuation character (underline is okay though).
*
* @param Mixed
*    If array, with numeric keys: the children nodes will be <n0>, <n1>, ...
*    If array, with string keys: the keys are used as the children element names
*    If object, the member variables are used as the children element names
*    If string or number, a single child element of <result> will be used.
*    If anything else it will be treated like string, but may not work correctly.
*
* @see http://snippets.dzone.com/posts/show/3391
* @see frapi: library/Frapi/Output/XML.php uses basically the same code.
*
* @todo Version hard-coded as 1.0
*/
function xml_encode($results){
$version='1.0';
$writer = new XmlWriter();
$writer->openMemory();
$writer->setIndent(true);   //Not just indent but also LF at the end. Default indent seems to be one space.
$writer->startDocument($version,'UTF-8');
$writer->startElement('response');
if(is_array($results) || is_object($results))xml_encode_write($writer,$results);
else $writer->writeElement('result', $results);
$writer->endElement();
$writer->endDocument();
return $writer->outputMemory(true);
}


/**
* Used for simple success messages
* 
* @param Bool $success
*/
function xml_success_result($success){
$version='1.0';
$writer = new XmlWriter();
$writer->openMemory();
$writer->setIndent(true);   //Not just indent but also LF at the end. Default indent seems to be one space.
$writer->startDocument($version,'UTF-8');
$writer->startElement('response');
$writer->writeAttribute('success',$success?'Y':'N');
$writer->endElement();
$writer->endDocument();
return $writer->outputMemory(true);
}

/**
* @param String $format 'json', 'xml', or default is print_r of a string that is "Y" or "N".
* @param Bool $success
*
* @todo What about other formats? "jsoncsv", "xmlcsv"?
*
* @internal Originally I had a carriage-return after the json. However PHP's own
*    json_decode cannot interpret "true\n"! And, on reflection, there is no real need
*    for the carriage-return.
*    (For actions that do multi-line output, such as GetHistory and Stream, they will
*     have to do their own carriage-return... but they don't generally output bool responses.)
*/
function formatBooleanResult($format,$success){
if($format=='json')return json_encode($success);   //Outputs just "true" or "false"
if($format=='xml')return xml_success_result($success);
return print_r($success?'Y':'N',/*output=*/true);  //Wrapped in a string, otherwise false outputs nothing at all.
}


/**
* @internal From http://stackoverflow.com/a/7225486/841830
*      This is the best answer for simple (non-associative) arrays, as it renumbers the index.
*   I changed the naming, and made it modify the incoming array.
*/
function array_simple_remove_value(&$array, $value){
$array = array_values(array_diff($array, array($value)));
}


?>