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

/* negative TTL: stored but already expired on read */
$key = "neg";
var_dump($yac->set($key, "v", -1));
var_dump($yac->get($key));

/* negative TTL overwrites a persistent value and expires it */
$yac->set($key, "persistent");
var_dump($yac->set($key, "v", -100));
var_dump($yac->get($key));

/* a key expired by a negative TTL can be add()-ed again */
var_dump($yac->add($key, "added"));
var_dump($yac->get($key));

/* delete() with a negative delay removes immediately */
$yac->set("del", "v");
var_dump($yac->delete("del", -1));
var_dump($yac->get("del"));

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
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
string(5) "added"
bool(true)
bool(false)
