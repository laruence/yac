--TEST--
Check for functional apis
--SKIPIF--
<?php 
if (!extension_loaded("yac")) print "skip";
print "skip Functional style APIs are not enabled";
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php 
$key = "dummy";
$value = "foo";
var_dump(yac_set($key, $value)); //true
var_dump(yac_add($key, $value)); //false
var_dump(yac_get($key));         //foo
var_dump(yac_delete($key));      //true
var_dump(yac_set($key, $value)); //true
var_dump(yac_flush());           //true
$info = yac_info();              
var_dump($info['slots_used']);   //0
?>
--EXPECTF--
bool(true)
bool(false)
string(3) "foo"
bool(true)
bool(true)
bool(true)
int(0)
