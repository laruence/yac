--TEST--
Yac::incr()/decr() only touch embedded longs
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();
$yac->flush();

/* absent key: no 0-seeding, false */
var_dump($yac->incr("n"));

/* seed an embedded long and step it */
var_dump($yac->set("n", 5));
var_dump($yac->incr("n"));        /* 6, default step 1 */
var_dump($yac->incr("n", 3));     /* 9 */
var_dump($yac->decr("n"));        /* 8 */
var_dump($yac->decr("n", 4));     /* 4 */
var_dump($yac->get("n"));         /* 4 */

/* non-long values are refused, value left intact */
var_dump($yac->set("s", "str"));
var_dump($yac->incr("s"));        /* false */
var_dump($yac->get("s"));         /* "str" untouched */

var_dump($yac->set("t", true));
var_dump($yac->incr("t"));        /* false */

var_dump($yac->set("d", 1.5));
var_dump($yac->decr("d"));        /* false */
var_dump($yac->get("d"));         /* 1.5 untouched */

/* negatives */
var_dump($yac->set("neg", -10));
var_dump($yac->decr("neg", 5));    /* -15 */
var_dump($yac->incr("neg", 5));   /* -10 */
var_dump($yac->get("neg"));        /* -10 */

$yac->flush();
?>
--EXPECTF--
bool(false)
bool(true)
int(6)
int(9)
int(8)
int(4)
int(4)
bool(true)
bool(false)
string(3) "str"
bool(true)
bool(false)
bool(true)
bool(false)
float(1.5)
bool(true)
int(-15)
int(-10)
int(-10)
