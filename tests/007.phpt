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

/* start_time is set at MINIT: a sane timestamp in the past */
var_dump($info['start_time'] > 1600000000 && $info['start_time'] <= time());

/* memory fields reflect the INI settings */
var_dump($info['slots_memory_size']);
var_dump($info['values_memory_size']);
var_dump($info['memory_size'] === $info['slots_memory_size'] + $info['values_memory_size']);

/* a get on a missing key counts as a miss */
$yac->get("no_such_key");
var_dump($yac->info()['miss']);

/* once all slots are occupied, further inserts kick out older entries */
for ($i = 0; $i < $info['slots_size'] + 100; $i++) {
    $yac->set("kick_" . $i, $i);
}
var_dump($yac->info()['kicks'] > 0);
--EXPECTF--
bool(true)
int(1000)
int(0)
int(0)
int(0)
bool(true)
int(4194304)
int(33554432)
bool(true)
int(1)
bool(true)
