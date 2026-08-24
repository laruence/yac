--TEST--
Yac multi-process concurrent set/get
--SKIPIF--
<?php
if (!extension_loaded("yac")) print "skip";
if (!extension_loaded("pcntl")) print "skip need pcntl";
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac("mp_");

if (($pid = pcntl_fork())) {
    /* Parent: write some keys, wait for child, then verify */
    for ($i = 1; $i <= 50; $i++) {
        $yac->set("p_key_$i", "parent_$i");
    }

    pcntl_wait($status);

    /* Parent keys should still be readable after child runs */
    $found = 0;
    for ($i = 1; $i <= 50; $i++) {
        if ($yac->get("p_key_$i") === "parent_$i") {
            $found++;
        }
    }
    var_dump($found > 0);
    var_dump($found);
    echo "Parent done\n";

} else {
    /* Child: can read parent's keys and write its own */
    for ($i = 1; $i <= 30; $i++) {
        $yac->set("c_key_$i", "child_$i");
    }

    $child_found = 0;
    for ($i = 1; $i <= 30; $i++) {
        if ($yac->get("c_key_$i") === "child_$i") {
            $child_found++;
        }
    }
    echo "child_found=$child_found\n";
    exit(0);
}
?>
--EXPECTF--
child_found=30
bool(true)
int(50)
Parent done
