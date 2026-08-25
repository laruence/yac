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
bench/run_mp.sh --procs=16 --seconds=5 --ratio=100
```

Tunable: `--procs`, `--seconds`, `--ratio`, `--keys`,
`--mixed=6,128` (value size classes in bytes),
`--backend=yac|apcu|memcached|all`, `--host`, `--port`.

Unavailable backends (extension not loaded, Memcached not running) are
skipped automatically. `run_mp.sh` loads the locally built `yac.so` and
enlarges key/value memory so the warmed key set fits without eviction;
it also enables APCu in CLI and sizes its shared memory accordingly.

## Workload details

The key space is split evenly across two value classes: **6-byte**
values (small enough for Yac 2.4.0 to embed directly in the slot) and
**128-byte** values. All values are random-looking text built from a
shared pool of random words, and **compression is disabled**
(`yac.compress_threshold` unset, Memcached's client-side
`OPT_COMPRESSION` off) so every backend stores values as-is and the
comparison measures raw cache mechanics.

## Environment

MacBook Pro (macOS 26.5, Apple M5 Pro, 15-core CPU), PHP 8.5,
APCu 5.1.28, php-memcached 3.4.0, Memcached on 127.0.0.1:11211.
`yac.keys_memory_size=32M`, `yac.values_memory_size=128M`, 20,000
shared keys. Results are stable across repeated runs.

Reference numbers (16 workers, 5s, 100:1, mixed 6/128-byte values):

| Backend   | Total ops/s    | Yac advantage |
|-----------|----------------|---------------|
| **Yac**   | **26,873,538** | —             |
| APCu      | 1,185,523      | 22.7x         |
| Memcached | 97,644         | 275.2x        |

## Disclaimer

These numbers were measured on one specific machine with one specific
workload and are provided for rough orientation only. They are not a
guarantee of performance: results vary with hardware, OS, PHP version
and extensions, memory sizing, and your application's actual read/write
ratio, key distribution and value sizes. Benchmark on your own hardware
and workload before making capacity or architecture decisions.
