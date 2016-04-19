<?php
/**
* Some functions to make compiling handlebars templates, on the fly, easier.
*
* Note: pre-compiling them, off-line, might be better.
* Note: designed for Handlebars 1.3.0. Need to analyze if later versions have broken
* anything or made anything redundant.
*
* @copyright QQ Trend Ltd, 2013, 2014
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;

/**
*
* @param String $path Where to look for the templates. Does not need to end in a forward slash.
* @param Array $list The names are added here.
* @param String $prefix Optional string to put at front of the name.
*         E.g. if loading templates from different sub-directories you might
*         want to use something here to get namespacing.
*
* @internal It is a function, rather than global code, to avoid
*   variable name clashes.
*
* @todo I wondered about a $shorten boolean parameter. If true then we'd change
*   name from the long name to e.g. a base26 (i.e. a..z - see the function below) string
*   based on the index in $list:
*       $name = num2alpha(count($list));    //I.e. returns "A" when 
*       $list[]=$name;

function num2alpha($n) {
    $r = '';
    for ($i = 1; $n >= 0 && $i < 10; $i++) {
        $r = chr(0x41 + ($n % pow(26, $i) / pow(26, $i - 1))) . $r;
        $n -= pow(26, $i);
    }
    return $r;
}
*/
function processHandlebarsTemplatesLoad($path,&$list,$prefix=''){
$dir=dir($path);
while( ($entry=$dir->read())!==false){
    if(!preg_match('/^([A-Za-z].+)\.hbs$/',$entry,$parts))continue;    //NB. must start with letter
    $name=$prefix.$parts[1];
    echo "<script id=\"{$name}-template\" type=\"text/x-handlebars-template\">";
    echo file_get_contents($path."/".$entry);
    echo "</script>\n";
    $list[]=$name;
    }
}


/**
*/
function processHandlebarsTemplatesCompile($list, $namespace="JST"){
//NOTE: the following gives us same structure as if we pre-compile them. 
echo "<script>var {$namespace}={};";
foreach($list as $name){
    echo "{$namespace}.{$name}=Handlebars.compile($('#{$name}-template').html());";
    }
echo "</script>\n";
}


/**
* Higher-level interface on processHandlebarsTemplatesLoad and processHandlebarsTemplatesCompile
*
* As well as loading all templates, it also makes all of them available as partials.
*/
function processHandlebarsTemplates($paths, $namespace="JST"){
$list=array();
foreach($paths as $path)processHandlebarsTemplatesLoad($path,$list);
processHandlebarsTemplatesCompile($list, $namespace);
echo "<script>Handlebars.partials = {$namespace};</script>"; //Make all templates also available as partials
}

