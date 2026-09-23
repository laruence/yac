--TEST--
Inline values (stored in the slot's unused key area)
--DESCRIPTION--
Values that miss the val word but fit the slot's key area (klen + size <= 48)
are stored inline after the key, block-less: dump() shows embedded with a real v_len.
Covers round-trips, the capacity boundary, form transitions, NUL bytes,
overwrite, incr() refusal, add()/delete()/ttl.
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
function dump_find_065($yac, $key) {
	foreach ($yac->dump(100000) as $item) {
		if ($item["key"] === $key) {
			return $item;
		}
	}
	return NULL;
}

$yac = new Yac();
$yac->flush();

/* round-trips: string too long for the val word, double, big int, array */
$s8 = "12345678";
var_dump($yac->set("t_str", $s8));
var_dump($yac->get("t_str") === $s8);
var_dump($yac->set("t_dbl", 3.14));
var_dump($yac->get("t_dbl") === 3.14);
var_dump($yac->set("t_big", PHP_INT_MAX));
var_dump($yac->get("t_big") === PHP_INT_MAX);
$arr = array("a" => 1, "b" => "two");
var_dump($yac->set("t_arr", $arr));
var_dump($yac->get("t_arr") == $arr);

/* dump shape: embedded, real v_len, no block metadata */
$item = dump_find_065($yac, "t_str");
var_dump($item["embedded"], $item["v_len"], $item["size"], $item["crc"]);
$item = dump_find_065($yac, "t_arr");
var_dump($item["embedded"], $item["v_len"] > 0, $item["size"]);

/* boundary: klen(3) + 45 = 48 stores inline, 46 spills to a block */
var_dump($yac->set("t44", str_repeat("x", 45)));
var_dump(dump_find_065($yac, "t44")["embedded"]);
var_dump($yac->get("t44") === str_repeat("x", 45));
var_dump($yac->set("t44", str_repeat("x", 46)));
var_dump(dump_find_065($yac, "t44")["embedded"]);
var_dump($yac->get("t44") === str_repeat("x", 46));

/* NUL bytes survive the inline copy */
$nul = "a\0b\0c\0d\0e";
var_dump($yac->set("t_nul", $nul));
var_dump($yac->get("t_nul") === $nul);

/* transitions on one key: val word -> inline -> block -> inline */
var_dump($yac->set("t_flip", "abc"));
var_dump(dump_find_065($yac, "t_flip")["v_len"]);
var_dump($yac->set("t_flip", str_repeat("y", 20)));
var_dump($yac->get("t_flip") === str_repeat("y", 20));
var_dump(dump_find_065($yac, "t_flip")["embedded"]);
var_dump($yac->set("t_flip", str_repeat("z", 64)));
var_dump($yac->get("t_flip") === str_repeat("z", 64));
var_dump(dump_find_065($yac, "t_flip")["embedded"]);
var_dump($yac->set("t_flip", str_repeat("w", 12)));
var_dump($yac->get("t_flip") === str_repeat("w", 12));
var_dump(dump_find_065($yac, "t_flip")["embedded"]);

/* overwrite an inline value with a different size */
var_dump($yac->set("t_grow", str_repeat("g", 10)));
var_dump($yac->set("t_grow", str_repeat("h", 40)));
var_dump($yac->get("t_grow") === str_repeat("h", 40));
var_dump($yac->set("t_grow", str_repeat("i", 15)));
var_dump($yac->get("t_grow") === str_repeat("i", 15));

/* incr refuses an inline long: only val-word embeds count */
var_dump($yac->incr("t_big"));
var_dump($yac->get("t_big") === PHP_INT_MAX);

/* add()/delete()/ttl on inline entries */
var_dump($yac->add("t_str", "nope"));
var_dump($yac->get("t_str") === $s8);
var_dump($yac->delete("t_str"));
var_dump($yac->get("t_str"));
var_dump($yac->set("t_ttl", str_repeat("t", 20), 1));
var_dump($yac->get("t_ttl") === str_repeat("t", 20));
sleep(2);
var_dump($yac->get("t_ttl"));

$yac->flush();
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
int(8)
int(0)
int(0)
bool(true)
bool(true)
int(0)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
int(3)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
