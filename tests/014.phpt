--TEST--
Check for ttl bug 
--INI--
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
