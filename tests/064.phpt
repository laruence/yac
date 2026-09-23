--TEST--
Yac::incr() refuses block-stored longs beyond the embedded range (64-bit)
--SKIPIF--
<?php
if (!extension_loaded("yac")) print "skip";
if (PHP_INT_SIZE < 8) print "skip 64-bit only";
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();
$yac->flush();

/* a plain long that does not fit the embedded range is stored as a block;
 * incr refuses it */
$max = (1 << 60) - 1;
var_dump($yac->set("big", $max));
var_dump($yac->incr("big", 1));   /* would reach 2^60: overflow, false */
var_dump($yac->get("big"));       /* $max, unchanged */

var_dump($yac->set("big2", $max + 1)); /* 2^60: not embedable, stored as block */
var_dump($yac->incr("big2"));      /* block long: false */

$yac->flush();
?>
--EXPECTF--
bool(true)
bool(false)
int(1152921504606846975)
bool(true)
bool(false)
