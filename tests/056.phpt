--TEST--
Yac multi-get default: every miss slot gets its own refcount (no shared-copy corruption)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* regression: misses used to be filled with the caller's default zval
 * without addref, so several misses shared one refcount and corrupted
 * the heap when the result array was released */
class Probe {
    public $tag;
    public function __construct($tag) { $this->tag = $tag; }
}

$yac = new Yac();

$def = new Probe("shared-default");
$ret = $yac->get(["m1", "m2", "m3"], $def);
var_dump($ret["m1"]->tag, $ret["m2"]->tag, $ret["m3"]->tag);
var_dump($ret["m1"] === $ret["m2"] && $ret["m2"] === $ret["m3"]);

/* release order that used to corrupt the heap */
unset($ret);
unset($def);

/* string default survives too */
$s = str_repeat("y", 7);
$a = $yac->get(["n1", "n2"], $s);
$b = $yac->get(["n3", "n4"], $s);
var_dump($a["n1"], $b["n4"]);

echo "OK\n";
?>
--EXPECT--
string(14) "shared-default"
string(14) "shared-default"
string(14) "shared-default"
bool(true)
string(7) "yyyyyyy"
string(7) "yyyyyyy"
OK
