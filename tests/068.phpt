--TEST--
YAC_DEBUG concurrent readers only observe complete values or misses
--SKIPIF--
<?php
if (!extension_loaded("yac")) print "skip";
if (!defined("YAC_DEBUG")) print "skip Yac was not built with debug instrumentation";
if (!extension_loaded("pcntl")) print "skip pcntl is required";
?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=1M
yac.values_memory_size=4M
yac.compress_threshold=-1
--FILE--
<?php
if (ini_get("yac.debug") !== false || ini_get("yac.test_pause_after_reserve_us") !== false) {
    exit(1);
}

function yac_debug_value($key, $seq) {
    $head = "YAC_DEBUG|$key|$seq|";
    return $head . str_repeat($head, 1024);
}

function yac_debug_valid($key, $value) {
    if (!is_string($value) || !preg_match('/^YAC_DEBUG\\|([^|]+)\\|(\\d+)\\|/', $value, $m)) {
        return false;
    }
    return $m[1] === $key && $value === yac_debug_value($key, (int)$m[2]);
}

$yac = new Yac();
$pids = array();
for ($worker = 0; $worker < 4; $worker++) {
    $pid = pcntl_fork();
    if ($pid === 0) {
        $yac = new Yac();
        for ($i = 0; $i < 200; $i++) {
            $key = "debug_" . ($i & 7);
            if ($worker < 2) {
                $yac->set($key, yac_debug_value($key, $worker * 1000 + $i));
            } else {
                $value = $yac->get($key);
                if ($value !== false && !yac_debug_valid($key, $value)) {
                    exit(1);
                }
            }
        }
        exit(0);
    }
    if ($pid < 0) {
        exit(1);
    }
    $pids[] = $pid;
}

foreach ($pids as $pid) {
    pcntl_waitpid($pid, $status);
    if (!pcntl_wifexited($status) || pcntl_wexitstatus($status) !== 0) {
        exit(1);
    }
}
echo "ok\n";
?>
--EXPECT--
ok
