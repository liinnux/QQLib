<?php
namespace QQAPI;

include_once "PDOWrapperJson.inc";

/**
* This database stores payment transactions.
*
* It is intended to be used for processing payments from multiple different payment
* gateways. All available information from those gateways is stored in the 'extra' field
* but the main columns are just the most useful ones for normal use and basic analysis.
*
* If the transaction is for multiple items that would either be stored in the extra column,
* or in another table; the aggregate order is what is stored here. Similarly, things like
* delivery address are not intended to be stored here (though it is possible it will be found
* in 'extra').
*
* created is when the DB record was created. It is when the transation was posted; the transaction
*    date could be different
* updated is when a record was last updated, but is normally NULL as transactions records will
*    normally be written once. However the comments field can be added afterwards. The comments
*    field is also where explanation of any other updates would be made.
* transactionDate: this is the timestamp the payment gateway gives; this is useful for matching
*    against bank statements, etc. It will be converted to UTC.
*
* source is e.g. "paypal".
* reference, for PayPal, is the paypal transaction id.
*   Note that the combination of source and reference must be unique.
*   They are used as the primary key.
* userId is a unique id of the person who made the payment. Typically this
*   will be the id field in a DBUsers table, or similar.
*
* amount is the amount that was paid by the member. It includes fees, shipping, tax.
* fees is the amount deducted by banks, paypal, etc.
* shipping is the part of amount that was billed as shipping, p&p, etc. It can be used
*    to mean any extra costs of the order, that is not covered by fees or tax.
* tax is the part of amount that is sales tax, VAT, etc.
* currency is the currency for amount (and usually for fees, shipping, tax).
*
* extra is a json field. It is optional.
*    E.g. for PayPal it contains the complete transaction record that PayPal sent.
* comments is for administrator comments. It is usually blank.
*
* Note: all the currency fields are required (NOT NULL), but all (except amount) default to
*    zero, so an application does not need to use them.
* Note: In complex systems where the fields need to be in different currencies, either
*    handle that in your application code (the DB level does not enforce they are the same
*    currency), or convert to a common currency, and document the original value and exchange
*    rate used in the extra column.
* Note: in the case of PayPal, we assume fees was withheld by PayPal, but shipping and tax
*    are just informational, and are being paid the to the business, for it to then pay to the
*    shipping company and government.
*
* @internal I considered combining source and reference into a single field, as a more natural
*   primary key, e.g. "PP*12355346". But that makes a more complex SQL query if we just want
*   to study income from a single payment gateway.
*   By the way, having reference be unique was important to make sure that a transaction
*   could not be accidentally processed twice.
*/
class DBPayments extends PDOWrapperJson{
/**
* The table name.
*
* Change this in the constructor of your derived class, if you need something different.
*/
protected $tableName = "payments";


/** */
function __construct($dsn,$user=null,$pw=null,$options=array()){
$this->jsonFields = array('extra');
parent::__construct($dsn,$user,$pw,$options);
}


/**
*/
public function createTable(){
if($this->checkForTable($this->tableName) !== null)return;
$q = "CREATE TABLE {$this->tableName}(";
$q.=<<<EOD
  created DOUBLE NOT NULL,
  updated DOUBLE,
  transactiondate DATETIME,

  source VARCHAR(100) NOT NULL,
  reference VARCHAR(200) NOT NULL,
  userId INTEGER NOT NULL,

  amount DOUBLE NOT NULL,
  fees DOUBLE NOT NULL DEFAULT 0.0,
  shipping DOUBLE NOT NULL DEFAULT 0.0,
  tax DOUBLE NOT NULL DEFAULT 0.0,
  currency TEXT NOT NULL,

  extra TEXT,
  comments TEXT,

  PRIMARY KEY (source, reference)
  )
EOD;
$this->get()->exec($q);
}


