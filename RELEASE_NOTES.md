Yac 2.4.1 was released on 2026-09-02; these notes mirror the GitHub release.

## What's New in 2.4.1

### Performance

- Every stored value is now integrity-checked with CRC-32C on read: values over 1KB are hashed over three interleaved hardware CRC chains on x86-64 and ARM64, everything else falls back to a sliced-by-8 software table
- Eviction rework: expired entries are swept in a separate pass, victims are picked by remaining ttl with ties falling to the least hit candidate, and blocks found corrupt are tombstoned in find() — less wasted eviction work under memory pressure
- A single MurmurHash64A now drives both the home slot and the probe stride (two hashes before)
- hits/miss are accumulated per process and folded into shared memory once at request end, taking shared-counter contention off the hot path
- Default memory sizes raised: keys 4M → 8M, values 32M → 64M; compression now on by default at 4K (was off)

### Fixes

- Fixed a refcount leak on the multi-get `$default` when filling missing keys
- Entries that fail to decompress are now invalidated instead of being served or evicted again
- Fixed crc sampling and made the decompression check more precise
- Fixed spurious kicks
- Fixed the JSON serializer decoding from an unterminated buffer, which could fail or read past the buffer
- Fixed `info()` miss counts being folded into the hits counter

## What's New in 2.4.0

Yac 2.4.0 was released on 2026-08-26.

### Performance

Measured on a 16-worker shared-cache benchmark (3-run averages), compared to 2.3.1:

| Workload | Metric | 2.3.1 | 2.4.0 | Improvement |
|----------|--------|-------|-------|-------------|
| Small values (embedded) | get | 3.9M ops/s | 6.3M ops/s | **+64%** |
| Small values (embedded) | set | 6.4M ops/s | 7.2M ops/s | +12% |
| Mixed (60% embedded, 40% serialized) | get | 2.3M ops/s | 4.0M ops/s | **+70%** |
| Mixed | set | 4.2M ops/s | 5.8M ops/s | +39% |
| Compressed values (LZ4 vs FastLZ) | get | 1.0M ops/s | 3.6M ops/s | **~3.5x** |

Key drivers:
- **Embedded values**: small scalars are stored directly in the hash slot, skipping value-memory allocation and block copy entirely — the biggest win for read-heavy small-value workloads
- Field-by-field slot commits and spinlock backoff (pause/yield) reduce contention overhead in multi-process environments
- LZ4 replaces FastLZ for compression: ~3.5x faster decompression and better ratio

Also, in an end-to-end comparison (16 workers, 100:1 read:write, mixed value sizes), Yac sustains **~27M ops/s** vs APCu's 1.2M and Memcached's 98K.

### API changes
- `get()` accepts an optional `$default` argument (returned on miss instead of `NULL`); multi-get omits missing keys (filled with `$default` when given) instead of `false` placeholders
- `dump()` gains an `$offset` parameter and reports `hits`, `atime`, `embedded`, `c_len` per entry
- `info()`/`dump()` expose per-entry hit counters and `start_time`

### Fixes
- Fixed a startup hang when `values_memory_size` is below the 4M segment minimum
- Fixed Windows shared memory cleanup (view unmapped exactly once)
- Fixed valgrind-reported leaks and uninitialized reads (MINFO output, compression error paths)
