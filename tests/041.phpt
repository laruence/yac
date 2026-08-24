--TEST--
Yac constructor throws in CLI when yac.enable_cli=0
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=0
--FILE--
<?php
try {
    $yac = new Yac();
    echo "constructed\n";
} catch (Exception $e) {
    var_dump(get_class($e), $e->getMessage());
}
?>
--EXPECTF--
string(9) "Exception"
string(18) "Yac is not enabled"
