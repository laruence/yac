Yac 2.4.0 Release Notes

## Notes

- Embed small scalars directly in the hash slot, avoiding value allocations for tiny entries
- Switch compression from FastLZ to LZ4 (better ratio and speed)
- get() accepts an optional $default argument (returned on miss instead of NULL)
- dump() gains an $offset parameter and reports hits, atime, embedded and c_len per entry
- Per-entry hit counters and start_time exposed in info()/dump()
- Multi-get omits missing keys (filled with $default when given) instead of false placeholders
- Field-by-field slot commits and spinlock backoff (pause/yield) reduce contention overhead
- Fixed a startup hang when values_memory_size is below the 4M segment minimum
- Fixed Windows shared memory cleanup (view unmapped exactly once)
- Fixed valgrind-reported leaks and uninitialized reads (MINFO output, compression error paths)

## Re-release record

- First 2.4.0 tag was rolled back the same day: the Windows x86 CI failed at
  tests/043.phpt. Root cause was the test itself — its PHP-side MurmurHash2
  replica overflowed 32-bit integers (not a storage bug); the resulting flood
  of deprecation notices exhausted x86 run-tests.php's memory. Fixed in
  ef989d3, Windows (x86/x64) and Linux CI all green.

## Re-release checklist

1. `git tag 2.4.0 && git push origin 2.4.0` (tag push triggers nothing; release created via gh)
2. `gh release create 2.4.0 --title 2.4.0 --notes-file RELEASE_NOTES.md`
   — this fires the `release` workflow that builds Windows DLLs
3. package.xml changelog already carries these notes; keep in sync
