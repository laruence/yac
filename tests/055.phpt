--TEST--
Yac multi-get — missing keys omitted without a default, filled with a default when given
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

$yac->set("a", "va");
$yac->set("b", "vb");

/* 1. no default — missing keys are simply absent from the result */
$ret = $yac->get(["a", "b", "nope"]);
var_dump(count($ret));
var_dump(array_key_exists("nope", $ret));
var_dump($ret["a"], $ret["b"]);

/* 2. with a default — missing keys are present and hold the default */
$ret = $yac->get(["a", "b", "nope"], "MISS");
var_dump(count($ret));
var_dump($ret["nope"]);
var_dump($ret["a"], $ret["b"]);

/* 3. defaults can be any type */
$ret = $yac->get(["nope"], 0);
var_dump($ret["nope"]);

/* 4. existing values are never shadowed by the default */
$yac->set("f", false);
$ret = $yac->get(["f"], "MISS");
var_dump($ret["f"]);
?>
--EXPECTF--
int(2)
bool(false)
string(2) "va"
string(2) "vb"
int(3)
string(4) "MISS"
string(2) "va"
string(2) "vb"
int(0)
bool(false)
