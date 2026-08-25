--TEST--
Yac rapid set/delete alternation — no dirty reads
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac("rapid_");
$errors = 0;
$keys = ["k1", "k2", "k3", "k4", "k5"];

for ($cycle = 0; $cycle < 100; $cycle++) {
    foreach ($keys as $key) {
        /* Set a value */
        if (!$yac->set($key, "cycle_$cycle")) {
            $errors++;
        }

        /* Read it back */
        $val = $yac->get($key);
        if ($val !== null && $val !== "cycle_$cycle") {
            /* Dirty read: got a value from a different cycle */
            $errors++;
        }

        /* Delete it */
        $yac->delete($key);

        /* Read after delete */
        $after = $yac->get($key);
        if ($after !== null) {
            /* Stale read after delete */
            $errors++;
        }

        /* Re-set for next cycle */
        $yac->set($key, "reset_$cycle");
    }
}

/* Should complete with zero errors */
var_dump($errors);
var_dump($yac->get("k1")); /* should be from last cycle */
?>
--EXPECTF--
int(0)
string(%d) "reset_%d"
