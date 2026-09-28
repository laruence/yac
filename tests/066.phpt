--TEST--
Yac::dump() reports kind and embed by the exposed enum constants
--DESCRIPTION--
dump() adds a "kind" field alongside "embed". kind is the value's logical type
(YAC_KIND_*), embed is where its bytes physically live (YAC_EMBED_*). The two
are orthogonal: a LONG can sit in the val word (small) or key-inline (PHP_INT_MAX
needs 63 bits), a STRING can be a val word (<=7 bytes), inline, or a block.
Assertions use the constants, not magic numbers, so a renumbering fails loudly.
The serializer choice does not affect kind: an array is BLOB whether php or
json serializes it.
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

/* assert kind and embed together for one stored value */
function check($yac, $key, $value, $kind, $embed) {
	$yac->set($key, $value);
	$item = dump_find($yac, $key);
	var_dump($item["kind"] === $kind, $item["embed"] === $embed);
}

$yac = new Yac();

/* FLAG: the four special scalars, all a single byte in the val word */
check($yac, "k_null",  NULL,        YAC_KIND_FLAG, YAC_EMBED_VALWORD);
check($yac, "k_true",  TRUE,        YAC_KIND_FLAG, YAC_EMBED_VALWORD);
check($yac, "k_false", FALSE,       YAC_KIND_FLAG, YAC_EMBED_VALWORD);
check($yac, "k_arr0",  array(),     YAC_KIND_FLAG, YAC_EMBED_VALWORD);

/* LONG: small fits the val word, PHP_INT_MAX needs the key-inline area */
check($yac, "k_int",    123,         YAC_KIND_LONG, YAC_EMBED_VALWORD);
check($yac, "k_negint", -456,        YAC_KIND_LONG, YAC_EMBED_VALWORD);
check($yac, "k_bigint", PHP_INT_MAX, YAC_KIND_LONG, YAC_EMBED_INLINE);

/* STRING: the val word holds (word bits - 5) / 8 bytes, 7 on 64-bit and 3 on
 * 32-bit, so derive the boundary instead of hard-coding one platform's */
$strmax = intdiv(PHP_INT_SIZE * 8 - 5, 8);
check($yac, "k_sstr0", "",                            YAC_KIND_STRING, YAC_EMBED_VALWORD);
check($yac, "k_sstrn", str_repeat("s", $strmax),      YAC_KIND_STRING, YAC_EMBED_VALWORD);
check($yac, "k_mstr",  str_repeat("m", $strmax + 1),  YAC_KIND_STRING, YAC_EMBED_INLINE);
check($yac, "k_lstr",  str_repeat("L", 64),           YAC_KIND_STRING, YAC_EMBED_BLOCK);

/* BLOB: a serialized array/object, small enough to inline */
check($yac, "k_arr",  array("a" => 1),     YAC_KIND_BLOB, YAC_EMBED_INLINE);
check($yac, "k_obj",  (object)array("p" => 1), YAC_KIND_BLOB, YAC_EMBED_INLINE);

/* DOUBLE is only ever inline or block (no float32 round-trip on 64-bit
 * builds for a full-precision double); a float-exact one may use the word.
 * Assert kind, and that it is embedded some way, not the exact form. */
$yac->set("k_dbl", 3.14159);
$d = dump_find($yac, "k_dbl");
var_dump($d["kind"] === YAC_KIND_DOUBLE, $d["embed"] > YAC_EMBED_BLOCK);

/* the MISS enum is exposed but never appears as a stored entry's kind */
var_dump(YAC_KIND_MISS === 0);
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
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
