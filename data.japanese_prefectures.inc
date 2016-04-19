<?php
/**
* Japanese prefecture lists
*
* Based on data found in fclib.
*
* See http://en.wikipedia.org/wiki/ISO_3166-2:JP
*/

namespace QQAPI;

/**
* NB. They keys are 1 for hokkaido, 2 for aomori, etc. 0 is the blank string.
*/
$GLOBALS['jpPrefectureNamesJa']=array(
'','北海道','青森県','岩手県','宮城県','秋田県','山形県','福島県','茨城県','栃木県',    //1..9
'群馬県','埼玉県','千葉県','東京都','神奈川県','新潟県','富山県','石川県','福井県','山梨県',    //10..19
'長野県','岐阜県','静岡県','愛知県','三重県','滋賀県','京都府','大阪府','兵庫県','奈良県','和歌山県',   //20..30
'鳥取県','島根県','岡山県','広島県','山口県','徳島県','香川県','愛媛県','高知県','福岡県','佐賀県', //31..41
'長崎県','熊本県','大分県','宮崎県','鹿児島県','沖縄県');   //42..47

/**
* Translation of $jpPrefectureNamesJa into English.
* 
* The -ken, -to, -fu suffix is not included
*/
$GLOBALS['jpPrefectureNamesEn'] = array(
'', 'hokkaido', 'aomori', 'iwate', 'miyagi', 'akita', 'yamagata', 'fukushima', 'miyagi', 'tochigi',
'gunma', 'saitama', 'chiba', 'tokyo', 'kanagawa', 'nigata', 'toyama', 'ishikawa', 'fukui', 'yamanashi',
'nagano', 'gifu', 'shizuoka', 'aichi', 'mie', 'shiga', 'kyoto', 'osaka', 'hyogo', 'nara', 'wakayama',
'tottori', 'shimane', 'okayama', 'heroshima', 'yamaguchi', 'tokushima', 'kanagawa', 'ehime', 'kochi', 'fukuoka', 'saga',
'nagasaki', 'kumamoto', 'oita', 'miyazaki', 'kagoshima', 'okinawa',
);

/**
* Key is the kanji string (though they are still in the ISO_3166-2:JP order)
*/
$GLOBALS['jprefToFurigana']=array(
'北海道'=>'ホッカイドウ',
'青森県'=>'アオモリケン',
'岩手県'=>'イワテケン',
'宮城県'=>'ミヤギケン',
'山形県'=>'ヤマガタケン',
'福島県'=>'フクシマケン',
'茨城県'=>'イバラギケン',
'栃木県'=>'トチギケン',
'群馬県'=>'グンマケン',
'埼玉県'=>'サイタマケン',
'千葉県'=>'チバケン',
'東京都'=>'トウキョウト',
'神奈川県'=>'カナガワケン',
'新潟県'=>'ニイガタケン',
'富山県'=>'トヤマケン',
'石川県'=>'イシカワケン',
'福井県'=>'フクイケン',
'山梨県'=>'ヤマナシケン',
'長野県'=>'ナガノケン',
'岐阜県'=>'ギフケン',
'静岡県'=>'シズオカケン',
'愛知県'=>'アイチケン',
'三重県'=>'ミエケン',
'滋賀県'=>'シガケン',
'京都府'=>'キョウトフ',
'兵庫県'=>'ヒョウゴケン',
'奈良県'=>'ナラケン',
'和歌山県'=>'ワカヤマケン',
'鳥取県'=>'トットリケン',
'島根県'=>'シマネケン',
'岡山県'=>'オカヤマケン',
'広島県'=>'ヒロシマケン',
'山口県'=>'ヤマグチケン',
'徳島県'=>'トクシマケン',
'香川県'=>'カガワケン',
'愛媛県'=>'エヒメケン',
'高知県'=>'アイチケン',
'福岡県'=>'フクオカケン',
'佐賀県'=>'サガケン',
'長崎県'=>'ナガサキケン',
'熊本県'=>'クマモトケン',
'大分県'=>'オオイタケン',
'宮崎県'=>'ミヤザキケン',
'鹿児島県'=>'カゴシマケン',
'沖縄県'=>'オキナワケン');

?>
