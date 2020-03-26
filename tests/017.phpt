--TEST--
Check for mutiple process
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
<?php if (!extension_loaded("pcntl")) print "skip need pcntl"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
yac.compress_threshold=1024
--FILE--
<?php
$yac = new Yac();

if (($pid = pcntl_fork())) {
    while(!$yac->set("parent", "FIN"));
    while (!($msg = $yac->get("child")));
    echo "MSG From Child:", $msg,"\n";
    while(!($yac->set("parent", "BYE")));
    pcntl_wait($status);
	echo "Parent exiting\n";
} else {
    while(!($msg = $yac->get("parent")));
    echo "MSG From Parent:" , $msg, "\n";
    while(!($yac->set("child", "ACK")));
    while(($msg = $yac->get("parent")) != "BYE");
	echo "Child exiting\n";
}
?>
--EXPECT--
MSG From Parent:FIN
MSG From Child:ACK
Child exiting
Parent exiting
