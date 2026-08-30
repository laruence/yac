--TEST--
Yac::get stops probing at an empty slot (empty-key miss must not crash)
--DESCRIPTION--
Inserts take the first empty probe slot and eviction only replaces full
slots, so a key can never live beyond an empty slot; find() may abort the
probe sequence there. Before that early abort, a miss could walk into an
empty probe slot whose zeroed header (hash 0, klen 0) exactly matches the
empty-string key (yac_hash of it is 0), pass the zero-length memcmp check
and dereference the NULL val pointer of the empty slot. This reproduces
only when the first slot of the chain is occupied by another key, which
this test arranges by picking a key whose hash lands in slot 0.
--SKIPIF--
<?php
if (!extension_loaded("yac")) print "skip";
if (PHP_INT_SIZE < 8) print "skip"; /* the replica assumes 64-bit ints */
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* PHP replica of yac_hash (MurmurHash64A) and its home/stride split.
 * 64-bit multiplies are done as 16-bit-limb schoolbook products (each
 * intermediate below 2^34, exact in a PHP int) because PHP promotes an
 * overflowing int product to a float, which would lose precision. */
function mul64(int $alo, int $ahi, int $blo, int $bhi): array {
    $a0 = $alo & 0xFFFF; $a1 = ($alo >> 16) & 0xFFFF;
    $a2 = $ahi & 0xFFFF; $a3 = ($ahi >> 16) & 0xFFFF;
    $b0 = $blo & 0xFFFF; $b1 = ($blo >> 16) & 0xFFFF;
    $b2 = $bhi & 0xFFFF; $b3 = ($bhi >> 16) & 0xFFFF;
    $p0 = $a0 * $b0;
    $p1 = $a0 * $b1 + $a1 * $b0;
    $p2 = $a0 * $b2 + $a1 * $b1 + $a2 * $b0;
    $p3 = $a0 * $b3 + $a1 * $b2 + $a2 * $b1 + $a3 * $b0;
    $d1 = ($p0 >> 16) + $p1;
    $d2 = ($d1 >> 16) + $p2;
    $d3 = ($d2 >> 16) + $p3;
    return [(($p0 & 0xFFFF) | (($d1 & 0xFFFF) << 16)),
            (($d2 & 0xFFFF) | (($d3 & 0xFFFF) << 16))];
}

function xor64(array $a, array $b): array { return [$a[0] ^ $b[0], $a[1] ^ $b[1]]; }

function shr64(array $a, int $n): array {
    if ($n >= 32) return [$a[1] >> ($n - 32), 0];
    return [(($a[0] >> $n) | (($a[1] << (32 - $n)) & 0xFFFFFFFF)), $a[1] >> $n];
}

function yac_hash(string $s): array {
    $h = mul64(strlen($s), 0, 0x5bd1e995, 0xc6a4a793); /* h = len * m */
    $len = strlen($s);
    $i = 0;
    while ($len - $i >= 8) {
        $klo = ord($s[$i]) | (ord($s[$i+1]) << 8) | (ord($s[$i+2]) << 16) | (ord($s[$i+3]) << 24);
        $khi = ord($s[$i+4]) | (ord($s[$i+5]) << 8) | (ord($s[$i+6]) << 16) | (ord($s[$i+7]) << 24);
        $k = mul64($klo, $khi, 0x5bd1e995, 0xc6a4a793);
        $k = xor64($k, shr64($k, 47));
        $k = mul64($k[0], $k[1], 0x5bd1e995, 0xc6a4a793);
        $h = xor64($h, $k);
        $h = mul64($h[0], $h[1], 0x5bd1e995, 0xc6a4a793);
        $i += 8;
    }
    $rem = $len - $i;
    for ($j = $rem - 1; $j >= 0; $j--) { /* tail: xor bytes high to low (case fallthrough) */
        $v = ord($s[$i + $j]);
        $sh = $j * 8;
        if ($sh < 32) $h = [$h[0] ^ ($v << $sh), $h[1]];
        else          $h = [$h[0], $h[1] ^ ($v << ($sh - 32))];
    }
    if ($rem > 0) $h = mul64($h[0], $h[1], 0x5bd1e995, 0xc6a4a793);
    $h = xor64($h, shr64($h, 47));
    $h = mul64($h[0], $h[1], 0x5bd1e995, 0xc6a4a793);
    return xor64($h, shr64($h, 47));
}

/* [home, stride] as the storage layer derives them: low bits of the hash
 * for home, a fold of the halves masked and forced odd for stride */
function home_stride(string $key, int $mask): array {
    $h = yac_hash($key);
    $home = $h[0] & $mask;
    $x2 = (($h[0] >> 16) | (($h[1] << 16) & 0xFFFFFFFF)); /* hash >> 16 */
    $stride = ((($h[1] ^ $x2 ^ $h[0]) & $mask)) | 1;
    return [$home, $stride];
}

$yac = new Yac();
$mask = $yac->info()["slots_size"] - 1;

/* find a key whose hash lands in slot 0, occupying the chain head */
for ($i = 0; ; $i++) {
    $keeper = "keeper_$i";
    if (home_stride($keeper, $mask)[0] === 0) break;
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
