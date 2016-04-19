<?php
/**
* Tokyo 23-ku list
*
* See https://ja.wikipedia.org/wiki/東京都区部
* See https://en.wikipedia.org/wiki/Special_wards_of_Tokyo
*/

namespace QQAPI;

/**
* NB. They keys are 1 for Chiyoda-ku, through to 23 for edogawa-ku. This ordering
* comes from 全国地方公共団体コードの末尾.
*/
$GLOBALS['jp23kuJa']=array(
'',
'千代田区', //1
'中央区',
'港区',
'新宿区',  //4
'文京区',
'台東区',
'墨田区',
'江東区',  //8
'品川区',
'目黒区',
'大田区',
'世田谷区', //12
'渋谷区',
'中野区',
'杉並区',
'豊島区',  //16
'北区',
'荒川区',
'板橋区',
'練馬区',  //20
'足立区',
'葛飾区',
'江戸川区', //23
);

/**
* Translation of $jp23kuJa into English.
*
* Hepburn romaji, no long vowels shown, and lowercase.
* 
* The -ku suffix (they might also use " city" suffix") is not included.
*/
$GLOBALS['jp23kuEn'] = array(
'',
'chiyoda', //1
'chuo',
'minato',
'shinjuku',  //4
'bunkyo',
'taito',
'sumida',
'koto',  //8
'shinagawa',
'meguro',
'ota',
'setagaya', //12
'shibuya',
'nakano',
'suginami',
'toshima',  //16
'kita',
'arakawa',
'itabashi',
'nerima',  //20
'adachi',
'katsushika',
'edogawa', //23
);


?>
