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

$key = str_repeat("k", YAC_MAX_KEY_LEN - 1);
$value = NULL;

var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$key = str_repeat("k", YAC_MAX_KEY_LEN);
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$key = str_repeat("k", YAC_MAX_KEY_LEN + 1);
var_dump($yac->set($key, $value));
var_dump($yac->get($key));
?>
--EXPECTF--
bool(true)
NULL
bool(true)
NULL

Warning: Yac::set(): Key(include prefix) can not be longer than 32 bytes in %s003.php on line %d
bool(false)

Warning: Yac::get(): key(include prefix) can not be longer than 32 bytes in %s003.php on line %d
bool(false)
