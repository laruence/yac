--TEST--
Yac delete with delay
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

$yac->set("delayed", "value");

/* 1. delete with 1-second delay — value still accessible immediately */
var_dump($yac->delete("delayed", 1));
var_dump($yac->get("delayed"));   /* still "value" — not yet expired */

/* 2. after sleep, the value is gone */
sleep(1);
var_dump($yac->get("delayed"));   /* NULL — expired */

/* 3. delete with 0 delay — same as immediate delete */
$yac->set("immediate", "now");
var_dump($yac->delete("immediate", 0));
var_dump($yac->get("immediate")); /* NULL — already gone */

/* 4. delete without delay argument — immediate */
$yac->set("no_delay", "x");
var_dump($yac->delete("no_delay"));
var_dump($yac->get("no_delay"));

/* 5. delayed delete on non-existent key */
var_dump($yac->delete("never_set", 10)); /* false */

/* 6. immediate delete on non-existent key */
var_dump($yac->delete("never_set")); /* false */
?>
--EXPECTF--
bool(true)
string(5) "value"
NULL
bool(true)
NULL
bool(true)
NULL
bool(false)
bool(false)
