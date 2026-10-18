--TEST--
Yac fast serializer round-trips arrays and stdClass, with PHP fallback
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

$array = ["nul\0key" => "nul\0value", "nested" => [1, 2, 3.5]];
var_dump($yac->set("array", $array));
var_dump($yac->get("array") === $array);

$scalars = [null, true, false, 1, -2, 3.5];
var_dump($yac->set("scalars", $scalars));
var_dump($yac->get("scalars") === $scalars);

$object = (object) ["id" => 1, "nested" => (object) ["name" => "yac"], "items" => [1, 2]];
var_dump($yac->set("object", $object));
$out = $yac->get("object");
var_dump($out instanceof stdClass);
var_dump($out == $object);

$custom = new DateTimeImmutable("2020-01-01T00:00:00Z");
var_dump($yac->set("custom", $custom));
var_dump($yac->get("custom") == $custom);

$ref = 42;
$with_ref = [&$ref];
var_dump($yac->set("reference", $with_ref));
var_dump($yac->get("reference") == $with_ref);

/* refs no longer disqualify the fast serializer: dump()'s enc field
 * says which encoder produced the payload */
function dump_find_069($yac, $key) {
	foreach ($yac->dump(100000) as $item) {
		if ($item["key"] === $key) {
			return $item;
		}
	}
	return NULL;
}
$ref2 = 1;
$with_ref2 = [&$ref2, 2, 3, 4];
var_dump($yac->set("withref", $with_ref2));
var_dump(dump_find_069($yac, "withref")["enc"] === YAC_ENC_YAC);
var_dump($yac->get("withref") == [1, 2, 3, 4]);

/* the custom object fell back to PHP's serialize() */
var_dump(dump_find_069($yac, "custom")["enc"] === YAC_ENC_PHP);

/* an in-slot scalar was never serialized at all */
var_dump($yac->set("plain", 42));
var_dump(dump_find_069($yac, "plain")["enc"] === YAC_ENC_NONE);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
