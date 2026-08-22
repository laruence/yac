--TEST--
Yac::dump() default limit and explicit limit
--CREDITS--
Jarvis (AI assistant to Laruence)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();

/* 1. dump on empty cache */
$empty = $yac->dump();
var_dump($empty);  /* empty array */

/* 2. populate 150 keys */
for ($i = 0; $i < 150; $i++) {
    $yac->set("dump_key_$i", "value_$i");
}

/* 3. dump() without argument — defaults to 100 */
$default = $yac->dump();
var_dump(is_array($default));
var_dump(count($default));

/* 4. dump(50) — explicit limit */
$limited = $yac->dump(50);
var_dump(count($limited));

/* 5. dump(200) — limit larger than total keys, returns all */
$all = $yac->dump(200);
var_dump(count($all));

/* 6. each entry has expected keys */
$first = $all[0];
var_dump(isset($first["key"]));
var_dump(isset($first["ttl"]));
var_dump(isset($first["index"]));
var_dump(isset($first["hash"]));
var_dump(isset($first["crc"]));
var_dump(isset($first["atime"]));
var_dump(is_int($first["index"]));
var_dump(is_int($first["ttl"]));
var_dump(is_int($first["atime"]));
var_dump(is_string($first["key"]));
?>
--EXPECTF--
array(0) {
}
bool(true)
int(100)
int(50)
int(150)
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
