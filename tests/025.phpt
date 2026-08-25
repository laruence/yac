--TEST--
Yac get() with a second parameter — optional default returned on a miss
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

$key = "k";

/* 1. hit — the default is ignored */
$yac->set($key, "stored");
var_dump($yac->get($key));
var_dump($yac->get($key, "fallback"));

/* 2. miss without a default — false (historical behavior) */
$yac->delete($key);
var_dump($yac->get($key));

/* 3. miss with a default — the default is returned */
var_dump($yac->get($key, "fallback"));
var_dump($yac->get($key, 42));
var_dump($yac->get($key, null));
var_dump($yac->get($key, false));

/* 4. array get with a second param works the same way */
$yac->set("a", "va");
var_dump($yac->get(["a", "b"]));
var_dump($yac->get(["a", "b"], "miss"));
?>
--EXPECTF--
string(6) "stored"
string(6) "stored"
bool(false)
string(8) "fallback"
int(42)
NULL
bool(false)
array(1) {
  ["a"]=>
  string(2) "va"
}
array(2) {
  ["a"]=>
  string(2) "va"
  ["b"]=>
  string(4) "miss"
}
