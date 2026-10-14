--TEST--
Yac values_memory_size below 4M rounds up to a single 4M segment
--DESCRIPTION--
Every value segment is a fixed 4M, so a values_memory_size below that still
yields one full 4M segment. A 1MB value that no longer fits a 1M segment now
fits the 4M floor, so set() succeeds and the fails counter stays at zero.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
display_startup_errors=0
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=1M
yac.compress_threshold=-1
--FILE--
<?php
$yac = new Yac();

/* one 4M segment is allocated regardless of the sub-segment ini value */
var_dump($yac->info()["segment_num"]);
var_dump($yac->info()["segment_size"] === 4 * 1024 * 1024);

/* a 1MB raw value now fits the 4M floor instead of failing to allocate */
var_dump($yac->info()["fails"]);
var_dump($yac->set("big", str_repeat("a", 1048576)));
var_dump($yac->info()["fails"]);
var_dump(strlen($yac->get("big")));

/* the cache stays usable after the big write */
var_dump($yac->set("small", "ok"));
var_dump($yac->get("small"));
?>
--EXPECTF--
PHP Warning:  yac.values_memory_size(1048576) is below the segment size(4194304), a single segment will be used in Unknown on line 0%A
int(1)
bool(true)
int(0)
bool(true)
int(0)
int(1048576)
bool(true)
string(2) "ok"
