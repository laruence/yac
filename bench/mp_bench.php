<?php
/**
 * mp_bench.php — Multi-process mixed read/write benchmark: Yac vs APCu vs Memcached.
 *
 * Simulates a realistic PHP-FPM deployment. The parent process initializes the
 * backend (allocating its shared memory), then forks N worker processes that all
 * share the SAME cache and hammer it concurrently:
 *
 *   - Yac / APCu : shared via mmap(MAP_SHARED | MAP_ANON), inherited across fork.
 *   - Memcached  : each worker opens its own TCP connection to the server.
 *
 * Every worker runs an interleaved read/write loop for a fixed duration, with a
 * read:write ratio of RATIO:1 (default 100:1, i.e. ~1 write per 100 reads) — a
 * read-heavy workload typical of real caches. The cache is warmed up first so
 * reads are hits.
 *
 * Throughput is reported as AGGREGATE ops/s across all workers over wall time,
 * which measures real contention behavior — not the sum of isolated single-process
 * runs.
 *
 * Usage (prefer run_mp.sh, which loads yac.so and the CLI switches):
 *   ./run_mp.sh
 *   ./run_mp.sh --procs=16 --seconds=5 --ratio=100 --keys=20000 --vallen=64
 *   --backend=yac|apcu|memcached|all     run a single backend instead of all
 */

error_reporting(E_ALL);

/* ------------------------- tunable parameters ------------------------- */
$PROCS   = 16;      // number of concurrent worker processes
$SECONDS = 5;       // how long each worker runs the mixed loop
$RATIO   = 100;     // reads per write (100 => ~1 write per 100 reads)
$KEYS    = 20000;   // shared key space; all workers hit the same keys (contention)
$VALLEN  = 64;      // value size in bytes
$BACKEND = 'all';
$MC_HOST = '127.0.0.1';
$MC_PORT = 11211;

foreach (array_slice($argv, 1) as $a) {
    if (preg_match('/^--(procs|seconds|ratio|keys|vallen|backend|host|port)=(.+)$/', $a, $m)) {
        switch ($m[1]) {
            case 'procs':   $PROCS   = max(1, (int)$m[2]); break;
            case 'seconds': $SECONDS = max(1, (int)$m[2]); break;
            case 'ratio':   $RATIO   = max(1, (int)$m[2]); break;
            case 'keys':    $KEYS    = (int)$m[2]; break;
            case 'vallen':  $VALLEN  = (int)$m[2]; break;
            case 'backend': $BACKEND = $m[2]; break;
            case 'host':    $MC_HOST = $m[2]; break;
            case 'port':    $MC_PORT = (int)$m[2]; break;
        }
    }
}
if ($KEYS <= 0 || $PROCS <= 0) { fwrite(STDERR, "--keys/--procs must be > 0\n"); exit(2); }
if (!function_exists('pcntl_fork')) { fwrite(STDERR, "pcntl extension is required\n"); exit(2); }

/* -----------------------------------------------------------------------
 * Pre-generate data in the parent. Workers inherit these by fork (read-only,
 * copy-on-write), so every worker sees the identical keys / value / access
 * sequence and no RNG cost falls inside the timed loop.
 * --------------------------------------------------------------------- */
$keys = [];
for ($i = 0; $i < $KEYS; $i++) { $keys[] = "bench_key_$i"; }
$value = str_pad('payload-' . str_repeat('x', 8), $VALLEN, 'v');

/* Pre-computed random key indices for the mixed loop. Fixed seed so all
 * backends use the exact same access pattern. */
mt_srand(20260806);
$IDX_N = 262144;                 // pool of random indices, cycled in the loop
$idx = [];
for ($i = 0; $i < $IDX_N; $i++) { $idx[] = mt_rand(0, $KEYS - 1); }

