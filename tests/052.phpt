--TEST--
Yac::delete misses when the whole probe chain is occupied
--DESCRIPTION--
Both find() and delete() probe at most 4 slots (home slot plus 3 steps of
the stride hash). When all four slots are occupied by other keys,
delete() must scan the full chain and return false without touching the
colliding entries. The test reconstructs yac's hash in PHP (MurmurHash64A
with 16-bit-limb multiplication, since PHP promotes overflowing int
products to floats), discovers the slot count from info(), then searches
for a key whose four probe slots are all fillable.
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
 * intermediate below 2^34, exact in a PHP int). */
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

/* the 4-slot probe path the storage layer walks for this key */
function probe_path(string $key, int $mask): array {
    $h = yac_hash($key);
    $x2 = (($h[0] >> 16) | (($h[1] << 16) & 0xFFFFFFFF)); /* hash >> 16 */
    $stride = ((($h[1] ^ $x2 ^ $h[0]) & $mask)) | 1;
    $path = array();
    $pos = $h[0] & $mask;
    for ($j = 0; $j < 4; $j++) {
        $path[] = $pos;
        $pos = ($pos + $stride) & $mask;
    }
    return $path;
}

$yac = new Yac();
$S = $yac->info()["slots_size"];
$mask = $S - 1;

/* build a slot -> filler-key map: one key per slot, inserted in no
   particular order, so each filler really lands on its own home slot */
$byslot = array();
for ($i = 0; $i < 100000; $i++) {
    $k = sprintf("fill%06d", $i);
    $slot = probe_path($k, $mask)[0];
    if (!isset($byslot[$slot])) $byslot[$slot] = $k;
}

/* find an absent key whose home slot and three stride probes are all
   occupied by fillers */
$miss = null; $fkeys = array();
for ($i = 0; $i < 100000; $i++) {
    $k = "miss" . $i;
    $need = probe_path($k, $mask);
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
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
