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
Fatal error: Class Sub may not inherit from final class (Yac) in %s023.php on line %d
