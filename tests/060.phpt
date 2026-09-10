--TEST--
Check that a failed serialization stores nothing
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.serializer=php
--FILE--
<?php
$yac = new Yac("ser");
$yac->set("probe", "intact");

class ThrowSleep {
	public $v = 1;
	public function __sleep(): array { throw new Exception("sleep boom"); }
}

/* the serializer leaves the buffer empty for the first three and half
 * written for the nested one, so a pack that reports success would either
 * crash on buf->s or store a payload that can never be read back */
$cases = [
	"closure"  => function () {},
	"internal" => new SplFileObject("/etc/hosts"),
	"sleep"    => new ThrowSleep(),
	"nested"   => ["head" => str_repeat("A", 32), "bad" => function () {}],
];

foreach ($cases as $name => $value) {
	try {
		var_dump($yac->set($name, $value));
	} catch (Exception $e) {
		echo get_class($e), ": ", $e->getMessage(), "\n";
	}
	var_dump($yac->get($name));
}

var_dump($yac->get("probe"));
?>
--EXPECTF--
Warning: Yac::set(): Serialization failed in %s on line %d
Exception: Serialization of 'Closure' is not allowed
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
Exception: Serialization of 'SplFileObject' is not allowed
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
Exception: sleep boom
bool(false)

Warning: Yac::set(): Serialization failed in %s on line %d
Exception: Serialization of 'Closure' is not allowed
bool(false)
string(6) "intact"