printf(
    "Multi-process mixed benchmark: procs=%d  duration=%ds  read:write=%d:1  keys=%d  vallen=%d\n",
    $PROCS, $SECONDS, $RATIO, $KEYS, $VALLEN
);

/**
 * Run one backend. The parent initializes + warms the backend, forks $procs
 * workers, releases them with a start barrier, then aggregates their results.
 *
 * Returns an associative array of aggregate metrics, or null if the backend is
 * unavailable (extension missing / CLI disabled / server down).
 */
function run_backend(string $name, int $procs, int $seconds, int $ratio,
                     array $keys, $value, array $idx,
                     string $host, int $port): ?array {
    $nkeys = count($keys);

    /* --- initialize + warm the cache in the parent (before forking) --- */
    $y = null;
    if ($name === 'yac') {
        if (!extension_loaded('yac')) { return null; }
        $y = new Yac();
        $y->flush();
        $info = $y->info();
        if ($info['slots_size'] < $nkeys) {
            fwrite(STDERR, "  [warn] Yac slots_size={$info['slots_size']} < keys=$nkeys; eviction will occur\n");
        }
        for ($i = 0; $i < $nkeys; $i++) { $y->set($keys[$i], $value); }   // warm-up

    } elseif ($name === 'apcu') {
        if (!extension_loaded('apcu') ||
            !filter_var(ini_get('apc.enable_cli'), FILTER_VALIDATE_BOOL)) { return null; }
        apcu_clear_cache();
        for ($i = 0; $i < $nkeys; $i++) { apcu_store($keys[$i], $value); } // warm-up

    } elseif ($name === 'memcached') {
        if (!extension_loaded('memcached')) { return null; }
        $m = new Memcached();
        $m->addServer($host, $port);
        $m->flush();                                                        // align with yac/apcu
        $m->set('__mp_probe__', 'x');
        if ($m->get('__mp_probe__') !== 'x') { return null; }
        $m->delete('__mp_probe__');
        for ($i = 0; $i < $nkeys; $i++) { $m->set($keys[$i], $value); }    // warm-up

    } else {
        return null;
    }

    $tmp = sys_get_temp_dir() . '/mpb_' . getmypid() . '_' . $name;
    @mkdir($tmp);

    /* --- fork the workers --- */
    $pids = [];
    for ($w = 0; $w < $procs; $w++) {
        $pid = pcntl_fork();
        if ($pid === -1) { fwrite(STDERR, "fork failed\n"); exit(2); }

        if ($pid === 0) {
            /* =================== worker process =================== */
            /* Wait for the parent's start signal so all workers begin together. */
            while (!file_exists("$tmp/go")) { usleep(50); }

            $start    = microtime(true);
            $deadline = $start + $seconds;
            $reads = 0; $writes = 0; $hits = 0;
            $i = 0;                          // running op counter
            $idxN   = count($idx);
            $stride = $ratio + 1;            // one write every (ratio+1) ops

            /* Open-addressing backends reuse the handle inherited from the parent
             * (shared memory); memcached needs its own connection per process. */
            $m = null;
            if ($name === 'memcached') {
                $m = new Memcached();
                if (defined('Memcached::OPT_BINARY_PROTOCOL')) { $m->setOption(Memcached::OPT_BINARY_PROTOCOL, true); }
                if (defined('Memcached::OPT_NO_BLOCK'))        { $m->setOption(Memcached::OPT_NO_BLOCK, true); }
                $m->addServer($host, $port);
            }

            /* Mixed loop. The deadline is only checked every 1024 ops so the cost
             * of microtime() is amortized and does not skew fast backends. */
            while (true) {
                for ($b = 0; $b < 1024; $b++, $i++) {
                    $k = $keys[$idx[$i % $idxN]];
                    if ($i % $stride === 0) {
                        /* ---- write ---- */
                        if ($name === 'yac')             { $y->set($k, $value); }
                        elseif ($name === 'apcu')        { apcu_store($k, $value); }
                        else                             { $m->set($k, $value); }
                        $writes++;
                    } else {
                        /* ---- read ---- */
                        $v = ($name === 'yac')   ? $y->get($k)
                         : (($name === 'apcu')  ? apcu_fetch($k)
                                                 : $m->get($k));
                        if ($v !== false) { $hits++; }
                        $reads++;
                    }
                }
                if (microtime(true) >= $deadline) { break; }
            }

            $elapsed = microtime(true) - $start;
            file_put_contents("$tmp/res_$w", sprintf('%.6f %d %d %d', $elapsed, $reads, $writes, $hits));
            exit(0);
            /* ====================================================== */
        }
        $pids[] = $pid;
    }

    /* ---------------- parent: release workers, then aggregate ---------------- */
    $t0 = microtime(true);
    file_put_contents("$tmp/go", '1');
    foreach ($pids as $p) { pcntl_waitpid($p, $st); }
    $wall = microtime(true) - $t0;

    /* Collect per-worker results. */
    $totReads = 0; $totWrites = 0; $totHits = 0;
    for ($w = 0; $w < $procs; $w++) {
        [$el, $r, $wr, $h] = array_map('floatval', explode(' ', file_get_contents("$tmp/res_$w")));
        $totReads += $r; $totWrites += $wr; $totHits += $h;
    }
    foreach (glob("$tmp/*") as $f) { @unlink($f); }
    @rmdir($tmp);

    $total = $totReads + $totWrites;
    return [
        'wall'      => $wall,
        'total_ops' => $total,
        'ops'       => $total / $wall,
        'reads_s'   => $totReads / $wall,
        'writes_s'  => $totWrites / $wall,
        'hit_rate'  => $totReads > 0 ? $totHits / $totReads : 0.0,
    ];
}

