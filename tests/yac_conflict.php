<?php
/* 
 * we used the following set:
 * yac.keys_memory_size => 128M => 128M
 * yac.values_memory_size => 1G => 1G
 * and put this under document root, run with ab -c 100
 */

$loop_count = 100;
$yac = new Yac("yac_test");

for ($i=0; $i<$loop_count; $i++) {
	$n = mt_rand(1, 100000000);
	$key = "key-" . $n;
	$value = array_fill(0, 100, $n);
	if (!($yac->set($key, $value))) {
		error_log("Set error: ". "key=". $key);
	}
	$testvalue = $yac->get($key);
	if ($testvalue === false) {
		/* this is not error */
		error_log("yac warning: ". "key=". $key. " Not_found\n");
	} else if ($testvalue !== $value) {
		error_log("yac error: ". "key=". $key. " Changed. <" . var_export($testvalue, 1) . "> should be <". var_export($value, 1) .">\n");
	}
}
?>
