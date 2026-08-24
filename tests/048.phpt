--TEST--
Yac value still too large after compression
--DESCRIPTION--
Three paths around the compression branch:
1) a barely compressible string (chained hashes) whose LZ4 output still
   exceeds YAC_STORAGE_MAX_ENTRY_LEN (1MB) -> "Value is too long";
2) the same payload serialized as an array -> "Value is too big";
3) an incompressible serialized payload -> compression is skipped.
The plain-string "compression grows the value" case is covered in 035.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
memory_limit=256M
--FILE--
<?php
$yac = new Yac();

/* deterministic pseudo-random data: compresses slightly (hash-chain
   runs are recognized as matches) but the output stays ~raw size,
   so 1.3MB compresses to > 1MB (YAC_STORAGE_MAX_ENTRY_LEN) */
$base = "";
$seed = "seed";
for ($i = 0; $i < 16640; $i++) { /* 16640 * 80 bytes ~= 1.3MB */
    $seed = hash("sha256", $seed);
    $base .= $seed . $seed;
}

/* 1. string path: compressed but still over the entry limit */
var_dump($yac->set("big_raw", $base));
var_dump($yac->get("big_raw"));

/* 2. array/object path: serialized, compressed, still over the limit */
var_dump($yac->set("big_arr", array("payload" => $base)));
var_dump($yac->get("big_arr"));

/* 3. array/object path: incompressible payload -> compression grows and
   fails cleanly */
var_dump($yac->set("rnd_arr", array("payload" => random_bytes(1200000))));
var_dump($yac->get("rnd_arr"));
?>
--EXPECTF--
Warning: Yac::set(): Value is too long(%d bytes) to be stored in %s048.php on line %d
bool(false)
bool(false)

Warning: Yac::set(): Value is too big to be stored in %s048.php on line %d
bool(false)
bool(false)

Warning: Yac::set(): Compression makes the value larger(%d -> %d bytes), skipped in %s048.php on line %d
bool(false)
bool(false)
