--TEST--
Yac::has() reports a live entry without reading it
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

/* stored values of every kind are "present", including the falsy ones get()
 * cannot tell from a miss */
$yac->set("i", 123);
$yac->set("n", null);
$yac->set("f", false);
$yac->set("e", "");
$yac->set("z", 0);
$yac->set("big", str_repeat("x", 100));   /* block value, not embedded */
var_dump($yac->has("i"));
var_dump($yac->has("n"));
var_dump($yac->has("f"));
var_dump($yac->has("e"));
var_dump($yac->has("z"));
var_dump($yac->has("big"));
var_dump($yac->has("miss"));

/* expiry and delete both make the key absent */
$yac->set("t", 1, 1);
var_dump($yac->has("t"));
sleep(2);
var_dump($yac->has("t"));

$yac->set("d", 1);
$yac->delete("d");
var_dump($yac->has("d"));

/* has() is a probe, not an access: it leaves the entry's hit count alone */
$yac->set("h", str_repeat("y", 64));
$hits = function ($yac, $key) {
	foreach ($yac->dump(1000) as $i) {
		if ($i["key"] === $key) {
			return $i["hits"];
		}
	}
	return -1;
};
var_dump($hits($yac, "h"));
for ($n = 0; $n < 100; $n++) { $yac->has("h"); }
var_dump($hits($yac, "h"));

/* prefix scopes the probe */
$p = new Yac("ns_");
$p->set("k", 1);
var_dump($p->has("k"));
var_dump($yac->has("k"));
var_dump($yac->has("ns_k"));

$yac->flush();
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
int(0)
int(0)
bool(true)
bool(false)
bool(true)
