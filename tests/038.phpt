--TEST--
Yac values_memory_size below segment minimum (4M) must not hang startup
--CREDITS--
Jarvis (AI assistant to Laruence)
--DESCRIPTION--
create_segments() halved segments_num while (v_size / segments_num) < 4M.
With v_size < 4M, segments_num reached 0 and v_size / 0 hung forever on
archs where integer division by zero does not trap (ARM), and crashed with
SIGFPE elsewhere. Now it falls back to a single segment.
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=1M
yac.values_memory_size=1M
--FILE--
<?php
$yac = new Yac();

/* basic round-trip with a sub-minimum values memory */
$yac->set("foo", "dummy");
var_dump($yac->get("foo"));

/* fill past the single 1M segment to force recycles; all sets must succeed
   and data written after a recycle must read back intact */
$ok = 0;
for ($i = 0; $i < 20; $i++) {
    $ok += $yac->set("k$i", str_repeat("x", 100000)) ? 1 : 0;
}
var_dump($ok);
var_dump($yac->get("k19") === str_repeat("x", 100000));
var_dump($yac->info()["recycles"] > 0);

/* a value larger than the whole segment fails cleanly, cache stays usable */
var_dump($yac->set("toobig", random_bytes(1100000)));
$yac->set("sanity", "ok");
var_dump($yac->get("sanity"));
?>
--EXPECTF--
%SWarning: %Syac.values_memory_size(1048576) is below the segment minimum(4194304), a single segment will be used in Unknown on line 0%A
string(5) "dummy"
int(20)
bool(true)
bool(true)

Warning: Yac::set(): Compression failed in %s038.php on line %d
bool(false)
string(2) "ok"
