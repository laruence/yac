--TEST--
Yac::dump() offset: skip leading entries, empty array past the end
--DESCRIPTION--
dump($limit, $offset) skips the first $offset occupied slots. Paging with
offset partitions the entries; an offset at or beyond the entry count (or
an empty cache) returns an empty array.
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

/* an empty cache dumps an empty array */
var_dump($yac->dump());

$yac->set("a", 1);
$yac->set("b", 2);
$yac->set("c", 3);

var_dump(count($yac->dump()));
var_dump(count($yac->dump(2)));
var_dump(count($yac->dump(100)));

/* paging with offset partitions all entries without overlap */
$all = array();
$off = 0;
while (!empty(($page = $yac->dump(1, $off)))) {
	$all[] = $page[0]["key"];
	++$off;
}
sort($all);
var_dump($all);

/* an offset at or beyond the entry count returns an empty array */
var_dump($yac->dump(10, 3));
var_dump($yac->dump(1, 100));
?>
--EXPECT--
array(0) {
}
int(3)
int(2)
int(3)
array(3) {
  [0]=>
  string(1) "a"
  [1]=>
  string(1) "b"
  [2]=>
  string(1) "c"
}
array(0) {
}
array(0) {
}
