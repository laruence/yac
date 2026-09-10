--TEST--
Check that a failed json serialization stores nothing
--SKIPIF--
<?php
if (!extension_loaded("yac")) print "skip";
else if (!defined("YAC_SERIALIZER_JSON")) print "skip yac built without the json serializer";
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.serializer=json
--FILE--
<?php
var_dump(YAC_SERIALIZER === YAC_SERIALIZER_JSON);

$yac = new Yac("jser");
$yac->set("probe", "intact");

class Thrower implements JsonSerializable {
	public function jsonSerialize(): mixed { throw new Exception("jsonSerialize boom"); }
}

/* php_json_encode() reports most of these through its return value rather
 * than an exception, so checking EG(exception) alone would let a half
 * written buffer through. INF/NAN are weaker still: the encoder writes a 0,
 * sets an error code and reports success, so only the code tells them apart
 * from a legitimately stored 0 */
$cases = [
	"inf"       => ["d" => INF],
	"nan"       => ["d" => NAN],
	"resource"  => ["r" => fopen("/etc/hosts", "r")],
	"bad_utf8"  => ["s" => "head\xB1\x31tail"],
	"thrower"   => ["o" => new Thrower()],
];

foreach ($cases as $name => $value) {
	try {
		var_dump($yac->set($name, $value));
	} catch (Exception $e) {
		echo get_class($e), ": ", $e->getMessage(), "\n";
	}
	var_dump($yac->get($name));
}

/* a self referencing array exceeds the encoder's depth limit */
$deep = [];
$deep["self"] = &$deep;
try {
	var_dump($yac->set("recursion", ["x" => $deep]));
} catch (Exception $e) {
	echo get_class($e), ": ", $e->getMessage(), "\n";
}
var_dump($yac->get("recursion"));

var_dump($yac->get("probe"));
?>
--EXPECTF--
bool(true)

Warning: Yac::set(): Serialization failed in %s on line %d
bool(false)
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
bool(false)
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
bool(false)
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
bool(false)
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
Exception: jsonSerialize boom
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
bool(false)
bool(false)
string(6) "intact"
