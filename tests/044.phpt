--TEST--
Embedded scalar values (tagged val pointers)
--DESCRIPTION--
NULL/bool/small ints/strings up to 7 bytes/empty arrays are stored embedded
in the slot's val pointer (low 3 tag bits) without allocating a value
block. This covers round-trips, the large-int fallback to the block path,
embedded/block transitions on the same key, add() semantics, delete and ttl.
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
$yac = new Yac();

/* scalars that are stored inline (values chosen to be inline on both
   32-bit and 64-bit builds) */
var_dump($yac->set("k_null", NULL));
var_dump($yac->set("k_true", TRUE));
var_dump($yac->set("k_false", FALSE));
var_dump($yac->set("k_zero", 0));
var_dump($yac->set("k_int", 123456789));
var_dump($yac->set("k_neg", -123456789));
var_dump($yac->set("k_empty_str", ""));
var_dump($yac->set("k_sstr1", "1"));
var_dump($yac->set("k_sstr3", "abc"));
var_dump($yac->set("k_sstr7", "abcdefg"));
var_dump($yac->set("k_nul", "a\0b"));
var_dump($yac->set("k_empty_arr", array()));

var_dump($yac->get("k_null"));
var_dump($yac->get("k_true"));
var_dump($yac->get("k_false"));
var_dump($yac->get("k_zero"));
var_dump($yac->get("k_int"));
var_dump($yac->get("k_neg"));
var_dump($yac->get("k_empty_str"));
var_dump($yac->get("k_sstr1"));
var_dump($yac->get("k_sstr3"));
var_dump($yac->get("k_sstr7"));
var_dump(bin2hex($yac->get("k_nul")));
var_dump($yac->get("k_empty_arr"));

/* a big int must round-trip through the block path on any platform */
var_dump($yac->set("k_big", PHP_INT_MAX));
var_dump($yac->get("k_big") === PHP_INT_MAX);

/* doubles always use the block path */
var_dump($yac->set("k_dbl", 3.14));
var_dump($yac->get("k_dbl"));

/* type transitions on the same key: inline -> block -> inline */
var_dump($yac->set("k_flip", 42));
var_dump($yac->set("k_flip", str_repeat("x", 64)));
var_dump($yac->get("k_flip") === str_repeat("x", 64));
var_dump($yac->set("k_flip", FALSE));
var_dump($yac->get("k_flip"));

/* add() must not overwrite a live inline entry */
var_dump($yac->add("k_int", 1));
var_dump($yac->get("k_int"));

/* delete and ttl */
var_dump($yac->delete("k_int"));
var_dump($yac->get("k_int"));
var_dump($yac->set("k_ttl", "1", 1));
var_dump($yac->get("k_ttl"));
sleep(2);
var_dump($yac->get("k_ttl"));
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
NULL
bool(true)
bool(false)
int(0)
int(123456789)
int(-123456789)
string(0) ""
string(1) "1"
string(3) "abc"
string(7) "abcdefg"
string(6) "610062"
array(0) {
}
bool(true)
bool(true)
bool(true)
float(3.14)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
int(123456789)
bool(true)
bool(false)
bool(true)
string(1) "1"
bool(false)
