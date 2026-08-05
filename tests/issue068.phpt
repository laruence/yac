--TEST--
Check for yac basic functions
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.serializer=php
--FILE--
<?php 
$yac = new Yac();

$key = "foo";
$value = "dummy";
var_dump(empty($yac->dump()));
var_dump($yac->set($key, $value));
var_dump(empty($yac->dump()));

unset($yac->foo);
var_dump(empty($yac->dump()));


?>
--EXPECTF--
bool(true)
bool(true)
bool(false)
bool(true)
