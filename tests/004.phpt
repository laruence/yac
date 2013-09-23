--TEST--
Check for yac ttl
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

$key = "foo";
$value = "dummy";

var_dump($yac->set($key, $value, 1));
var_dump($yac->get($key));
sleep(1);
var_dump($yac->get($key));

var_dump($yac->set($key, $value));
var_dump($yac->get($key));
var_dump($yac->delete($key, 1));
var_dump($yac->get($key));
sleep(1);
var_dump($yac->get($key));

?>
--EXPECTF--
bool(true)
string(5) "dummy"
bool(false)
bool(true)
string(5) "dummy"
bool(true)
string(5) "dummy"
bool(false)
