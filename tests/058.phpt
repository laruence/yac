--TEST--
Yac default memory sizes: 8M for keys, 64M for values
--DESCRIPTION--
Without explicit configuration, yac.keys_memory_size defaults to 8M and
yac.values_memory_size to 64M; info() reports the effective sizes.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
--FILE--
<?php
$yac = new Yac();

$info = $yac->info();
var_dump($info["slots_memory_size"]);
var_dump($info["values_memory_size"]);
var_dump($info["memory_size"]);
?>
--EXPECT--
int(8388608)
int(67108864)
int(75497472)