/* --------------------------- run the backends --------------------------- */
$want = $BACKEND === 'all' ? ['yac', 'apcu', 'memcached'] : [$BACKEND];
$results = []; $skipped = [];
foreach ($want as $name) {
    echo "\n>>> $name\n";
    $r = run_backend($name, $PROCS, $SECONDS, $RATIO, $keys, $value, $idx, $MC_HOST, $MC_PORT);
    if ($r === null) {
        $skipped[$name] = 'extension not loaded / apc.enable_cli off / server unreachable';
        echo "  skipped\n";
        continue;
    }
    $results[$name] = $r;
    printf("  wall=%.2fs  total=%s ops  aggregate=%.2fM ops/s  hit=%.1f%%\n",
        $r['wall'], number_format($r['total_ops']), $r['ops'] / 1e6, $r['hit_rate'] * 100);
}

/* ------------------------------ summary -------------------------------- */
echo "\n==================== Aggregate throughput (all workers) ====================\n";
printf("%-10s %14s %14s %14s %9s\n", 'Backend', 'Total ops/s', 'Reads/s', 'Writes/s', 'Hit%');
foreach ($results as $name => $r) {
    printf("%-10s %14s %14s %14s %8.1f%%\n",
        $name,
        number_format($r['ops'], 0),
        number_format($r['reads_s'], 0),
        number_format($r['writes_s'], 0),
        $r['hit_rate'] * 100);
}

if (isset($results['yac']) && count($results) > 1) {
    echo "\nRelative to Yac (>1 means faster than Yac):\n";
    foreach ($results as $name => $r) {
        if ($name === 'yac') { continue; }
        printf("  %-10s total %.2fx   reads %.2fx   writes %.2fx\n", $name,
            $results['yac']['ops']      / max($r['ops'], 1e-9),
            $results['yac']['reads_s']  / max($r['reads_s'], 1e-9),
            $results['yac']['writes_s'] / max($r['writes_s'], 1e-9));
    }
}

if ($skipped) {
    echo "\nSkipped backends:\n";
    foreach ($skipped as $name => $why) { echo "  - $name: $why\n"; }
}
echo "\n";
