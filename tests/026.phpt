--TEST--
Yac false ambiguity — get() returns false for both missing keys and stored false
--CREDITS--
Jarvis (AI assistant to Laruence)
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

/* 1. key that never existed → false */
var_dump($yac->get("never_set"));

/* 2. store literal false → get returns false */
$yac->set("bool_false", false);
var_dump($yac->get("bool_false"));

/* 3. these two are indistinguishable by return value alone */
var_dump($yac->get("never_set") === $yac->get("bool_false"));
var_dump($yac->get("never_set") === false);
var_dump($yac->get("bool_false") === false);

/* 4. explicit type check: gettype() shows both are boolean false */
var_dump(gettype($yac->get("never_set")));
var_dump(gettype($yac->get("bool_false")));

/* 5. store true for contrast — get returns true, distinguishable from false */
$yac->set("bool_true", true);
var_dump($yac->get("bool_true"));
var_dump($yac->get("bool_true") === $yac->get("never_set"));
?>
--EXPECTF--
bool(false)
bool(false)
bool(true)
bool(true)
bool(true)
string(7) "boolean"
string(7) "boolean"
bool(true)
bool(false)
