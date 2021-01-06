--TEST--
Check for yac::__construct with yac.enable=0
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=0
--FILE--
<?php 
try {
	$yac = new Yac("prefix");
} catch (Exception $e) {
	var_dump($e->getMessage());
}

@var_dump($yac);
?>
--EXPECTF--
string(18) "Yac is not enabled"
NULL
