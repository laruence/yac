--TEST--
Yac resource type triggers E_WARNING
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

/* 1. storing a resource directly — triggers warning, returns false */
$fh = fopen(__FILE__, "r");
var_dump(is_resource($fh));

ob_start();
$result = $yac->set("res_key", $fh);
$output = ob_get_clean();
var_dump($result);                  /* false — resource not stored */
var_dump($yac->get("res_key"));     /* NULL — not in cache */
fclose($fh);

/* 2. array containing a resource — the array is serialized as a whole */
/*    by the IS_ARRAY path in yac_add_impl, so it bypasses the IS_RESOURCE
 *    check. Whether it succeeds depends on the serializer (php/json/msgpack). */
$fh2 = fopen(__FILE__, "r");
ob_start();
$result = $yac->set("arr_with_res", ["normal" => 1, "bad" => $fh2]);
$output = ob_get_clean();
var_dump($result);                  /* true — array serialized, not per-element */
fclose($fh2);

/* 3. after resource attempt, cache still works */
$yac->set("sanity", "ok");
var_dump($yac->get("sanity"));
?>
--EXPECTF--
bool(true)
bool(false)
NULL
bool(true)
string(2) "ok"
