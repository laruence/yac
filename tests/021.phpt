--TEST--
Check for yac read/write/unset property
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php 
$yac = new Yac("prefix");

$yac->value = "value";

/* can not used in writen context */
try {
$yac->foo->bar = "bar";
} catch (Exception $e) {};

var_dump($yac->get("value"));
var_dump($yac->value);

var_dump($yac->get("foo"));
var_dump($yac->foo);

unset($yac->value);
var_dump($yac->get("value"));
var_dump($yac->value);

/* the write handler must not take a ref of its own: the engine does not
 * consume one, so the value would outlive the caller's unset */
class Probe {
	public function __destruct() { echo "DTOR\n"; }
}
$obj = new Probe();
$yac->obj = $obj;
unset($obj);
echo "AFTER-UNSET\n";
?>
--EXPECT--
string(5) "value"
string(5) "value"
bool(false)
NULL
bool(false)
NULL
DTOR
AFTER-UNSET
