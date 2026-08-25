Yac 2.4.0 Release Notes (draft — release rolled back 2026-08-25, re-use when re-tagging)

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

## Pending before re-release

- Windows x86 CI: `TEST 43/56 [tests/043.phpt]` fails on all PHP versions (x86-only); run-tests.php
  dies with a 4MB allocation error right after that test. Root cause not yet identified.

## Re-release checklist

1. Fix/skip the x86 043.phpt issue
2. `git tag 2.4.0 && git push origin 2.4.0` (tag push triggers nothing; release created via gh)
3. `gh release create 2.4.0 --title 2.4.0 --notes-file RELEASE_NOTES.md --prerelease`
   (or drop --prerelease) — this fires the `release` workflow that builds Windows DLLs
4. package.xml changelog already carries these notes; keep in sync
