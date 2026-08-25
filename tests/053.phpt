--TEST--
Yac per-entry hit counters (block values, embedded scalars, carry on overwrite)
--DESCRIPTION--
find() bumps a per-entry counter: block values keep it in the value
block, embedded scalars keep it in the slot's flag union bytes.
Overwriting a live entry carries the count forward; expired and
deleted entries start cold again. dump() exposes the count.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.serializer=php
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

/* block values: the count lives in the value block */
$yac->set("b", str_repeat("x", 64));
$yac->get("b");
$yac->get("b");
var_dump(dump_find($yac, "b")["hits"]);

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
var_dump(dump_find($yac, "b")["embedded"]);

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
--EXPECT--
int(0)
int(3)
int(3)
int(2)
int(0)
int(1)
int(0)
int(0)
int(0)
bool(true)
NULL
int(0)
int(0)
