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
?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
