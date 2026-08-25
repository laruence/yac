--TEST--
Yac false ambiguity — a miss returns false; a default value disambiguates
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

/* 1. without a default, a miss and a stored false are indistinguishable */
$yac->set("bool_false", false);
var_dump($yac->get("never_set"));
var_dump($yac->get("bool_false"));
var_dump($yac->get("never_set") === $yac->get("bool_false"));

/* 2. with a sentinel default the two become distinguishable */
var_dump($yac->get("never_set", "__NONE__"));   // miss -> the sentinel
var_dump($yac->get("bool_false", "__NONE__")); // stored false is returned as-is

/* 3. explicit type check: both are boolean false without a default */
var_dump(gettype($yac->get("never_set")));
var_dump(gettype($yac->get("bool_false")));

/* 4. store true for contrast — get returns true, distinguishable from a miss */
$yac->set("bool_true", true);
var_dump($yac->get("bool_true"));
var_dump($yac->get("bool_true") === $yac->get("never_set"));
?>
--EXPECTF--
bool(false)
bool(false)
bool(true)
string(8) "__NONE__"
bool(false)
string(7) "boolean"
string(7) "boolean"
bool(true)
bool(false)
