# Yac benchmarks

Multi-process benchmark comparing Yac against APCu and Memcached.
`mp_bench.php` simulates a realistic PHP-FPM deployment: the parent
process initializes and warms the backend (allocating its shared
memory), then forks N worker processes that all share the SAME cache
and hammer it concurrently:

- Yac / APCu — shared via `mmap(MAP_SHARED|MAP_ANON)` inherited across
  `fork()` (the same mechanism FPM workers use)
- Memcached — each worker opens its own TCP connection

Every worker runs an interleaved read/write loop for a fixed duration
at a 100:1 read:write ratio (read-heavy, typical of real caches), with
the cache warmed up first so reads are hits. Throughput is reported as
**aggregate ops/s across all workers over wall time** — real contention
behavior, not the sum of isolated single-process runs.

## Reproduction

```bash
make                                   # build modules/yac.so
bench/run_mp.sh --pcntl \
    --apcu=$(php -r 'echo ini_get("extension_dir");')/apcu.so \
    --memcached=$(php -r 'echo ini_get("extension_dir");')/memcached.so \
    --procs=16 --seconds=5 --ratio=100 --value-size=6
```

`run_mp.sh` starts php with `-n`, so **only** the extensions given on
the command line are loaded — no system-ini copies can sneak in.
`--pcntl` is required (the benchmark forks workers): use the bare flag
when pcntl is built into PHP, `--pcntl=/path/to/pcntl.so` when it is a
shared extension.

Which backends run follows from which extensions get loaded: the apcu
and memcached backends only run when their `.so` was passed (and the
memcached server is reachable at `--mc-host`/`--mc-port`).
`--value-size=N` makes every value exactly N bytes. The reference table
uses 4, 64, 1024, 16384, and 262144 bytes. Other tunables: `--procs`,
`--seconds`, `--ratio`, `--keys`.

Shared memory is fixed at 160M for Yac (`keys_memory_size=32M` +
`values_memory_size=128M`) and `apc.shm_size=160M` for APCu, so both
backends get the same budget.

## Workload details

All values in one run come from a single size class. They are
random-looking text built from a shared pool of random words, and
**compression is off everywhere**: `yac.compress_threshold=-1`,
`Memcached::OPT_COMPRESSION` off, and APCu never compresses — so every
backend stores values as-is and the comparison measures raw cache
mechanics.

## Environment

MacBook Pro (macOS 26.5, Apple M5 Pro, 15-core CPU), PHP 8.5,
APCu 5.1.28, php-memcached 3.4.0, Memcached server 1.6.45 on
127.0.0.1:11211. Yac built from the current `master`.
`--keys=20000` for 4 B through 1 KB, `6000` for 16 KB, and `200` for
256 KB, keeping each warmed key set within the 160M shared-memory budget.

Reference numbers (16 workers, 5s, 100:1 read:write, compression off,
160M shared memory):

| Value size | Yac          | APCu       | Memcached  | Yac / APCu | Yac / Memcached |
|-----------:|-------------:|-----------:|-----------:|-----------:|----------------:|
| 4 B        | **63.8M**    | 1.19M      | 0.108M     | 53.8x      | 592.6x          |
| 64 B       | **69.9M**    | 1.20M      | 0.107M     | 58.4x      | 650.6x          |
| 1 KB       | **44.7M**    | 1.29M      | 0.106M     | 34.7x      | 419.9x          |
| 16 KB      | **9.18M**    | 1.37M      | 0.093M     | 6.7x       | 98.5x           |
| 256 KB     | **0.879M**   | 0.625M     | 0.045M     | 1.4x       | 19.3x           |

## Disclaimer

These numbers were measured on one specific machine with one specific
workload and are provided for rough orientation only. They are not a
guarantee of performance: results vary with hardware, OS, PHP version
and extensions, memory sizing, and your application's actual read/write
ratio, key distribution and value sizes. Benchmark on your own hardware
and workload before making capacity or architecture decisions.
