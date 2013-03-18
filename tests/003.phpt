--TEST--
Check for yac errors
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php 
$yac = new Yac();

$key = str_repeat("k", 63);
$value = NULL;

var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$key = str_repeat("k", 64);
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$key = str_repeat("k", 65);
var_dump($yac->set($key, $value));
var_dump($yac->get($key));
?>
--EXPECTF--
bool(true)
NULL
bool(true)
NULL

Warning: Yac::set(): Key(include prefix) can not be longer than 64 bytes in /home/huixinchen/packages/php-5.2.17/ext/yac/tests/003.php on line 15
bool(false)

Warning: Yac::get(): key(include prefix) can not be longer than 64 bytes in /home/huixinchen/packages/php-5.2.17/ext/yac/tests/003.php on line 16
bool(false)
