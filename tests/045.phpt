--TEST--
Yac::dump() reports embedded entries (embedded => bool)
--DESCRIPTION--
Scalars small enough to live inside the slot's val pointer are stored with
no value block at all; dump() must expose that via embedded=true (and
size/crc 0, since the crc/size union holds atime there). Values that do not
fit (big ints, doubles, long strings, non-empty arrays) go through the
block path and report embedded=false with a real size and crc.

The assertion set is chosen to be stable across 32-bit and 64-bit builds:
NULL/bool/small ints/strings up to 3 bytes/empty arrays embed on every
platform (32-bit short-string cap is 3 bytes). The 7-byte string sits right
at the 64-bit cap, so its embedded-ness follows the pointer width and is
asserted against PHP_INT_SIZE.
--CREDITS--
Jarvis (AI assistant to Laruence)
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

/* these embed on both 32-bit and 64-bit (ints within +/-2^28, strings <= 3
   bytes, empty array) — no value block is ever allocated for them */
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
	var_dump($item["embedded"], $item["size"], $item["crc"]);
}

/* 7-byte string: embeds on 64-bit, falls back to a block on 32-bit */
$yac->set("emb_sstr7", "abcdefg");
var_dump(dump_find($yac, "emb_sstr7")["embedded"] === (PHP_INT_SIZE >= 8));

/* none of these fit in a pointer word on any platform: value block */
$block_keys = array("emb_big", "emb_dbl", "emb_lstr", "emb_arr");
$yac->set("emb_big", PHP_INT_MAX);
$yac->set("emb_dbl", 3.14);
$yac->set("emb_lstr", str_repeat("x", 64));
$yac->set("emb_arr", array("a" => 1));

foreach ($block_keys as $key) {
	$item = dump_find($yac, $key);
	var_dump($item["embedded"], $item["size"] > 0, $item["crc"] != 0);
}

/* the flag tracks storage form across updates of the same key */
$yac->set("emb_flip", 7);
var_dump(dump_find($yac, "emb_flip")["embedded"]);
$yac->set("emb_flip", str_repeat("y", 64));
var_dump(dump_find($yac, "emb_flip")["embedded"]);

/* round-trips still work for both paths */
var_dump($yac->get("emb_sstr7"));
var_dump($yac->get("emb_lstr") === str_repeat("x", 64));
?>
--EXPECT--
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
int(0)
int(0)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(false)
string(7) "abcdefg"
bool(true)
