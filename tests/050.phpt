--TEST--
Yac::delete with overlong and non-string keys
--DESCRIPTION--
delete() of a key that exceeds YAC_MAX_KEY_LEN (also via prefix assembly)
degrades to false with a warning; multi-delete accepts non-string entries
(int keys are cast).
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

/* plain overlong key */
var_dump($yac->delete(str_repeat("k", YAC_MAX_KEY_LEN + 1)));

/* prefix + key assembled over the limit */
$p = new Yac("pfx");
var_dump($p->delete(str_repeat("k", YAC_MAX_KEY_LEN)));

/* multi-delete with non-string (int) entries */
var_dump($yac->set("123", "int-key"));
var_dump($yac->set("del_a", "a"));
var_dump($yac->delete(array("del_a", 123, "never_there")));
var_dump($yac->get("del_a"));
var_dump($yac->get("123"));
?>
--EXPECTF--
Warning: Yac::delete(): Key '%s' exceed max key length '48' bytes in %s050.php on line %d
bool(false)

Warning: Yac::delete(): Key '%s' exceed max key length '48' bytes in %s050.php on line %d
bool(false)
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
