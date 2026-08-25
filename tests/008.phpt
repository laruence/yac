--TEST--
Check for yac prefix
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac("dummy_");
$yac2 = new Yac();

$key = "bar";
$value = true;

$yac->set($key, $value);
var_dump($yac->get($key));  //true
var_dump($yac2->get($key)); //false
var_dump($yac2->get("dummy_" . $key)); //true

$yac2->delete($key);  //fail
var_dump($yac->get($key));  //true

$yac->delete($key); //okey
var_dump($yac->get($key));  //false

$yac->set($key, $value);
$yac2->delete("dummy_" . $key);  //okey
var_dump($yac->get($key));  //false
?>
--EXPECTF--
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(false)
