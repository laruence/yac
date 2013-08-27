--TEST--
Check for Yac::dump
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
$yac = new Yac();

for ($i = 0; $i<100; $i++) {
	$yac->set("key". $i, "kjslkdfkldasjkf");
}
for ($i = 0; $i<100; $i++) {
	$yac->set("key". $i, "kjslkdfkldasjkf");
}

var_dump(count($yac->dump(1000)));
--EXPECTF--
int(100)
