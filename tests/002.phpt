--TEST--
Check for yac basic functions
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
<?php if (YAC_SERIALIZER != "PHP") print "skip"; ?>
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

var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = NULL;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = TRUE;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = FALSE;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = range(1, 5);
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = 9234324;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = 9234324.123456;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = new StdClass();;
var_dump($yac->set($key, $value));
var_dump($yac->get($key));

$value = fopen("php://input", "r");
var_dump($yac->set($key, $value));

$value = range(1, 5);
var_dump($yac->set($key, $value));
var_dump($yac->delete($key));
var_dump($yac->get($key));

?>
--EXPECTF--
bool(true)
string(5) "dummy"
bool(true)
NULL
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
array(5) {
  [0]=>
  int(1)
  [1]=>
  int(2)
  [2]=>
  int(3)
  [3]=>
  int(4)
  [4]=>
  int(5)
}
bool(true)
int(9234324)
bool(true)
float(9234324.123456)
bool(true)
object(stdClass)#3 (0) {
}

Warning: Yac::set(): Type 'IS_RESOURCE' cannot be stored in %s002.php on line %d
bool(false)
bool(true)
bool(true)
bool(false)