/**
* This inserts a pay pal row for you, when you receive IPN messages.
*
* Also see the PayPalHandler class in the library, for dealing with IPN messages.
*
* @param Array $P The data that PayPal sent. The whole thing is stored
*    in the extra field, but we also directly use these fields:
*       payment_date, txn_id, mc_gross, mc_fee, mc_currency, mc_shipping, mc_handling, tax
*    Note: the sum of mc_shipping and mc_handling goes in the shipping column.
*    Note: you should already have validated the data with PayPal.
* @param Int $userId The user who made this payment.
* @param Number $now The created time to use. This is just here for unit testing,
*    and the default is what you want, normally.
*
* @return True if added ok, false if it was already there. Other errors will be
*    thrown as exceptions.
* 
* @internal Here is sample IPN data from PayPal:
* The meaning of these fields can be found at:
*    https://developer.paypal.com/docs/classic/ipn/integration-guide/IPNandPDTVariables/
Array
(
    [mc_gross] => 500
    [protection_eligibility] => Eligible
    [address_status] => unconfirmed
    [item_number1] => 
    [payer_id] => 6NXSXXGAF5ZU2
    [tax] => 0
    [address_street] => Nishi 4-chome, Kita 55-jo, Kita-ku
    [payment_date] => 07:33:01 May 14, 2015 PDT
    [payment_status] => Completed
    [charset] => Shift_JIS
    [address_zip] => 150-0002
    [mc_shipping] => 0
    [mc_handling] => 0
    [first_name] => test
    [mc_fee] => 58
    [address_country_code] => JP
    [address_name] => buyer test
    [notify_version] => 3.8
    [custom] => 
    [payer_status] => verified
    [business] => darren-facilitator@dcook.org
    [address_country] => Japan
    [num_cart_items] => 1
    [mc_handling1] => 0
    [address_city] => Shibuya-ku
    [verify_sign] => A9LC3Qajo-H2V8mPq4eIktgPvG2RAct4aRchq8tHmEEIjVT9O5qT43Bi
    [payer_email] => darren-buyer@dcook.org
    [mc_shipping1] => 0
    [tax1] => 0
    [txn_id] => 07B16547DL587011T
    [payment_type] => instant
    [last_name] => buyer
    [address_state] => Tokyo
    [item_name1] => Test Purchase Thing
    [receiver_email] => darren-facilitator@dcook.org
    [payment_fee] => 
    [quantity1] => 1
    [receiver_id] => UVZEGUEYJ9J44
    [txn_type] => cart
    [mc_gross_1] => 500
    [mc_currency] => JPY
    [residence_country] => JP
    [test_ipn] => 1
    [transaction_subject] => 
    [payment_gross] => 
    [ipn_track_id] => 9581890b8972
)

*/
public function insertRowForPayPalIPN($P, $userId, $now = null){
if(!$now)$now = microtime(true);
$when = strtotime($P['payment_date']);  //E.g. 07:33:01 May 14, 2015 PDT
$shipping = (double)@$P['mc_shipping'] + (double)@$P['mc_handling'];
$d = array(
    'created' => $now,
    'transactiondate' => gmdate("Y-m-d H:i:s", $when),
    'source' => 'paypal',
    'reference' => $P['txn_id'],
    'userId' => $userId,
    'amount' => $P['mc_gross'], //What was paid by the user, including all fees.
    'fees' => $P['mc_fee'], //What paypal deducted
    'shipping' => $shipping,
    'tax' => $P['tax'],
    'currency' => $P['mc_currency'],
    'extra' => $P,
    );

try{
    $this->insertRow($this->tableName, $d);
    }catch(\PDOException $e){
    if($e->getCode() == "23000")return false;  //Duplicate row, so nothing to do
    throw $e;   //Otherwise re-throw it
    }

return true;
}


/**
* Get all transactions for a particular user and/or a particular payment gateway
* 
* By setting the first two parameters to null, but using $start/$end, you can also fetch
* all transactions for all users and payment sources in a given time period.
* 
* @param Number $userId The user to fetch. Use null to mean all users.
* @param String $source The payment gateway to fetch. Use null (the default) to mean all of them.
* @param Number $start,$end Optional parameters to filter by created date.
*     If given then comparisons are >= $start, and <= $end.
*     (The created field is secs since 1970, but a double.)
*     Set to null to not use either of them.
* @param String $dateField Whether to use created or transactionDate for the date filtering.
* @param string $select The fields to get. Defaults to '*'
* @param string $orderBy How to order. If "" then no ordering is specified, but it defaults
*   to returning payments from oldest to newest (based on creation time).
* @return array All payment transactions
* 
* @internal I started with getAllForUser() and getAllForSource(). I was reluctant to combine
*   them in a single more complex function, but it does not seem too bad. It also means
*   the same set of parameters can be used for analyzePayments().
*   Initially I called this getAllForUserOrSource(), but getAll() is just as good.
*   I took this opportunity to make $userId optional parameter too.
*   The only negative point is that it does not communicate so well that the first param
*   is $userId and the second is $source. 
*/
public function getAll($userId=null, $source=null, $start=null, $end=null,
    $dateField = 'created', $select = '*', $orderBy = "created ASC"){
assert($dateField=='created' || $dateField=='transactionDate');
$d = array();
$q = 'SELECT '.$select.' FROM '.$this->tableName.' WHERE 1=1';
if($userId){$q.=" AND userId = ?";$d[]=$userId;}
if($source){$q.=" AND source = ?";$d[]=$source;}
if($start !== null){$q.=" AND {$dateField} >= ?";$d[]=$start;}
if($end !== null){$q.=" AND {$dateField} <= ?";$d[]=$end;}
if($orderBy)$q.=" ORDER BY ".$orderBy;
return $this->fetchAll($q, $d);
}

/**
* Like getAll(), but does an aggregate query instead.
* 
* Returned results are grouped by currency. If you are using a single currency system
* then only a single row will be returned.
* 
* @param Number $userId The user to fetch. Use null to mean all users.
* @param String $source The payment gateway to fetch. Use null (the default) to mean all of them.
* @param Number $start,$end Optional parameters to filter by created date.
*     If given then comparisons are >= $start, and <= $end.
*     (The created field is secs since 1970, but a double.)
*     Set to null to not use either of them.
* @param String $dateField Whether to use created or transactionDate for the date filtering.
* @param string $orderBy Defaults to ordering by currency, alphabetic order.
* @return Array of arrays, where the inner array will have these fields:
*     currency, count, amount, fees, shipping, tax
*    By default they are ordered in alphabetical currency order (e.g. EUR, GBP, JPY, USD).
*/
public function analyzePayments($userId=null, $source=null, $start=null, $end=null,
    $dateField = 'created', $orderBy = "currency ASC"){
$d = array();
$q = 'SELECT currency,count(*) AS `count`,sum(amount) AS amount,sum(fees) AS fees,sum(shipping) AS shipping,sum(tax) AS tax FROM '.$this->tableName.' WHERE 1=1';
if($userId){$q.=" AND userId = ?";$d[]=$userId;}
if($source){$q.=" AND source = ?";$d[]=$source;}
if($start !== null){$q.=" AND {$dateField} >= ?";$d[]=$start;}
if($end !== null){$q.=" AND {$dateField} <= ?";$d[]=$end;}
$q.=" GROUP BY currency";
$q.=" ORDER BY ".$orderBy;
return $this->fetchAll($q, $d);
}

}   //End of class DBPayments
