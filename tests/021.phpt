--TEST--
Check for yac read/write/unset property
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php 
$yac = new Yac("prefix");

$yac->value = "value";

try {
	/* can not used in writen context */
	$yac->foo->bar = "bar";
} catch (Error $e) {
	/* expected exception on PHP 8 */
}

var_dump($yac->get("value"));
var_dump($yac->value);

var_dump($yac->get("foo"));
var_dump($yac->foo);

unset($yac->value);
var_dump($yac->get("value"));
var_dump($yac->value);
?>
--EXPECT--
string(5) "value"
string(5) "value"
bool(false)
NULL
bool(false)
NULL
