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
`--value-size=N` makes every value exactly N bytes; the table below
comes from three runs with 6, 256 and 2048. Other tunables: `--procs`,
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
`--keys=20000` shared keys. Results are stable across repeated runs.

Reference numbers (16 workers, 5s, 100:1 read:write, compression off,
160M shared memory):

| Value size | Yac          | APCu       | Memcached  | Yac / APCu | Yac / Memcached |
|-----------:|-------------:|-----------:|-----------:|-----------:|----------------:|
| 6 B        | **77.1M**    | 1.10M      | 0.11M      | 69.8x      | 726.7x          |
| 256 B      | **60.5M**    | 1.12M      | 0.11M      | 54.0x      | 574.3x          |
| 2048 B     | **17.2M**    | 1.28M      | 0.10M      | 13.4x      | 169.6x          |

## Disclaimer

These numbers were measured on one specific machine with one specific
workload and are provided for rough orientation only. They are not a
guarantee of performance: results vary with hardware, OS, PHP version
and extensions, memory sizing, and your application's actual read/write
ratio, key distribution and value sizes. Benchmark on your own hardware
and workload before making capacity or architecture decisions.
