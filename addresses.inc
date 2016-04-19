<?php
/**
* QQ API: A utility class for addresses and postcodes
*
* @copyright QQ Trend Ltd, 2015
* @author Darren Cook <darren@dcook.org>
* @license MIT
*/

namespace QQAPI;


/**
* This currently only handles jp (and hk).
*
* @param String $v The postcode
* @param String $country The two-letter country code. E.g."jp" or "uk".
*           For countries it has no support for, yet, the only
*           modification will be trimming it (and the only validation will
*           be that it is not blank).
* @param Bool $isRequired If true then it will throw if (after trimming) we have
*           a blank string.
* @return String The modified postcode.
*       Note: for "jp" postcodes it will be removing the hyphen, leaving just 7 digits.
*       For "hk" it always returns a blank string (even if non-blank string was given).
*       If an error it throws an exception (QQAPI\UserException)
*
* @internal This should have identical behaviour of the JS function (in QQ.i18n.js) of same name.
*
* @todo The JS file already handles UK postcodes, so update this function.
*/
function normalizeAndValidatePostcode($v, $country, $isRequired=true){
if($country=='jp'){
    $v = mb_convert_kana($v,"n");   //Could use "a" but we only care about numbers.
    $v = preg_replace('/[^0-9]/','',$v);    //Get rid of whitespace, the hyphen, and anything else
    if(strlen($v)!=3 && strlen($v)!=7)throw new QQAPI\ErrorException("postcode must be 7 (or 3) digits.");
    return $v;
    }

if($country=='hk')return '';    //Hong Kong does not use postcodes

//TODO: do UK processing

$v = trim($v);
if($isRequired && $v=='')throw new QQAPI\ErrorException("postcode not given");
return true;
}

