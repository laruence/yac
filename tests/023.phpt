--TEST--
Check for inherit from Yac
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=0
--FILE--
<?php
class Sub extends Yac {};
?>
--EXPECTF--
Fatal error: Class Sub %s final class %s
