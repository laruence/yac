--TEST--
Yac compress_threshold above maximum is clamped to 1M
--DESCRIPTION--
yac.compress_threshold values above YAC_STORAGE_MAX_ENTRY_LEN (1M, the
largest size a single stored entry can have) are lowered to that maximum
by the INI handler. A 1.25MB value is therefore still above the effective
threshold and gets compressed, which dump() proves by reporting a c_len
much smaller than the value length.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=2097152
--FILE--
<?php
$yac = new Yac();

/* raw ini_get reports the literal value ... */
var_dump(ini_get("yac.compress_threshold"));

/* ... but 1.25M > clamped(1M) still triggers compression */
$v = str_repeat("a", 1310720);
var_dump($yac->set("clampedmax", $v));
var_dump($yac->get("clampedmax") === $v);

foreach ($yac->dump() as $item) {
    if ($item["key"] === "clampedmax") {
        var_dump($item["c_len"] < 1310720); /* stored compressed */
    }
}
?>
--EXPECT--
string(7) "2097152"
bool(true)
bool(true)
bool(true)
