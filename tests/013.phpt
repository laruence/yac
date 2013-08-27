--TEST--
Check for ttl bug 
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php 
$yac = new Yac(); 
$yac->set('test', 1, 1); 
sleep(2); 
var_dump($yac->get('test'));
?>
--EXPECTF--
bool(false)
