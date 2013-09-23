--TEST--
ISSUE #12 segfault if use mmap&k_size bigger than 4M
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=8M
yac.values_memory_size=128M
--FILE--
<?php
$yac = new Yac();
$i = 0;
while (true) {
	$yac->set(rand(), strval(rand()), 2);
	$i++;
	if ($i > 20000) {
		break;
	}
}
?>
OKEY
--EXPECTF--
OKEY
