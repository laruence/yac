--TEST--
Yac::add batch failure with numeric keys and argument edge cases
--DESCRIPTION--
Multi-add where a key already exists releases the stringified numeric key
on the failure path; set() with an array + non-int second arg warns;
set()/get()/dump() argument-count edge cases.
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

/* numeric (list) keys in batch add: first succeeds, second run must fail
   and release the stringified keys */
var_dump($yac->add(array("first", "second")));
var_dump($yac->add(array("first", "second")));
var_dump($yac->get("0"));
var_dump($yac->get("1"));

/* set(array, non-int) -> ttl must be integer */
var_dump($yac->set(array("a" => 1), "not-an-int"));

/* single-arg set with a non-array fails argument parsing
   (PHP 7: warning, PHP 8: TypeError) */
try {
    @$yac->set(12345);
    echo "argcheck failed\n";
} catch (TypeError $e) {
    echo "argcheck failed\n";
}

/* too many arguments */
try {
    @$yac->set("k", "v", 0, "extra");
    echo "wrong count\n";
} catch (TypeError $e) {
    echo "wrong count\n";
}

/* get with 3 args -> wrong count */
try {
    $x = null;
    @$yac->get("k", $c, $x);
    echo "wrong count\n";
} catch (TypeError $e) {
    echo "wrong count\n";
}

/* dump limit string is cast in coercive mode */
var_dump(count($yac->dump("1")) <= 1);
?>
--EXPECTF--
bool(true)
bool(false)
string(5) "first"
string(6) "second"

Warning: Yac::set(): ttl parameter must be an integer in %s051.php on line %d
NULL
argcheck failed
wrong count
wrong count
bool(true)
