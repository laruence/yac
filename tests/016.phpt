--TEST--
Check for Yac setter/getter
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
$yac = new Yac();

$yac->name = "test";

var_dump($yac->name);
?>
--EXPECTF--
string(4) "test"
