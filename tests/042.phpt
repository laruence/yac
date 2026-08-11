--TEST--
Yac multi-process contention: no torn reads, atomic add()
--CREDITS--
Jarvis (AI assistant to Laruence)
--DESCRIPTION--
Several forked workers hammer the same keys with set/get/delete; every
read must return either false or one of the written values (never a torn
or mixed value). Afterwards all workers race to add() the same key — the
lock-free CAS layer must let exactly one of them win.
--SKIPIF--
<?php if (!extension_loaded("yac") || !extension_loaded("pcntl")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$workers = 4;
$rounds = 500;
$keys = 50;

$pids = array();
for ($w = 0; $w < $workers; $w++) {
    $pid = pcntl_fork();
    if ($pid === 0) {
        $yac = new Yac();
        /* phase 1: concurrent set/get/delete on shared keys */
        for ($i = 0; $i < $rounds; $i++) {
            $key = "conflict_" . ($i % $keys);
            $value = "w{$w}_{$i}";
            if (!$yac->set($key, $value)) exit(2);
            /* a concurrent delete may legitimately make this read miss; a
               returned value must however always be one of the written ones */
            $got = $yac->get($key);
            if ($got !== false && (!is_string($got) || !preg_match('/^w\d+_\d+$/', $got))) exit(3);
            if ($i % 5 === $w % 5) {
                $yac->delete($key);
                $got = $yac->get($key);
                if ($got !== false && (!is_string($got) || !preg_match('/^w\d+_\d+$/', $got))) exit(4);
            }
        }
        /* phase 2: all workers race to add() the same key */
        exit($yac->add("race", $w) ? 1 : 0);
    }
    $pids[] = $pid;
}

$winners = 0;
$failures = 0;
$status = 0;
foreach ($pids as $pid) {
    pcntl_waitpid($pid, $status);
    if (!pcntl_wifexited($status)) {
        /* crashed (e.g. segfault) instead of exiting */
        $failures++;
        continue;
    }
    $code = pcntl_wexitstatus($status);
    if ($code === 1) {
        $winners++;
    } elseif ($code !== 0) {
        $failures++;
    }
}
var_dump($failures);
var_dump($winners === 1);
?>
--EXPECT--
int(0)
bool(true)
