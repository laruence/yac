--TEST--
Yac per-entry hit counters across val-word, inline, and block values
--DESCRIPTION--
find() bumps a per-entry counter in the slot LRU state for both block
and embedded values. Overwriting, expiry, and deletion start it cold again.
dump() exposes the count.

The counter is sampled (one read in YAC_HITS_PER_SAMPLE adds that
much), so a count that followed reads is an estimate and only matched
as %d. The zeroes are exact: every (re)write starts cold.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
function dump_find($yac, $key) {
	foreach ($yac->dump(100000) as $item) {
		if ($item["key"] === $key) {
			return $item;
		}
	}
	return NULL;
}

$yac = new Yac();

/* a fresh entry is cold */
$yac->set("e", 123);
var_dump(dump_find($yac, "e")["hits"]);

/* global and per-entry counters move together */
$hits0 = $yac->info()["hits"];
$yac->get("e");
$yac->get("e");
$yac->get("e");
var_dump($yac->info()["hits"] - $hits0);
var_dump(dump_find($yac, "e")["hits"]);

/* inline values update the same slot LRU */
$yac->set("i", str_repeat("i", 8));
for ($n = 0; $n < 128; $n++) {
	$yac->get("i");
}
var_dump(dump_find($yac, "i")["hits"] > 0);

/* block values update the same slot LRU */
$yac->set("b", str_repeat("x", 64));
for ($n = 0; $n < 128; $n++) {
	$yac->get("b");
}
var_dump(dump_find($yac, "b")["hits"] > 0);

/* overwriting a live block resets the count */
$yac->set("b", str_repeat("y", 64));
var_dump(dump_find($yac, "b")["hits"]);
$yac->get("b");
var_dump(dump_find($yac, "b")["hits"]);

/* overwriting a live embed resets the count too */
$yac->set("e", 456);
var_dump(dump_find($yac, "e")["hits"]);

/* the storage form changes, the count is cold either way: embed -> block ... */
$yac->set("e", str_repeat("z", 64));
var_dump(dump_find($yac, "e")["hits"]);
/* ... and block -> embed */
$yac->set("b", 789);
var_dump(dump_find($yac, "b")["hits"]);
var_dump(dump_find($yac, "b")["location"]);

/* expired entries start cold again */
$yac->set("t", 1, 1);
$yac->get("t");
sleep(2);
var_dump($yac->get("t"));
$yac->set("t", 2);
var_dump(dump_find($yac, "t")["hits"]);

/* deleted entries start cold again */
$yac->set("d", str_repeat("d", 64));
$yac->get("d");
$yac->get("d");
$yac->delete("d");
$yac->set("d", 42);
var_dump(dump_find($yac, "d")["hits"]);
?>
--EXPECTF--
int(0)
int(3)
int(%d)
bool(true)
bool(true)
int(0)
int(%d)
int(0)
int(0)
int(0)
int(1)
bool(false)
int(0)
int(0)
