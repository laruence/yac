--TEST--
Yac::add() batch mode — array arguments
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

/* 1. batch add when none exist — all succeed */
$batch1 = ["a" => "va", "b" => "vb", "c" => "vc"];
var_dump($yac->add($batch1));
var_dump($yac->get("a"));
var_dump($yac->get("b"));
var_dump($yac->get("c"));

/* 2. batch add when some keys exist — should fail entirely */
$batch2 = ["c" => "vc_new", "d" => "vd"];
var_dump($yac->add($batch2)); /* false — "c" already exists */
var_dump($yac->get("c"));     /* still "vc" */
var_dump($yac->get("d"));     /* NULL — NOT added */

/* 3. batch add with TTL */
$batch3 = ["e" => "ve", "f" => "vf"];
var_dump($yac->add($batch3, 60));
var_dump($yac->get("e"));
var_dump($yac->get("f"));

/* 4. batch add empty array — should succeed (nothing to add) */
var_dump($yac->add([]));

/* 5. batch add after delete — succeeds */
$yac->delete("c");
$batch4 = ["c" => "restored"];
var_dump($yac->add($batch4));
var_dump($yac->get("c"));

/* 6. batch add is not atomic: on failure, new keys are still written */
$batch5 = ["g" => "vg", "a" => "va_new"]; /* "a" exists since section 1 */
var_dump($yac->add($batch5)); /* false — "a" already exists */
var_dump($yac->get("g"));     /* "vg" — no rollback */
var_dump($yac->get("a"));     /* "va" — untouched */
?>
--EXPECTF--
bool(true)
string(2) "va"
string(2) "vb"
string(2) "vc"
bool(false)
string(2) "vc"
NULL
bool(true)
string(2) "ve"
string(2) "vf"
bool(true)
bool(true)
string(8) "restored"
bool(false)
string(2) "vg"
string(2) "va"
