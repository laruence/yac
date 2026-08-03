--TEST--
Yac get() with second parameter — accepted but stays untouched (CAS not implemented)
--CREDITS--
Jarvis (AI assistant to Laruence)
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

/* 1. get() with second param — accepted, but stays untouched */
$cas = -1;
$yac->set($key, "initial");
$ret = $yac->get($key, $cas);
var_dump($ret);              // "initial" — get works fine
var_dump($cas);              // -1 — unchanged (CAS not implemented)

/* 2. write does not change cas value either */
$yac->set($key, "updated");
$cas2 = -1;
$yac->get($key, $cas2);
var_dump($cas2);             // -1 — still untouched

/* 3. non-existent key → false return, cas stays untouched */
$yac->delete($key);
$cas3 = -1;
$ret = $yac->get($key, $cas3);
var_dump($ret);              // false (key not found)
var_dump($cas3);             // -1 — untouched

/* 4. array get with cas param — stays untouched */
$yac->set("a", "va");
$yac->set("b", "vb");
$cas_arr = -1;
$ret = $yac->get(["a", "b"], $cas_arr);
var_dump(is_array($ret));
var_dump($ret["a"]);
var_dump($ret["b"]);
var_dump($cas_arr);          // -1 — untouched
?>
--EXPECTF--
string(7) "initial"
int(-1)
int(-1)
bool(false)
int(-1)
bool(true)
string(2) "va"
string(2) "vb"
int(-1)
