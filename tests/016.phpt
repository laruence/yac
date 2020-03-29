--TEST--
Check for Yac setter/getter
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
$yac = new Yac("prefix");

$yac->name = "test";

var_dump($yac->name);
var_dump($yac->get("name"));
?>
--EXPECTF--
string(4) "test"
string(4) "test"
