--TEST--
Check for yac::__construct with yac.enable=0
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=0
--FILE--
<?php
$yac = new Yac("prefix");
var_dump($yac instanceof Yac);
var_dump($yac->set("key", "value"));
var_dump($yac->get("key"));
var_dump($yac->info());
?>
--EXPECT--
bool(true)
bool(false)
bool(false)
bool(false)
