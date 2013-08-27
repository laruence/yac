--TEST--
Check for yac info
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php

$yac = new Yac();

for ($i = 0; $i<1000; $i++) {
    $key =  "xxx" . rand(1, 1000);
    $value = str_repeat("a", rand(1, 100000));

    if (!$yac->set($key, $value)) {
        var_dump($key, $value);
        var_dump("write " . $i);
    }

    if ($value != ($new = $yac->get($key))) {
        var_dump($new);
        var_dump("read " . $i);
    }
}

$info = $yac->info();
var_dump($info['slots_used'] <= 1000);
var_dump($info['hits']);
var_dump($info['miss']);
var_dump($info['fails']);
var_dump($info['kicks']);
--EXPECTF--
bool(true)
int(1000)
int(0)
int(0)
int(0)
