--TEST--
Yac flush() is global across all instances
--CREDITS--
Jarvis (AI assistant to Laruence)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* Create multiple instances with different prefixes */
$yac_a = new Yac("a_");
$yac_b = new Yac("b_");
$yac_raw = new Yac();

/* Set values in each */
$yac_a->set("k", "from_a");
$yac_b->set("k", "from_b");
$yac_raw->set("raw_k", "from_raw");

/* Verify all readable before flush */
var_dump($yac_a->get("k"));
var_dump($yac_b->get("k"));
var_dump($yac_raw->get("raw_k"));

/* Flush from one instance */
$yac_a->flush();

/* All instances see empty cache — flush is truly global */
var_dump($yac_a->get("k"));       /* false */
var_dump($yac_b->get("k"));       /* false */
var_dump($yac_raw->get("raw_k")); /* false */

/* After flush, new writes work normally */
$yac_a->set("k", "after_flush");
var_dump($yac_a->get("k"));
?>
--EXPECTF--
string(6) "from_a"
string(6) "from_b"
string(8) "from_raw"
bool(false)
bool(false)
bool(false)
string(11) "after_flush"
