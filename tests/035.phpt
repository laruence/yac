--TEST--
Yac compression round-trip with threshold boundary
--CREDITS--
Jarvis (AI assistant to Laruence)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
$yac = new Yac();

/* 1. value below threshold — NOT compressed (stored as-is) */
$small = str_repeat("x", 512);
$yac->set("small", $small);
var_dump($yac->get("small") === $small);

/* 2. value at exactly threshold — NOT compressed */
$exact = str_repeat("y", 1024);
$yac->set("exact", $exact);
var_dump($yac->get("exact") === $exact);

/* 3. value above threshold — compressed, then decompressed on read */
$large = str_repeat("z", 2048);
$yac->set("large", $large);
var_dump($yac->get("large") === $large);

/* 4. highly compressible (single char repeat × 2048) — small compressed payload */
$repeated = str_repeat("A", 2048);
$yac->set("repeated", $repeated);
var_dump($yac->get("repeated") === $repeated);

/* 5. array above threshold — serialized then compressed */
$big_arr = [];
for ($i = 0; $i < 100; $i++) {
    $big_arr["key_$i"] = str_repeat("data", 10); /* 40 bytes each */
}
$yac->set("big_arr", $big_arr);
$result = $yac->get("big_arr");
var_dump(count($result));
var_dump($result["key_0"]);
var_dump($result["key_99"]);
?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
bool(true)
int(100)
string(40) "datadatadatadatadatadatadatadatadatadata"
string(40) "datadatadatadatadatadatadatadatadatadata"
