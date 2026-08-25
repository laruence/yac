--TEST--
Yac::get() returns NULL for missing keys and false for stored false — no ambiguity
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

/* 1. key that never existed → NULL */
var_dump($yac->get("never_set"));

/* 2. store literal false → get returns false */
$yac->set("bool_false", false);
var_dump($yac->get("bool_false"));

/* 3. the two are distinguishable by return value */
var_dump($yac->get("never_set") === NULL);
var_dump($yac->get("bool_false") === false);
var_dump($yac->get("never_set") === $yac->get("bool_false"));

/* 4. explicit type check: NULL vs boolean */
var_dump(gettype($yac->get("never_set")));
var_dump(gettype($yac->get("bool_false")));

/* 5. store true for contrast */
$yac->set("bool_true", true);
var_dump($yac->get("bool_true"));
var_dump($yac->get("bool_true") === $yac->get("never_set"));
?>
--EXPECTF--
NULL
bool(false)
bool(true)
bool(true)
bool(false)
string(4) "NULL"
string(7) "boolean"
bool(true)
bool(false)
