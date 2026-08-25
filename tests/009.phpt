--TEST--
Check for yac multi ops
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

$values = array();
$chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
for ($i = 0; $i < 100; $i++) {
    $key = substr(str_shuffle($chars), 0, rand(16, 32));
    $value = md5($key . rand(1, 10000));
    $values[$key] = $value;
}

$numbers = count($values);

var_dump($yac->set($values));

$keys = array_keys($values);
$ret = $yac->get($keys);
var_dump(count(array_filter($ret)) == $numbers);

$disable = array_slice($keys, 0, -10);
$yac->delete($disable);

$ret = $yac->get($keys);
var_dump(count(array_filter($ret)) == 10);

/* batch delete mixing existent and non-existent keys: returns false, but existent keys are still removed */
$remaining = array_slice($keys, -10);
var_dump($yac->delete(array_merge($remaining, array("nonexistent_key")))); /* false */
$ret = $yac->get($keys);
var_dump(count(array_filter($ret)) == 0);

/* multi-get omits missing keys */
$yac->set("present", "v");
$ret = $yac->get(array("present", "absent"));
var_dump($ret["present"]);
var_dump(array_key_exists("absent", $ret));

/* numeric-indexed batch set stores values under string keys "0", "1", ... */
var_dump($yac->set(array("zero", "one")));
var_dump($yac->get("0"));
var_dump($yac->get("1"));

/* batch set with a non-integer ttl warns and writes nothing */
var_dump($yac->set(array("w" => "v"), "not_int"));
var_dump($yac->get("w") === null);
?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
string(1) "v"
bool(false)
bool(true)
string(4) "zero"
string(3) "one"

Warning: Yac::set(): ttl parameter must be an integer in %s on line %d
NULL
bool(true)
