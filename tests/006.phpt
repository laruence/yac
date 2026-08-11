--TEST--
Check for yac multi set/get
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
    $key =  "xxx" . rand(1, 100000);
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

var_dump($i);

/* with default settings, strings over 1MB are transparently compressed */
$big = str_repeat("abc123", 262144); /* 1.5MB */
var_dump($yac->set("big", $big));
var_dump($yac->get("big") === $big);
--EXPECTF--
int(1000)
bool(true)
bool(true)
