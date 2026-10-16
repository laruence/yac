--TEST--
Yac MINFO output via phpinfo()
--DESCRIPTION--
Covers PHP_MINFO_FUNCTION: version/shared-memory table, INI entries display
and the cache info table.
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
$yac->set("info_key", "info_value");

ob_start();
phpinfo(INFO_MODULES);
$out = ob_get_clean();

/* header table */
var_dump(strpos($out, "yac support") !== false);
var_dump(strpos($out, "Version => ") !== false);
var_dump(strpos($out, "Shared Memory => ") !== false);

/* INI entries are listed; the internal debug pause is never configurable. */
var_dump(strpos($out, "yac.enable") !== false);
var_dump(strpos($out, "yac.keys_memory_size") !== false);
var_dump(strpos($out, "yac.compress_threshold") !== false);
var_dump(strpos($out, "yac.debug") === false);
var_dump(strpos($out, "yac.test_pause_after_reserve_us") === false);
var_dump(defined("YAC_DEBUG") || strpos($out, "Debug Frontier Retries") === false);

/* cache info table with live counters */
var_dump(strpos($out, "Total Shared Memory Usage(memory_size)") !== false);
var_dump(strpos($out, "Size of Shared Memory Segment(segment_size)") !== false);
var_dump((bool)preg_match("/Total Used Slots\(slots_num\) => [1-9][0-9]*/", $out));
?>
--EXPECT--
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
bool(true)
bool(true)
