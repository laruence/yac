--TEST--
Yac::dump() honors the instance prefix
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac_a = new Yac("a_");
$yac_b = new Yac("b_");
$yac_raw = new Yac();

$yac_raw->flush();   /* the shared segment survives across runs */

$yac_a->set("k1", "v1");
$yac_a->set("k2", "v2");
$yac_b->set("k1", "v3");
$yac_raw->set("plain", "v4");

/* prefixed instance sees only its own entries, keys without the prefix */
$keys_a = array_map(fn($i) => $i["key"], $yac_a->dump(100));
sort($keys_a);
var_dump($keys_a);

/* a different prefix sees a different set */
$keys_b = array_map(fn($i) => $i["key"], $yac_b->dump(100));
var_dump($keys_b);

/* the prefix must not be leaked into the keys */
var_dump(in_array("a_k1", $keys_a));
var_dump(in_array("b_k1", $keys_a));

/* unprefixed instance: no filtering, sees every entry with prefixes intact */
$keys_raw = array_map(fn($i) => $i["key"], $yac_raw->dump(100));
sort($keys_raw);
var_dump($keys_raw);

/* a_->set("b_k1") is stored as a_b_k1; the a_ instance strips the prefix, showing it as "b_k1" */
$yac_a->set("b_k1", "v5");
$keys_a2 = array_map(fn($i) => $i["key"], $yac_a->dump(100));
sort($keys_a2);
var_dump($keys_a2);

/* unprefixed instance sees every entry, including a_b_k1 (now 5 entries) */
$keys_all = array_map(fn($i) => $i["key"], $yac_raw->dump(100));
sort($keys_all);
var_dump($keys_all);

/* offset counts only matching entries: skip the first of a_ */
$page = array_map(fn($i) => $i["key"], $yac_a->dump(100, 2));
sort($page);
var_dump(count($page));

/* dumped keys are directly usable with get() */
$yac_a->flush();
var_dump($yac_a->get("k1"));
?>
--EXPECTF--
array(2) {
  [0]=>
  string(2) "k1"
  [1]=>
  string(2) "k2"
}
array(1) {
  [0]=>
  string(2) "k1"
}
bool(false)
bool(false)
array(4) {
  [0]=>
  string(4) "a_k1"
  [1]=>
  string(4) "a_k2"
  [2]=>
  string(4) "b_k1"
  [3]=>
  string(5) "plain"
}
array(3) {
  [0]=>
  string(4) "b_k1"
  [1]=>
  string(2) "k1"
  [2]=>
  string(2) "k2"
}
array(5) {
  [0]=>
  string(6) "a_b_k1"
  [1]=>
  string(4) "a_k1"
  [2]=>
  string(4) "a_k2"
  [3]=>
  string(4) "b_k1"
  [4]=>
  string(5) "plain"
}
int(1)
bool(false)
