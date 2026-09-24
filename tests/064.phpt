--TEST--
isset()/empty()/property_exists() on Yac read a stored entry without touching its LRU state
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
$yac->set("s0", "0");
$yac->set("big", str_repeat("x", 100));   /* block value path */

/* isset(): stored and not null. A stored null reads like a miss here, which
 * is the standard isset() contract */
var_dump(isset($yac->i), isset($yac->s), isset($yac->n), isset($yac->f),
        isset($yac->e), isset($yac->z), isset($yac->big), isset($yac->miss));

/* empty(): stored and falsy, or absent. Every falsy value is embedded or
 * inline, so peek() settles them with no value read */
var_dump(empty($yac->i), empty($yac->n), empty($yac->f), empty($yac->e),
        empty($yac->z), empty($yac->s0), empty($yac->big), empty($yac->miss));

/* property_exists(): stored at all, a null included */
var_dump(property_exists($yac, "i"), property_exists($yac, "n"),
        property_exists($yac, "f"), property_exists($yac, "big"),
        property_exists($yac, "miss"));

/* a falsy double is only ever 0.0/-0.0, both float-representable, so both
 * embed in the val word no matter the key length -- peek() settles them with
 * no block read. a long key still stores a non-falsy precision double in a
 * block, but that is non-empty, so peek() answers without reading it either */
$yac->set("d0", 0.0);
$yac->set("d1", 1.5);
$longkey = str_repeat("K", 45);   /* > 40: 0.0 still embeds, but 3.14 would go to a block */
$yac->set($longkey, 0.0);
var_dump(empty($yac->d0), empty($yac->d1), isset($yac->d0), $yac->has("d0"));
var_dump(empty($yac->$longkey), isset($yac->$longkey), $yac->has($longkey));

/* none of the four may count as an access: hits and atime stay put */
$hits = function ($yac, $key) {
	foreach ($yac->dump(1000) as $i) {
		if ($i["key"] === $key) {
			return $i["hits"];
		}
	}
	return -1;
};
$yac->set("h", str_repeat("y", 64));   /* block, so it carries a hit counter */
$before = $hits($yac, "h");
for ($n = 0; $n < 100; $n++) {
	$yac->has("h");
	isset($yac->h);
	empty($yac->h);
	property_exists($yac, "h");
}
var_dump($before, $hits($yac, "h"));

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
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
int(0)
int(0)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
