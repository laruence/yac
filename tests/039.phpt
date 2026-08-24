--TEST--
Yac binary safety — strings with NUL bytes round-trip byte-exact
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

/* 1. NUL bytes inside a string */
$bin = "a\0b\0c";
var_dump($yac->set("bin", $bin));
var_dump($yac->get("bin") === $bin);
var_dump(strlen($yac->get("bin")));

/* 2. leading and trailing NULs */
$lead = "\0start";
$trail = "end\0";
$yac->set("lead", $lead);
$yac->set("trail", $trail);
var_dump($yac->get("lead") === $lead);
var_dump($yac->get("trail") === $trail);

/* 3. all-NUL string */
$nuls = str_repeat("\0", 8);
$yac->set("nuls", $nuls);
var_dump($yac->get("nuls") === $nuls);

/* 4. binary strings as array keys and values (serializer path) */
$arr = ["k\0ey" => "v\0alue", "plain" => 1];
$yac->set("arr", $arr);
var_dump($yac->get("arr") === $arr);
?>
--EXPECT--
bool(true)
bool(true)
int(5)
bool(true)
bool(true)
bool(true)
bool(true)
