--TEST--
Yac::delete misses when the whole probe chain is occupied
--DESCRIPTION--
Both find() and delete() probe at most 4 slots (home slot plus 3 steps of
the secondary hash). When all four slots are occupied by other keys,
delete() must scan the full chain and return false without touching the
colliding entries. The test reconstructs yac's hash functions in PHP,
discovers the slot count from dump() indices, then searches for a key
whose four probe slots are all fillable.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* 32-bit-safe helpers: on builds where zend_long is 32 bits, full-width
 * int multiplications would overflow into doubles and lose bits, so the
 * multiply is decomposed into 16-bit partial products (each < 2^53, exact
 * in doubles) and the result is folded back into a signed 32-bit int.
 * The shifts in murmur are logical (unsigned), which PHP only gives for
 * free on non-negative ints, hence the lsr32() wrapper. */
function imul32(int $a, int $b): int {
    $al = $a & 0xFFFF; $ah = ($a >> 16) & 0xFFFF;
    $bl = $b & 0xFFFF; $bh = ($b >> 16) & 0xFFFF;
    $r = fmod($al * $bl + fmod($al * $bh + $ah * $bl, 65536.0) * 65536.0,
              4294967296.0);
    return $r >= 2147483648.0 ? (int)($r - 4294967296.0) : (int)$r;
}

function lsr32(int $v, int $n): int {
    return $v >= 0 ? $v >> $n : (($v >> $n) & ((1 << (32 - $n)) - 1));
}

/* replica of yac_inline_hash_func1 (MurmurHash2, 32-bit) */
function murmur(string $data): int {
    $m = 0x5bd1e995;
    $len = strlen($data);
    $h = $len;
    $i = 0;
    while ($len - $i >= 4) {
        $k = ord($data[$i]) | (ord($data[$i+1]) << 8)
           | (ord($data[$i+2]) << 16) | (ord($data[$i+3]) << 24);
        $k = imul32($k, $m);
        $k ^= lsr32($k, 24);
        $k = imul32($k, $m);
        $h = imul32($h, $m);
        $h ^= $k;
        $i += 4;
    }
    $rem = $len - $i;
    if ($rem >= 3) $h ^= ord($data[$i+2]) << 16;
    if ($rem >= 2) $h ^= ord($data[$i+1]) << 8;
    if ($rem >= 1) { $h ^= ord($data[$i]); $h = imul32($h, $m); }
    $h ^= lsr32($h, 13);
    $h = imul32($h, $m);
    $h ^= lsr32($h, 15);
    return $h;
}

/* yac_inline_hash_func2 (DJBX33A) reduced mod $mod each step; only the low
   bits are ever used by the storage layer (seed & slots_mask), and $mod is
   a power of two, so the reduction is exact. Accumulate in doubles:
   $h < $mod <= 2^26 keeps $h * 33 + c < 2^53, which is exact on every
   build, unlike a zend_long accumulation on 32-bit platforms */
function djb2mod(string $key, int $mod): int {
    $h = 5381 % $mod;
    $n = strlen($key);
    for ($i = 0; $i < $n; $i++) {
        $h = fmod($h * 33.0 + ord($key[$i]), $mod);
    }
    return (int)$h;
}

$yac = new Yac();

/* discover the slot count: store a few keys and find the power-of-two
   size consistent with every observed dump() index */
$probe = array("probe_one", "probe_two", "probe_three", "probe_four");
foreach ($probe as $k) $yac->set($k, 1);
$idx = array();
foreach ($yac->dump() as $item) {
    if (in_array($item["key"], $probe, true)) $idx[$item["key"]] = $item["index"];
}
$S = 0;
for ($p = 10; $p <= 26; $p++) {
    $cand = 1 << $p;
    $ok = true;
    foreach ($idx as $k => $i) {
        if ((murmur($k) & ($cand - 1)) !== $i) { $ok = false; break; }
    }
    if ($ok) { $S = $cand; break; }
}
if (!$S) die("could not discover slot count");
$mask = $S - 1;
$yac->flush();

/* build a slot -> filler-key map */
$byslot = array();
for ($i = 0; $i < 100000; $i++) {
    $k = sprintf("fill%06d", $i);
    $slot = murmur($k) & $mask;
    if (!isset($byslot[$slot])) $byslot[$slot] = $k;
}

/* find an absent key whose home slot and three secondary probes are all
   occupied by fillers */
$miss = null; $fkeys = array();
for ($i = 0; $i < 100000; $i++) {
    $k = "miss" . $i;
    $b = murmur($k) & $mask;
    $d = djb2mod($k, $S);
    $need = array($b, ($b + $d) & $mask, ($b + 2 * $d) & $mask, ($b + 3 * $d) & $mask);
    $f = array();
    foreach ($need as $slot) {
        if (!isset($byslot[$slot])) continue 2;
        $f[] = $byslot[$slot];
    }
    if (count(array_unique($f)) !== 4) continue; /* defensive */
    $miss = $k; $fkeys = $f;
    break;
}
if ($miss === null) die("no collision chain found");

/* occupy all four probe slots */
foreach ($fkeys as $n => $fk) $yac->set($fk, "filler_$n");

/* fillers really sit on the expected slots */
$where = array();
foreach ($yac->dump() as $item) $where[$item["key"]] = $item["index"];
var_dump(count($where) === 4);

/* delete of the absent colliding key scans the full chain, then fails */
var_dump($yac->delete($miss));
/* get of it also misses after four probes */
var_dump($yac->get($miss));
/* nothing was disturbed */
foreach ($fkeys as $n => $fk) var_dump($yac->get($fk) === "filler_$n");
?>
--EXPECT--
bool(true)
bool(false)
NULL
bool(true)
bool(true)
bool(true)
bool(true)
