--TEST--
Check for ttl bug 
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
<?php if (YAC_SERIALIZER != "PHP") print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
error_reporting(E_ALL);
$yac = new Yac();

for($i = 0; $i<100; $i++) {
        $value[] = ($i + 100000)."sdfsfsfsfs";
}

$yac->set('test', $value);

echo count($yac->get('test'));
?>
--EXPECTF--
100
