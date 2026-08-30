--TEST--
Check for yac key reused
--DESCRIPTION--
Eviction tie-break regression. The keys memory is only 1K, which shrinks
to 4 slots, so inserting 18 keys forces evictions. Every set lands within
the same second, leaving the candidates' atimes tied, so pick_victim's
tie-break decides who goes: on a tie the entry at the earliest probe
position is evicted, because the evicted slot is inherited by the incoming
key and the closer it sits to its home slot the cheaper every future
lookup becomes. dump() emits slots in reverse order (the list is built by
pushing to its head), so dump()[0] is the highest occupied slot; its key
is pinned by that rule and asserted here.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=1K
yac.values_memory_size=32M
--FILE--
<?php 
$short_keys = array();
$long_keys = array();

$yac = new Yac();
$i = 0;
while (++$i < 10) {
	$long_keys[] = $i . str_pad("x", 40, "x");
	$short_keys[] = $i . str_pad("o", 20, "o");
}

$i=0;
while (++$i < 10) {
	$yac->set($long_keys[$i-1], "dummy");
}
$i = 0;
while (++$i < 10) {
	$yac->set($short_keys[$i-1], "dummy");
}
$info = $yac->dump();
print($info[0]['key']);
?>
--EXPECTF--
%doooooooooooooooooooo
