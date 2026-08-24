--TEST--
Yac values memory with unaligned segment remainder
--DESCRIPTION--
A values_memory_size whose aligned half-segments (6M + 2K + 4 bytes each)
do not add up to the total forces mmap's create_segments to give the last
segment only the remainder, covering the tail branch. Also smoke-tests odd
but valid sizes.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=12587012
--FILE--
<?php
$yac = new Yac();

/* round-trips with the odd-sized region */
var_dump($yac->set("k", "v"));
var_dump($yac->get("k") === "v");
$big = str_repeat("t", 100000);
var_dump($yac->set("big", $big));
var_dump($yac->get("big") === $big);

$info = $yac->info();
var_dump($info["segment_num"] >= 1);
var_dump($info["memory_size"] > 0);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
