--TEST--
Yac values_memory_size below the 4M segment size rounds up to one segment
--DESCRIPTION--
With every value segment fixed at 4M, a values_memory_size below that still
yields a single 4M segment (the old create_segments() hang regressed here,
when segments_num was halved to zero and divided by). Fill past 4M to prove
the segment recycles, and that post-recycle data reads back intact.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
display_startup_errors=0
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=1M
yac.values_memory_size=1M
yac.compress_threshold=-1
--FILE--
<?php
$yac = new Yac();

/* basic round-trip with a sub-segment values memory */
$yac->set("foo", "dummy");
var_dump($yac->get("foo"));

/* fill past the single 4M segment to force recycles; all sets must succeed
   and data written after a recycle must read back intact */
$ok = 0;
for ($i = 0; $i < 50; $i++) {
    $ok += $yac->set("k$i", str_repeat("x", 100000)) ? 1 : 0;
}
var_dump($ok);
var_dump($yac->get("k49") === str_repeat("x", 100000));
var_dump($yac->info()["recycles"] > 0);

/* an incompressible value fails cleanly (compression would grow it),
   cache stays usable */
var_dump($yac->set("toobig", random_bytes(1100000)));
$yac->set("sanity", "ok");
var_dump($yac->get("sanity"));
?>
--EXPECTF--
PHP Warning:  yac.values_memory_size(1048576) is below the segment size(4194304), a single segment will be used in Unknown on line 0%A
string(5) "dummy"
int(50)
bool(true)
bool(true)

Warning: Yac::set(): Compression makes the value larger(%d -> %d bytes), skipped in %s038.php on line %d
bool(false)
string(2) "ok"
