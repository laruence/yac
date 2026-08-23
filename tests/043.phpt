--TEST--
Yac::get stops probing at an empty slot (empty-key miss must not crash)
--CREDITS--
Jarvis (AI assistant to Laruence)
--DESCRIPTION--
Inserts take the first empty probe slot and eviction only replaces full
slots, so a key can never live beyond an empty slot; find() may abort the
probe sequence there. Before that early abort, a miss could walk into an
empty probe slot whose zeroed header (hash 0, klen 0) exactly matches the
empty-string key (murmur2 of it is 0), pass the zero-length memcmp check
and dereference the NULL val pointer of the empty slot. This reproduces
only when the first slot of the chain is occupied by another key, which
this test arranges by picking a key whose hash lands in slot 0.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* MurmurHash2, identical to yac_inline_hash_func1 (32-bit semantics) */
function murmur2(string $s): int {
    $len = strlen($s);
    $h = $len;
    $i = 0;
    while ($len >= 4) {
        $k = ord($s[$i]) | (ord($s[$i + 1]) << 8) | (ord($s[$i + 2]) << 16) | (ord($s[$i + 3]) << 24);
        $k = (($k * 0x5bd1e995) & 0xffffffff);
        $k ^= ($k >> 24);
        $k = (($k * 0x5bd1e995) & 0xffffffff);
        $h = (($h * 0x5bd1e995) & 0xffffffff);
        $h ^= $k;
        $i += 4;
        $len -= 4;
    }
    switch ($len) {
        case 3: $h ^= ord($s[$i + 2]) << 16;
        case 2: $h ^= ord($s[$i + 1]) << 8;
        case 1: $h ^= ord($s[$i]); $h = (($h * 0x5bd1e995) & 0xffffffff);
    }
    $h ^= ($h >> 13);
    $h = (($h * 0x5bd1e995) & 0xffffffff);
    $h ^= ($h >> 15);
    return $h & 0xffffffff;
}

$yac = new Yac();
$mask = $yac->info()["slots_size"] - 1;

/* find a key whose hash lands in slot 0, occupying the chain head */
for ($i = 0; ; $i++) {
    $keeper = "keeper_$i";
    if ((murmur2($keeper) & $mask) === 0) break;
    if ($i > 500000) die("keeper search failed");
}
var_dump($yac->set($keeper, 1));

/* empty-string key: hash 0, klen 0. The chain head is occupied, so the
   old code entered the probe loop, matched the zeroed empty slot header
   and dereferenced its NULL val pointer. Must be a clean miss now. */
var_dump($yac->get(""));

/* empty key roundtrip still works */
var_dump($yac->set("", "empty"));
var_dump($yac->get(""));
var_dump($yac->delete(""));
var_dump($yac->get(""));
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
string(5) "empty"
bool(true)
bool(false)
