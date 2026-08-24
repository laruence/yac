--TEST--
Yac::add() must not fail when inserting requires evicting a valid entry
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=256K
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();
$yac->flush();

/* Over-fill the slot table (~10x) so that every subsequent insert of a NEW key
   lands on probes that are all occupied by valid entries, forcing eviction.
   slots_size is read at runtime so the test holds on any platform/segment size. */
$slots = $yac->info()["slots_size"];
for ($i = 0; $i < $slots * 10; $i++) {
    $yac->set("warm_$i", "v");
}

/* Each add() targets a key that does NOT exist. Before the fix, the eviction
   path re-used do_update() while `k` still described the evicted (different)
   key, so `add` saw "valid + not expired" and wrongly returned false. */
$ok = true;
for ($i = 0; $i < 1000; $i++) {
    if ($yac->add("brand_new_$i", "nv_$i") !== true) {
        $ok = false;
    }
}
var_dump($ok);

/* the most recently added key is present and readable */
var_dump($yac->get("brand_new_999"));

/* control: add() of a key that DOES exist still returns false */
$yac->set("exists", "old");
var_dump($yac->add("exists", "new"));
var_dump($yac->get("exists"));
?>
--EXPECTF--
bool(true)
string(6) "nv_999"
bool(false)
string(3) "old"
