--TEST--
Check for inherit from Yac
--SKIPIF--
<?php
if (!extension_loaded("yac")) die("skip");
if (version_compare(PHP_VERSION, "8.1.0", "<")) {
   die("skip, only PHP8.1 and above is supported");
}
?>
--INI--
yac.enable=0
--FILE--
<?php
class Sub extends Yac {};
?>
--EXPECTF--
Fatal error: Class Sub cannot extend final class Yac in %s024.php on line %d
