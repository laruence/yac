--TEST--
Yac compress_threshold below minimum is clamped to 1024
--DESCRIPTION--
yac.compress_threshold values below YAC_MIN_COMPRESS_THRESHOLD (1024) are
raised to the minimum by the INI handler. A 2000-byte compressible string
is therefore compressed (threshold effectively 1024), which dump() proves
by reporting a stored v_len smaller than the value length.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=100
--FILE--
<?php
$yac = new Yac();

/* raw ini_get reports the literal value ... */
var_dump(ini_get("yac.compress_threshold"));

/* ... but 2000 > clamped(1024) still triggers compression */
$v = str_repeat("c", 2000);
var_dump($yac->set("clamped", $v));
var_dump($yac->get("clamped") === $v);

foreach ($yac->dump() as $item) {
    if ($item["key"] === "clamped") {
        var_dump($item["v_len"] < 2000); /* stored compressed */
    }
}
?>
--EXPECT--
string(3) "100"
bool(true)
bool(true)
bool(true)
