--TEST--
DIAG: empty-key path forensics (runs before 043; never prints big payloads)
--DESCRIPTION--
Diagnostic twin of 043 for the Windows x86 failure: prints metadata only
(layout info, value types, lengths, head/tail hex, dump() snapshots) so a
huge garbage value can be identified without blowing run-tests.php's 128M
memory limit. Intentionally fails (EXPECT never matches) to force .out/.diff
capture. Remove once the 32-bit bug is fixed.
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

/* describe any value without ever printing a big string body */
function describe($v): string {
    if (!is_string($v)) {
        return var_export($v, true);
    }
    $n = strlen($v);
    $first_nz = strspn($v, "\0"); /* offset of first non-NUL byte (C-speed) */
    $body = $first_nz < $n ? bin2hex(substr($v, $first_nz, 64)) : "";
    return sprintf("string len=%d first_nz=%d body@first_nz=%s head=%s tail=%s",
        $n, $first_nz, $body,
        bin2hex(substr($v, 0, 32)),
        $n > 32 ? bin2hex(substr($v, -32)) : "-");
}

$yac = new Yac();
$info = $yac->info();
printf("layout slots_size=%d slots_used=%d segment_size=%d segment_num=%d mem=%d k=%d v=%d\n",
    $info["slots_size"], $info["slots_used"], $info["segment_size"], $info["segment_num"],
    $info["memory_size"], $info["slots_memory_size"], $info["values_memory_size"]);

$mask = $info["slots_size"] - 1;
printf("mask=%x ptr_size=%d\n", $mask, PHP_INT_SIZE);

for ($i = 0; ; $i++) {
    $keeper = "keeper_$i";
    if ((murmur2($keeper) & $mask) === 0) break;
    if ($i > 500000) die("keeper search failed");
}
printf("keeper=%s murmur=%x\n", $keeper, murmur2($keeper));
printf("set(keeper,1): %s\n", describe($yac->set($keeper, 1)));

printf("dump#1: %s\n", json_encode($yac->dump()));
printf("get(\"\")#1: %s\n", describe($yac->get("")));
printf("dump#2: %s\n", json_encode($yac->dump()));

printf("set(\"\",empty): %s\n", describe($yac->set("", "empty")));
printf("get(\"\")#2: %s\n", describe($yac->get("")));
printf("dump#3: %s\n", json_encode($yac->dump()));

printf("delete(\"\"): %s\n", describe($yac->delete("")));
printf("get(\"\")#3: %s\n", describe($yac->get("")));
printf("dump#4: %s\n", json_encode($yac->dump()));
printf("info_end slots_used=%d miss=%d hits=%d fails=%d kicks=%d recycles=%d\n",
    $yac->info()["slots_used"], $yac->info()["miss"], $yac->info()["hits"],
    $yac->info()["fails"], $yac->info()["kicks"], $yac->info()["recycles"]);
?>
--EXPECT--
DIAGNOSTIC OUTPUT WILL NEVER MATCH
