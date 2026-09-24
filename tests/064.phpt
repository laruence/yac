--TEST--
isset()/empty()/property_exists() on Yac map to stored entries
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();
$yac->flush();

$yac->set("i", 123);
$yac->set("s", "str");
$yac->set("n", null);
$yac->set("f", false);
$yac->set("e", "");
$yac->set("z", 0);
$yac->set("big", str_repeat("x", 100));   /* block value path */

/* isset(): stored and not null. A stored null reads like a miss here, which
 * is the standard isset() contract */
var_dump(isset($yac->i), isset($yac->s), isset($yac->n), isset($yac->f),
        isset($yac->e), isset($yac->z), isset($yac->big), isset($yac->miss));

/* empty(): stored and falsy, or absent */
var_dump(empty($yac->i), empty($yac->n), empty($yac->f), empty($yac->e),
        empty($yac->z), empty($yac->big), empty($yac->miss));

/* property_exists(): stored at all, a null included */
var_dump(property_exists($yac, "i"), property_exists($yac, "n"),
        property_exists($yac, "f"), property_exists($yac, "big"),
        property_exists($yac, "miss"));

/* an expired or deleted key is absent to all three */
$yac->set("t", 1, 1);
sleep(2);
var_dump(isset($yac->t), property_exists($yac, "t"), $yac->has("t"));
$yac->set("d", 1);
unset($yac->d);
var_dump(isset($yac->d), property_exists($yac, "d"), $yac->has("d"));

$yac->flush();
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
