<?php
/**
 * mp_bench.php — Multi-process benchmark: Yac vs APCu vs Memcached.
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
 * The value size class(es) come from --value-size (default 6 and 128 bytes).
 * With one size, every key holds an identically-sized value. Compression is
 * intentionally left off so every backend stores values as-is.
 *
 * Throughput is reported as AGGREGATE ops/s across all workers over wall time,
 * which measures real contention behavior — not the sum of isolated single-process
 * runs.
 *
 * Usage (prefer run_mp.sh, which loads the extensions and the CLI switches):
 *   ./run_mp.sh --pcntl --apcu=/path/to/apcu.so
 *   ./run_mp.sh --pcntl --procs=16 --seconds=5 --ratio=100 --keys=20000
 *   --value-size=256                    all values this many bytes (default 6,128)
 * Which backends run depends on the loaded extensions: apcu/memcached only
 * run when their .so was passed.
 */

error_reporting(E_ALL);

/* ------------------------- tunable parameters ------------------------- */
$PROCS   = 16;      // number of concurrent worker processes
$SECONDS = 5;       // how long each worker runs the mixed loop
$RATIO   = 100;     // reads per write (100 => ~1 write per 100 reads)
$KEYS    = 20000;   // shared key space; all workers hit the same keys (contention)
$MC_HOST = '127.0.0.1';
$MC_PORT = 11211;
$MIXED   = [6, 128];    // value size class(es) in bytes; one size = fixed

foreach (array_slice($argv, 1) as $a) {
    if (preg_match('/^--(procs|seconds|ratio|keys|host|port|value-size)=(.+)$/', $a, $m)) {
        switch ($m[1]) {
            case 'procs':   $PROCS   = max(1, (int)$m[2]); break;
            case 'seconds': $SECONDS = max(1, (int)$m[2]); break;
            case 'ratio':   $RATIO   = max(1, (int)$m[2]); break;
            case 'keys':    $KEYS    = (int)$m[2]; break;
            case 'host':    $MC_HOST = $m[2]; break;
            case 'port':    $MC_PORT = (int)$m[2]; break;
            case 'value-size': $MIXED = array_map('intval', explode(',', $m[2])); break;
        }
    }
}
if ($KEYS <= 0 || $PROCS <= 0) { fwrite(STDERR, "--keys/--procs must be > 0\n"); exit(2); }
if (count($MIXED) < 1) { fwrite(STDERR, "--value-size needs at least one size\n"); exit(2); }
if (!function_exists('pcntl_fork')) { fwrite(STDERR, "pcntl extension is required\n"); exit(2); }

/* -----------------------------------------------------------------------
 * Pre-generate data in the parent. Workers inherit these by fork (read-only,
 * copy-on-write), so every worker sees the identical keys / values / access
 * sequence and no RNG cost falls inside the timed loop.
 *
 * Values are assembled from a pool of random 8-byte words: random enough to
 * be realistic (no pathological repetition), still text-like. Compression is
 * off in this benchmark, so the content only needs to be plausible.
 * --------------------------------------------------------------------- */
mt_srand(20260806);

$pool = [];
for ($i = 0; $i < 256; $i++) {
    $w = '';
    for ($j = 0; $j < 8; $j++) { $w .= chr(mt_rand(97, 122)); }
    $pool[] = $w;
}

function make_value(array $pool, int $len): string {
    $words = max(1, intdiv($len + 7, 8));
    $parts = [];
    $n = count($pool);
    for ($j = 0; $j < $words; $j++) { $parts[] = $pool[mt_rand(0, $n - 1)]; }
    return substr(implode('', $parts), 0, $len);
}

$keys = [];
for ($i = 0; $i < $KEYS; $i++) { $keys[] = "bench_key_$i"; }
$nclasses = count($MIXED);
$values = [];
for ($i = 0; $i < $KEYS; $i++) { $values[] = make_value($pool, $MIXED[$i % $nclasses]); }

