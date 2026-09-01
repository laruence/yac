--TEST--
Yac compress_threshold of -1 disables compression for values below 1M
--DESCRIPTION--
-1 parses to (unsigned long)-1, which no value length can exceed, so
compression is never attempted. The clamping must not touch the sentinel:
a 5000-byte value is stored uncompressed and dump() reports no c_len.
(Values above the 1M stored-entry limit still get a compression attempt
from the size guard, which is what keeps them storable at all.)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=-1
--FILE--
<?php
$yac = new Yac();

$v = str_repeat("x", 5000);
var_dump($yac->set("raw", $v));
var_dump($yac->get("raw") === $v);

foreach ($yac->dump() as $item) {
    if ($item["key"] === "raw") {
        var_dump(isset($item["c_len"])); /* no c_len: stored uncompressed */
        var_dump($item["v_len"]);
    }
}
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
int(5000)
