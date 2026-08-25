--TEST--
Yac fails counter increments when a value cannot fit in values memory
--DESCRIPTION--
With a single 1M segment, a 1MB value plus serialization overhead cannot be
allocated; set() must fail cleanly and bump info()'s fails counter instead
of corrupting state.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
display_startup_errors=0
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=1M
--FILE--
<?php
$yac = new Yac();

var_dump($yac->info()["fails"]);

/* an uncompressed 1MB value does not fit a 1M segment once serialized */
var_dump($yac->set("big", str_repeat("a", 1048576)));
var_dump($yac->info()["fails"] > 0);
var_dump($yac->get("big"));

/* the cache stays usable after a failed allocation */
var_dump($yac->set("small", "ok"));
var_dump($yac->get("small"));
?>
--EXPECTF--
PHP Warning:  yac.values_memory_size(1048576) is below the segment minimum(4194304), a single segment will be used in Unknown on line 0%A
int(0)
bool(false)
bool(true)
bool(false)
bool(true)
string(2) "ok"
