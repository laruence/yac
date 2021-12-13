--TEST--
Check for inherit from Yac
--SKIPIF--
<?php
if (!extension_loaded("yac")) die("skip");
if (version_compare(PHP_VERSION, "8.1.0", ">=")) {
   die("skip, only PHP8.0 and below is supported");
}
?>
--INI--
yac.enable=0
--FILE--
<?php
class Sub extends Yac {};
?>
--EXPECTF--
Fatal error: Class Sub may not inherit from final class (Yac) in %s023.php on line %d
