<?php
/**
* Functions related to Japanese language and/or Japan
*/

namespace QQAPI;

include_once "data.japanese_prefectures.inc";
include_once "data.japan_tokyo_ku.inc";


/**
 * Does some pattern matching, and lookups, on a free text address string to try and
 * get more structured data.
 * 
 * Note: it only fills in fields it finds there. E.g. if it finds 奥多摩市, which definitely
 * implies prefecture==13, it still only sets prefecture if 東京都 was found.
 *   @todo Perhaps have a flag to say if it should fill in implied information?
 *       ---> No, I'd rather anything like that was done with a wrapper function.
 * 
 * @param string $address The user search string. Can be in romaji (i.e. English) or Japanese.
 * @return Array These keys: (all optional)
 *    postcode   (7 digits, ascii, no spaces or hyphens)
 *    pref   1..47 number (e.g. 13 is Tokyo)
 *    city    The city, if followed by 市 (i.e. in Japanese)
 *    city_en   The city, if it was followed by "shi" or "-shi" (i.e. in romaji)
 *    ku      The ku
 *    ku_en   The ku, if it was followed by "ku" or "-ku" (i.e. in romaji)
 *    other   Array: Anything that didn't fit in the above. Ascii will have been lowercased.
 *           Each entry represents one split (based on spaces or common punctuation), and
 *           might be what is left over after matching city or ku.
 * 
 * @todo Lots of scope for optimizing this. E.g.
 *    * Detect if English or Japanese, and then only half the regexes are needed.
 * 
 * @internal Remember that Japanese addresses are often written with no spaces or punctuation. That
 *   is why we remove each match from $s, and then carry on, rather than go to the next address part.
 *   We also deliberately go in the order of: postcode, ken, city, ku, other, so that each can greedily match
 *   to the left of it.  (Doesn't matter in English where the punctuation will exist.)
 * 
 * @internal This function could've done an English to Japanese city lookup. But that would
 *   require a large list as a dependency; that would be undesirable in an application where
 *   it knows it is only ever dealing with Japanese. (Or where ja lookup for English is not needed.)
 *   So, instead, we return city_en, and a wrapper function can then do the lookups.
 *   The same is done for ku/ku_en.
 *   On the other hand, prefectures are looked up in both English and Japanese, because:
 *   a) we already have full lists; b) they are not that long; c) prefecture is returned as a
 *   code (which is a consequence of b).
 * 
 * @todo The next enhancement would be to try and recognize the chome-ban-go numbers.
 *    In English any of these:
 *      sugamo-2-5-4
 *      sugamo 2-chome 5-4
 *      sugamo 2 chome 5-4
 *      sugamo2-5-4
 *      sugamo2 5-4    (less common)
 *    I.e. the challenge is any combination of space or hyphen could be used as separator between
 *    the numbers, the "chome" is optional, and there may be no space before the first number.
 *    In Japanese, any of these:
 *      巣鴨２丁目５－４
 *      巣鴨２丁目５番４号
 *      巣鴨２－５－４
 *      巣鴨 ２丁目５－４
 *      巣鴨 ２－５－４
 *  　I.e. hypens or kanji between the numbers, but space also possible.
 *    Could be double width or single width hyphens.
 *    Digits could be single or double width.
 *   In Tokyo, I think they always follow this pattern, and the numbers will always be
 *   1 or 2 digits each. But outside of Tokyo, just two sections, and larger numbers are
 *   possible. E.g. 横瀬町横瀬４６２８−３
 * 　　---> This may be the difference between a city and a town? I.e. the 丁目 is not
 *      a number but a name and followed by 町 ?
 * 
 */
