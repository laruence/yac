--TEST--
Yac::dump() reports embedded entries (embed => int)
--DESCRIPTION--
dump() reports embed as an int: 1 for a val-word embed, 2 for a key-inline
value (the only form with a real v_len), 0 for a block. Both embed kinds carry
size/crc 0. Assertions are stable across 32/64-bit builds.
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

/* val-word embeds on both 32-bit and 64-bit */
$embed_keys = array("emb_null", "emb_true", "emb_false", "emb_int", "emb_neg",
		"emb_sstr0", "emb_sstr3", "emb_nul", "emb_arr0");
$yac->set("emb_null", NULL);
$yac->set("emb_true", TRUE);
$yac->set("emb_false", FALSE);
$yac->set("emb_int", 123);
$yac->set("emb_neg", -456);
$yac->set("emb_sstr0", "");
$yac->set("emb_sstr3", "abc");
$yac->set("emb_nul", "a\0b");
$yac->set("emb_arr0", array());

foreach ($embed_keys as $key) {
	$item = dump_find($yac, $key);
	var_dump($item["embed"], $item["size"], $item["crc"]);
}

/* 7-byte string: val word on 64-bit, inline on 32-bit — embedded either way */
$yac->set("emb_sstr7", "abcdefg");
var_dump(dump_find($yac, "emb_sstr7")["embed"] > 0);

/* too big for the val word, small enough to store inline */
$inline_keys = array("emb_big", "emb_dbl", "emb_arr");
$yac->set("emb_big", PHP_INT_MAX);
$yac->set("emb_dbl", 3.14);
$yac->set("emb_arr", array("a" => 1));

foreach ($inline_keys as $key) {
	$item = dump_find($yac, $key);
	var_dump($item["embed"], $item["v_len"] > 0, $item["size"]);
}

/* fits neither the val word nor the key area: block */
$yac->set("emb_lstr", str_repeat("x", 64));
$item = dump_find($yac, "emb_lstr");
var_dump($item["embed"], $item["size"] > 0, $item["crc"] != 0);

/* the flag tracks storage form across updates of the same key */
$yac->set("emb_flip", 7);
var_dump(dump_find($yac, "emb_flip")["embed"]);
$yac->set("emb_flip", str_repeat("y", 64));
var_dump(dump_find($yac, "emb_flip")["embed"]);

/* round-trips still work for both paths */
var_dump($yac->get("emb_sstr7"));
var_dump($yac->get("emb_lstr") === str_repeat("x", 64));
?>
--EXPECT--
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
int(1)
int(0)
int(0)
bool(true)
int(2)
bool(true)
int(0)
int(2)
bool(true)
int(0)
int(2)
bool(true)
int(0)
int(0)
bool(true)
bool(true)
int(1)
int(0)
string(7) "abcdefg"
bool(true)
