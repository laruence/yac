--TEST--
Check for yac::add
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

$key = "dummy";
$value = "foo";
var_dump($yac->add($key, $value)); // true
var_dump($yac->add($key, $value)); // false
var_dump($yac->set($key, $value)); // true
var_dump($yac->get($key));		   // foo
var_dump($yac->delete($key));      // true
$value = "bar";
var_dump($yac->add($key, $value, 1)); //true
var_dump($yac->add($key, $value, 1)); //false
var_dump($yac->get($key));            //bar
sleep(1);
var_dump($yac->add($key, $value));    //true

?>
--EXPECTF--
bool(true)
bool(false)
bool(true)
string(3) "foo"
bool(true)
bool(true)
bool(false)
string(3) "bar"
bool(true)
