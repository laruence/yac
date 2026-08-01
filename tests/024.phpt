--TEST--
Check for yac key reused
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
