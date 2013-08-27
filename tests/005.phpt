--TEST--
Check for yac non-string key
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

$key = md5(12345);
$value = "dummy";

var_dump($yac->set($key, $value, 1));
var_dump($yac->get($key));

$key = 12345.12345;
var_dump($yac->set($key, $value, 1));
var_dump($yac->get($key));

$key = range(1, 2);
var_dump($yac->set($key, $value, 1));
var_dump($yac->get($key));

$key = new StdClass();
var_dump($yac->set($key, $value, 1));
var_dump($yac->get($key));
?>
--EXPECTF--
bool(true)
string(5) "dummy"
bool(true)
string(5) "dummy"
bool(true)
array(2) {
  [1]=>
  int(2)
  [2]=>
  bool(false)
}

Catchable fatal error: Object of class stdClass could not be converted to string in %s005.php on line %d
