--TEST--
Recycled value blocks preserve read correctness
--DESCRIPTION--
Values remain readable until their blocks are overwritten during recycling;
once overwritten, the old key must return a miss rather than stale data.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=1M
yac.values_memory_size=4M
yac.compress_threshold=-1
--FILE--
<?php
$yac = new Yac();
$same = str_repeat("a", 1024 * 1024 - 256);
$old = str_repeat("o", 1024 * 1024 - 256);
$new = str_repeat("n", 1024 * 1024 - 256);

var_dump($yac->set("same", $same));
var_dump($yac->get("same") === $same);

var_dump($yac->set("reuse", $old));
var_dump($yac->set("reuse", $old));

/* old0 remains readable after the next segment generation begins, until its
 * former block offset is reused. */
var_dump($yac->set("old0", $old));
var_dump($yac->set("late", $new));
var_dump($yac->set("new0", $new));
var_dump($yac->get("old0") === $old);

/* Reusing old0's block makes its old slot a miss, never stale data. */
var_dump($yac->set("new1", $new));
var_dump($yac->get("old0") === false);
var_dump($yac->set("new2", $new));
var_dump($yac->get("old0") === false);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
