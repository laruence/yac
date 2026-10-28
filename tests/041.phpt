--TEST--
Yac is a no-op in CLI when yac.enable_cli=0
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=0
--FILE--
<?php
$yac = new Yac("prefix:");

var_dump($yac->add("key", "value"));
var_dump($yac->set("key", "value"));
var_dump($yac->get("key", "default"));
var_dump($yac->delete("key"));
var_dump($yac->has("key"));
var_dump($yac->flush());
var_dump($yac->info());
var_dump($yac->dump(10));
?>
--EXPECT--
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
