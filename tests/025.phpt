--TEST--
Yac get() with second parameter — accepted but stays unchanged (CAS not implemented)
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

$key = "cas_test";

/* 1. get() with second param — accepted, value stays unchanged */
$cas = -1;
$yac->set($key, "initial");
$ret = $yac->get($key, $cas);
var_dump($ret);              // "initial" — get works fine
var_dump($cas);              // -1 — unchanged (CAS not implemented)

/* 2. write does not change second param value */
$yac->set($key, "updated");
$cas2 = -1;
$yac->get($key, $cas2);
var_dump($cas2);             // -1 — still unchanged

/* 3. get() for non-existent key - NULL return, second param unchanged */
$yac->delete($key);
$cas3 = -1;
$ret = $yac->get($key, $cas3);
var_dump($ret);              // NULL (key not found)
var_dump($cas3);             // -1

/* 4. array get with second param — value unchanged */
$yac->set("a", "va");
$yac->set("b", "vb");
$cas_arr = -1;
$ret = $yac->get(["a", "b"], $cas_arr);
var_dump(is_array($ret));
var_dump($ret["a"]);
var_dump($ret["b"]);
var_dump($cas_arr);          // -1
?>
--EXPECTF--
string(7) "initial"
int(-1)
int(-1)
NULL
int(-1)
bool(true)
string(2) "va"
string(2) "vb"
int(-1)