/* Pre-computed random key indices for the mixed loop. Fixed seed so all
 * backends use the exact same access pattern. */
$IDX_N = 262144;                 // pool of random indices, cycled in the loop
$idx = [];
for ($i = 0; $i < $IDX_N; $i++) { $idx[] = mt_rand(0, $KEYS - 1); }

printf(
    "Multi-process benchmark: procs=%d  duration=%ds  read:write=%d:1  keys=%d  value-size=%s bytes  compression=off\n",
    $PROCS, $SECONDS, $RATIO, $KEYS, implode('/', $MIXED)
);

/**
 * Run one backend. The parent initializes + warms the backend, forks $procs
 * workers, releases them with a start barrier, then aggregates their results.
 *
 * Returns an associative array of aggregate metrics, or null if the backend is
 * unavailable (extension missing / CLI disabled / server down).
 */
function run_backend(string $name, int $procs, int $seconds, int $ratio,
                     array $keys, array $values, array $idx,
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
        for ($i = 0; $i < $nkeys; $i++) { $y->set($keys[$i], $values[$i]); }   // warm-up

    } elseif ($name === 'apcu') {
        if (!extension_loaded('apcu') ||
            !filter_var(ini_get('apc.enable_cli'), FILTER_VALIDATE_BOOL)) { return null; }
        apcu_clear_cache();
        for ($i = 0; $i < $nkeys; $i++) { apcu_store($keys[$i], $values[$i]); } // warm-up

    } elseif ($name === 'memcached') {
        if (!extension_loaded('memcached')) { return null; }
        $m = new Memcached();
        $m->addServer($host, $port);
        $m->flush();                                                        // align with yac/apcu
        $m->set('__mp_probe__', 'x');
        if ($m->get('__mp_probe__') !== 'x') { return null; }
        $m->delete('__mp_probe__');
        for ($i = 0; $i < $nkeys; $i++) { $m->set($keys[$i], $values[$i]); }    // warm-up

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
             * (shared memory); memcached needs its own connection per process.
             * Client-side compression is disabled so all backends store values
             * as-is, matching Yac/APCu in this benchmark. */
            $m = null;
            if ($name === 'memcached') {
                $m = new Memcached();
                if (defined('Memcached::OPT_BINARY_PROTOCOL')) { $m->setOption(Memcached::OPT_BINARY_PROTOCOL, true); }
                if (defined('Memcached::OPT_NO_BLOCK'))        { $m->setOption(Memcached::OPT_NO_BLOCK, true); }
                if (defined('Memcached::OPT_COMPRESSION'))     { $m->setOption(Memcached::OPT_COMPRESSION, false); }
                $m->addServer($host, $port);
            }

            /* Mixed loop. The deadline is only checked every 1024 ops so the cost
             * of microtime() is amortized and does not skew fast backends. */
            while (true) {
                for ($b = 0; $b < 1024; $b++, $i++) {
                    $ki = $idx[$i % $idxN];
                    $k = $keys[$ki];
                    if ($i % $stride === 0) {
                        /* ---- write ---- */
                        if ($name === 'yac')             { $y->set($k, $values[$ki]); }
                        elseif ($name === 'apcu')        { apcu_store($k, $values[$ki]); }
                        else                             { $m->set($k, $values[$ki]); }
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
/* Which backends run follows from the loaded extensions: apcu and
 * memcached only run when their .so was passed on the command line.
 * run_backend() skips anything unavailable (extension missing /
 * apc.enable_cli off / server unreachable). */
$want = ['yac', 'apcu', 'memcached'];
$results = []; $skipped = [];
foreach ($want as $name) {
    echo "\n>>> $name\n";
    $r = run_backend($name, $PROCS, $SECONDS, $RATIO, $keys, $values, $idx, $MC_HOST, $MC_PORT);
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
    echo "\nYac advantage over the others (>1 means Yac is faster):\n";
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