function analyzeAddressTextAssumingJapan($address){
global  $jpPrefectureNamesJa, $jpPrefectureNamesEn;
$ret = array('other'=>array());

$addressParts = preg_split('/[　、；\s,]+/u',strtolower($address));

//Consider each address part
foreach($addressParts as $s){
    if($s=='japan' || $s=='日本')continue;    //If given, just ignore. But only if in a segment
        //by itself. I.e. if inside another string it is likely to be e.g. 日本橋, or part of a building name, etc.

    //Postcode
    if(preg_match('/[〒]?([0-9０１２３４５６７８９]{3})[-−ー]?([0-9０１２３４５６７８９]{4})/u',$s,$postcode_parts)){
        $ret['postcode'] = mb_convert_kana($postcode_parts[1].$postcode_parts[2],"n",'utf8');        //"a" is numbers and letters to ascii versions, "n" is numbers only.
        $s = str_replace($postcode_parts[0],"",$s);
        }
    
    //Try prefecture
    for($prefId=1;$prefId<=47;++$prefId){
        $suffix = 'ken';if($prefId==13)$suffix="to";if($prefId==26 || $prefId==27)$suffix="fu";
        if(strpos($s, $jpPrefectureNamesJa[$prefId])!==false){    //ja match on prefecture name
            $s=str_replace($jpPrefectureNamesJa[$prefId],'',$s);    //Remove it from address string
            $ret['pref']=$prefId;
            }
        elseif($s == $jpPrefectureNamesEn[$prefId]    //en match on prefecture name. Exact match.
            || $s == $jpPrefectureNamesEn[$prefId].$suffix
            || $s == $jpPrefectureNamesEn[$prefId]."-".$suffix
            ){
            $ret['pref']=$prefId;
            continue 2; //It was a full match. No need for str_replace or carrying on.
            }
        if($s=='')continue 2;  //I.e. prefecture was only thing in $s, so go to next $addressParts
        }
        
    if(preg_match('/(.+?)[-]?(shi|city)$/u',$s,$cityMatch)){
        $ret['city_en'] = $cityMatch[1];
        continue;
        }
    if(preg_match('/(.+?)[-]?ku$/u',$s,$kuMatch)){  //NB. "xxx city" got caught by the above, so no point repeating it here
        $ret['ku_en'] = $kuMatch[1];
        continue;
        }
    if(preg_match('/^chome[\s-]?/u',$s)){ //If a sub-string starts with chome, join it to previous entry in other.
        if(count($ret['other'])>=1){
            $ret['other'][count($ret['other'])-1].="-".$s;
            continue;
            }
        //else fall through (likely it will be appended to other, as-is.
        }

    if(preg_match('/(.+市)/u',$s,$cityMatch)){
        $ret['city'] = $cityMatch[1];
        $s=str_replace($cityMatch[1],'',$s);    //Remove it from address string
        }
    if(preg_match('/(.+区)/u',$s,$kuMatch)){
        $ret['ku'] = $kuMatch[1];
        $s=str_replace($kuMatch[1],'',$s);    //Remove it from address string
        }
    if($s!='')$ret['other'][]=$s;  //If not found, append to the other list.
    }

return $ret;
}


/**
 * This expands analyzeAddressTextAssumingJapan() by doing lookups on
 * ku_en and city_en. Also, if ku/city not set (en or ja), it tries to find one
 * in the "other" fields.
 * 
 * It does this by using the given lists.
 * 
 * @todo I think this function should be split into sub-helper-functions, and
 *    then we take some parameter to describe which ones we use. (Or leave this
 *    function as a super try-everything function, and when efficiency is needed
 *    you can pick and choose from the sub-functions.)
 * 
 * @internal We do two checks against $jp23kuEn, one for -ku and one for -city.
 *    Because official English names are actually e.g. "Toshima City", not "Toshima ku".
 * 
 * @todo I wondered about having a $prefHint parameter. This is used if pref did not get set
 *   from the initial analysis, and could be used as a default. (E.g. we know the whole
 *   database being analyzed is Tokyo or Osaka, or it is a search and we know the user
 *   lives in Nagoya, or the last GPS data we got for them was Tokyo, etc.)
 */
function analyzeAddressTextAssumingJapanExtra($address){
global $jp23kuJa,$jp23kuEn;
$ret = analyzeAddressTextAssumingJapan($address);
if(array_key_exists('ku_en',$ret)){
    $ku_id = array_search($ret['ku_en'], $jp23kuEn);
    if($ku_id >=1 && $ku_id <=23)$ret['ku'] = $jp23kuJa[$ku_id];
    //else not an error, e.g. it might be a ku in another city.
    }
elseif(array_key_exists('city_en',$ret)){
    //TODO: next ought to only be done when pref==13 (or pref is unknown)
    $ku_id = array_search($ret['city_en'], $jp23kuEn);
    if($ku_id >=1 && $ku_id <=23)$ret['ku'] = $jp23kuJa[$ku_id];
    //TODO: try some per-prefecture city lookups here.
    //    (and if pref unknown, we'll have to try them all)
    }
    
if(!array_key_exists('ku',$ret)){
    //TODO: could try scanning the other fields for a ku match without the 区 suffix
    }
    
return $ret;   
}

