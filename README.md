# Yac - Yet Another Cache

[![AppVeyor](https://ci.appveyor.com/api/projects/status/6bu09pw8ukyx61m2/branch/master?svg=true)](https://ci.appveyor.com/project/laruence/yac/branch/master) [![Linux](https://github.com/laruence/yac/actions/workflows/linux.yml/badge.svg?branch=master)](https://github.com/laruence/yac/actions/workflows/linux.yml) [![Windows](https://github.com/laruence/yac/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/laruence/yac/actions/workflows/windows.yml)

Yac is a shared and lockless memory user data cache for PHP.

It can be used to replace APC or local memcached.

## What's new in 2.4.0

- **Embedded small values** — `NULL`, booleans, integers, strings up to 7
  bytes and empty arrays are stored directly inside the slot: no value
  allocation, no copy, no CRC. Typical caches hold a lot of these, so
  small-value workloads get noticeably faster.
- **LZ4 replaces FastLZ** — compresses faster and, more importantly,
  decompresses several times faster, so enabling `yac.compress_threshold`
  no longer costs read throughput.
- **Leaner hot paths** — `update()` commits slot fields one by one instead of
  copying the whole slot, and the slot spinlock backs off with CPU pause/yield
  instructions to burn less CPU under contention.

Behavior changes worth knowing:

- `get()` returns `NULL` on a miss instead of `false`, so a miss is
  distinguishable from a stored `false` — see [Missing keys](#missing-keys).
- Slots now track `atime` and per-entry hit counters (eviction picks the
  least recently accessed entry), `flush()` no longer resets `start_time`,
  and `dump()` gained `atime`/`hits`/`embedded`/`c_len` fields and an
  `offset` parameter.

## When to use Yac

Yac is a **lockless, shared memory cache**. It lives in the same process space as PHP (no network round-trip) and avoids coarse-grained locks — which means:

- **Best for**: read-heavy workloads with large, relatively stable key sets — configuration, routing tables, precomputed data, HTML fragments — things you read far more often than you write. There is no global lock; arbitration is per-slot, so many worker processes can share one cache and throughput **scales with the worker count**, as long as writes are spread across keys (see [Benchmarks](#benchmarks)).
- **Watch out for**: many processes writing the **same** key at once. That is the one case per-slot arbitration cannot parallelize away: under heavy same-key contention a `set()` can fail (it returns `false` — retry if the value matters), and readers see relaxed rather than strict read-your-writes consistency. If you need strong write consistency or atomic multi-key operations, use Redis or Memcached.

It trades perfect consistency for raw speed. `get()` is essentially a hash lookup in shared memory — microsecond-level latency.

## Benchmarks

16 worker processes sharing one cache, mixed reads/writes at a 100:1 ratio.
Half the keys hold 6-byte values, half hold 128-byte values; compression is
off. Numbers are aggregate ops/s across all workers:

| Backend   | Total ops/s    | Yac advantage |
|-----------|----------------|---------------|
| **Yac**   | **26,873,538** | —             |
| APCu      | 1,185,523      | 22.7x         |
| Memcached | 97,644         | 275.2x        |

Measures throughput, not consistency — see [When to use Yac](#when-to-use-yac).
Environment and reproduction: [Benchmark details](#benchmark-details).

## Requirement

- PHP 7+

## Install

### Install via PECL

```bash
$ pecl install yac
```

### Install via PIE

[PIE](https://php.github.io/pie/) (PHP Installer for Extensions) downloads, builds and installs the extension from [Packagist](https://packagist.org/packages/laruence/yac):

```bash
$ pie install laruence/yac
```

Optional serializers can be enabled at install time:

```bash
$ pie install laruence/yac --enable-json
```

See [PIE usage](https://php.github.io/pie/#docs/usage) for more options (e.g. installing a specific version).

### Compile from source

```bash
$ /path/to/phpize
$ ./configure --enable-yac \
    [--enable-msgpack] \
    [--enable-igbinary] \
    [--enable-json] \
    --with-php-config=/path/to-php-config
$ make && make install
```

The optional `--enable-msgpack`, `--enable-igbinary`, and `--enable-json` flags enable the corresponding serializers. See [Serializer](#serializer) below.

### Important: CLI mode

**Yac is disabled in CLI mode by default.** If you are testing or running scripts from the command line, add this to your `php.ini`:

```ini
yac.enable_cli = 1
```

Otherwise `new Yac()` will throw an exception.

## Note

1. Yac is a lockless cache, you should try to avoid or reduce the probability of multiple processes setting the same key simultaneously.
2. Yac uses partial CRC for integrity checks. You'd better re-arrange your cache content and place the most important (mutable) bytes at the head or tail of the value. Values shorter than 256 bytes (`YAC_FULL_CRC_THRESHOLD`) use full CRC.

## Restrictions

1. Cache key cannot be longer than 48 (`YAC_MAX_KEY_LEN`) bytes.
2. Cache value cannot be longer than 67108863 (`YAC_MAX_VALUE_RAW_LEN`) bytes, i.e. `(1 << 26) - 1`.
3. Cache value after compression cannot be longer than 1M (`YAC_MAX_RAW_COMPRESSED_LEN`) bytes.

## INI Settings

```ini
yac.enable = 1

yac.debug = 0  ; enable debug mode (PHP_INI_ALL).
               ; Currently reserved for future use.

yac.keys_memory_size = 4M  ; 4M can hold ~32K key slots, 32M can hold ~256K key slots

yac.values_memory_size = 64M

yac.compress_threshold = -1 ; -1 means no compression. A positive value N means
                            ; values larger than N bytes will be compressed before storage.
                            ; Values below 1024 are clamped to 1024 (YAC_MIN_COMPRESS_THRESHOLD).

yac.enable_cli = 0  ; whether to enable Yac in CLI mode, default 0

yac.serializer = php ; since Yac 2.2.0, specify which serializer Yac uses.
                      ; Available: php (always available),
                      ; json (requires --enable-json),
                      ; msgpack (requires --enable-msgpack),
                      ; igbinary (requires --enable-igbinary)
```

## Constants

```php
YAC_VERSION

YAC_MAX_KEY_LEN = 48
; If your key is longer than 48 bytes, consider using md5() of the key instead.

YAC_MAX_VALUE_RAW_LEN = 67108863    ; (1 << 26) - 1

YAC_MAX_RAW_COMPRESSED_LEN = 1048576  ; 1M in bytes

YAC_SERIALIZER_PHP = 0  ; always available, since Yac 2.2.0

; The following are only defined when the corresponding serializer is compiled in:
YAC_SERIALIZER_JSON     = 1  ; requires --enable-json
YAC_SERIALIZER_MSGPACK  = 2  ; requires --enable-msgpack
YAC_SERIALIZER_IGBINARY = 3  ; requires --enable-igbinary

YAC_SERIALIZER ; the serializer in use, determined by yac.serializer.
               ; Default is YAC_SERIALIZER_PHP.
```

## Supported Data Types

Yac can store all PHP types **except resources**:

| Type | Notes |
|------|-------|
| `null` | Stored as-is; to read it back unambiguously from a miss, see [Missing keys](#missing-keys) |
| `bool` | Stored as-is; a miss returns `NULL`, so a stored `false` stays distinguishable |
| `int` / `long` | Stored directly |
| `float` / `double` | Stored directly |
| `string` | Stored directly; compressed if larger than `yac.compress_threshold` |
| `array` | Serialized (php/msgpack/igbinary/json), then compressed if needed |
| `object` | Serialized and compressed same as array |
| `resource` | **Not supported** — triggers a warning |

### Missing keys

`get()` returns `NULL` for a key that does not exist. A stored `false` is
returned as `false`, so a miss and a stored `false` never collide.

> **Note**: Before 2.4.0, `get()` returned `false` for a missing key — the
> same value as a stored `false`, so the two cases were indistinguishable.

If you store `NULL` as a value, the return value is again indistinguishable
from a miss. To tell them apart, read the key via the array form:
`get(array($key))` returns an array containing only the keys that **exist** —
a missing key is simply absent from the result:

```php
<?php
$yac->set("n", NULL);

var_dump($yac->get("n"));                    // NULL — same as a miss

$ret = $yac->get(array("n"));
var_dump(array_key_exists("n", $ret));       // true — "n" exists, value is NULL

$ret = $yac->get(array("missing"));
var_dump(array_key_exists("missing", $ret)); // false — key does not exist
?>
```

## Methods

### Yac::__construct

```php
Yac::__construct([string $prefix = ""])
```

Constructor of Yac. You can specify a prefix which will be prepended to every key in subsequent `set`/`get`/`delete` calls.

The prefix is concatenated directly onto the key — no separator is added automatically. If you need a separator, include it in the prefix.

Throws an exception if:
- Yac is not enabled (`yac.enable = 0`)
- The prefix exceeds `YAC_MAX_KEY_LEN` (48) bytes

```php
<?php
$yac = new Yac("myproduct_");
?>
```

#### Multi-tenant prefix example

```php
<?php
// Two instances sharing the same memory pool but with isolated key namespaces:
$userCache = new Yac("user_");
$sysCache  = new Yac("sys_");

$userCache->set("123", $userData);  // actual key: "user_123"
$sysCache->set("config", $config);  // actual key: "sys_config"
```

### Property Access

Yac also supports array-like property access, which maps directly to `get`/`set`/`delete`:

```php
<?php
$yac = new Yac();
$yac->foo = "bar";       // equivalent to $yac->set("foo", "bar")
echo $yac->foo;          // equivalent to $yac->get("foo")
unset($yac->foo);        // equivalent to $yac->delete("foo")
```

**Note**: Property access always uses `set()` semantics (overwrite), not `add()`. TTL is not supported through property access — if you need TTL or `add` semantics, call the method explicitly.

### Yac::add

```php
Yac::add(string $key, mixed $value[, int $ttl = 0]): bool
Yac::add(array $kvs[, int $ttl = 0]): bool
```

Similar to `set`, but only stores the value if the key does **not** already exist (memcached `add` semantics).

`$ttl` is the time-to-live in seconds. `0` means the value never expires.

Returns `true` on success, `false` if the key already exists or on error.

```php
<?php
$yac = new Yac();
$yac->add("foo", "bar");   // true — key doesn't exist yet
$yac->add("foo", "new");   // false — key already exists
```

### Yac::set

```php
Yac::set(string $key, mixed $value[, int $ttl = 0]): bool
Yac::set(array $kvs[, int $ttl = 0]): bool
```

Store a value into Yac cache. Keys are cache-unique, so storing a second value with the same key will overwrite the original value.

`$ttl` is the time-to-live in seconds. `0` means the value never expires.

Returns `true` on success, `false` on error (e.g. cannot obtain CAS write lock).

```php
<?php
$yac = new Yac();
$yac->set("foo", "bar");
$yac->set([
    "dummy"  => "foo",
    "dummy2" => "foo",
]);
?>
```

#### Note

Since Yac 2.1, `set()` may fail if CAS competition occurs. For critical values, retry until success:

```php
while (!$yac->set("important", "value") && $retry++ < 100/* guard against persistent CAS failure */);
```

### Yac::get

```php
Yac::get(string $key): mixed
Yac::get(array $keys): array
```

Fetches a stored variable from the cache.

For a single key, returns the cached value on success and `NULL` when the key
does not exist (or the integrity check fails).

> **Note**: Before 2.4.0, a missing key returned `false`.

For an array of keys, returns an array of the found key-value pairs. Keys
that do not exist are simply **omitted** from the result, so the presence of
a key in the returned array means it exists in the cache:

```php
<?php
$yac = new Yac();
$yac->set("dummy", "foo");
$yac->set("dummy2", "foo");

$yac->get("dummy");                       // "foo"
$yac->get("missing");                     // NULL
$yac->get(["dummy", "missing"]);          // ["dummy" => "foo"] — "missing" omitted
?>
```

### Yac::delete

```php
Yac::delete(string|array $keys[, int $delay = 0]): bool
```

Removes a stored variable from the cache. If `$delay` is specified (in seconds), the value will be deleted after `$delay` seconds — a delayed deletion.

> **Note**: `delete()` is a logical deletion — it marks the entry as expired
> (sets its TTL to 1, or to a future timestamp for delayed deletion) but keeps
> the slot until the space is reclaimed on a future access. It does not
> immediately free memory.

Returns `true` on success, `false` on failure.

### Yac::flush

```php
Yac::flush(): bool
```

Immediately invalidates **all existing items across all Yac instances**. This does not actually free any resources — it only marks all items as invalid. The operation is global and affects the entire shared memory pool, regardless of which instance (or prefix) calls it.

### Yac::info

```php
Yac::info(): array
```

Get cache info and statistics.

```php
<?php
var_dump($yac->info());
/* will return an array like:
array(13) {
    ["memory_size"]        => int(541065216)
    ["slots_memory_size"]  => int(4194304)
    ["values_memory_size"] => int(536870912)
    ["segment_size"]       => int(4194304)
    ["segment_num"]        => int(128)
    ["miss"]               => int(0)
    ["hits"]               => int(955)
    ["fails"]              => int(0)
    ["kicks"]              => int(0)
    ["recycles"]           => int(0)
    ["start_time"]         => int(1787379043)
    ["slots_size"]         => int(32768)
    ["slots_used"]         => int(955)
}
*/
```

Each field means:

- `memory_size` — slots + values memory in total (bytes)
- `slots_memory_size` — memory reserved for the slot table (bytes)
- `values_memory_size` — memory reserved for values (bytes)
- `segment_size` — size of each value segment (bytes)
- `segment_num` — number of value segments
- `miss` — failed lookups (not found or expired)
- `hits` — successful lookups
- `fails` — failed writes (value too big to allocate, etc.)
- `kicks` — entries evicted to make room for new ones
- `recycles` — value segments wrapped around and reused
- `start_time` — when the shared memory was created (last (re)start), not reset by `flush()` (since Yac 2.4.0)
- `slots_size` — total number of slots
- `slots_used` — slots currently occupied

#### Memory management

Yac maintains two independent pools:

- **Keys memory** (`yac.keys_memory_size`) is a fixed-size hash table of
  `slots_size` slots — it caps *how many* entries can exist at once. Each key
  occupies one slot; a lookup probes up to 4 candidate slots. When inserting a
  new key whose candidate slots are all occupied, the entry with the oldest
  `atime` among them is evicted to make room — one **kick**.
- **Values memory** (`yac.values_memory_size`) is split into `segment_num`
  segments of `segment_size` bytes each (4M or more), managed as a ring:
  writes advance a per-segment cursor and space is never freed per entry. When
  an allocation no longer fits, the cursor wraps back to the start of a
  segment — one **recycle**. A recycle does not invalidate the segment at
  once: existing values stay readable until the wrapped cursor actually
  overwrites them; overwritten values fail the integrity guard and turn into
  misses.

#### What to watch

The core metric is the **hit rate**: `hits / (hits + miss)`. The counters
accumulate from `start_time`, so compute it over the deltas between two
`info()` snapshots to reflect the current window instead of the lifetime
average.

- **Hit rate is healthy (say ≥ 90%)** — the cache is fine. A high `kicks`
  alone is not a problem: it just means the key distribution is not uniform,
  so some probe groups collide more than others.
- **Hit rate low and `kicks` high** — the slot table is too small for the key
  set; live entries get evicted before they are re-read. Increase
  `yac.keys_memory_size` (4M holds ~32K slots, scaling roughly linearly).
- **Hit rate low and `recycles` frequent** — values are being overwritten
  before they get re-read. Increase `yac.values_memory_size`, or enable
  compression (`yac.compress_threshold`) to shrink stored values.
- **`recycles` frequent while the slot table still has room (`slots_used`
  below `slots_size`), regardless of hit rate** — the value ring is wrapping
  fast while keys memory is not the constraint: values memory is undersized
  for the write volume. Increase `yac.values_memory_size`, or enable
  compression (`yac.compress_threshold`).
- **`fails` > 0** — writes that could not allocate space: most commonly a
  single value larger than one segment, or transient allocator contention
  under heavy concurrent writes. Enable compression
  (`yac.compress_threshold`) or shrink oversized values.

### Yac::dump

```php
Yac::dump([int $limit = 100, [int $offset = 0]]): array
```

Dump cache entries for debugging. Returns an array of entries, each containing:

- `index` — slot index in the hash table
- `hash` — 64-bit hash of the key, used for slot probing
- `crc` — CRC32 checksum of the value payload; `0` for embedded entries
- `ttl` — expiration timestamp (unix time); `0` means never expires
- `k_len` — key length
- `v_len` — value length in bytes; for compressed entries this is the length of the **original** (uncompressed) value
- `c_len` — length of the compressed payload actually stored in shared memory, in bytes; present **only** for compressed entries (since Yac 2.4.0)
- `size` — allocated size of the value block in shared memory (bytes); `0` for embedded entries
- `atime` — last access time, updated on successful `get()`; the entry with the oldest `atime` among the candidate slots is evicted first (since Yac 2.4.0)
- `hits` — per-entry hit counter, bumped on every successful `get()`; reset when the entry is overwritten, deleted or expires (since Yac 2.4.0)
- `embedded` — whether the value is stored directly inside the slot (see below) (since Yac 2.4.0)
- `key` — the cache key

> **Note**: Before 2.4.0, `dump()` did not report `atime`, `hits`, `embedded` or `c_len` — these fields do not exist in older versions — and `v_len` was the stored (compressed) length rather than the original value length.

Small values are **embedded** in the slot itself instead of allocating a value
block: `NULL`, booleans, small integers, strings up to 7 bytes and empty
arrays. Embedded entries allocate no value memory at all, so for them `crc`
and `size` are reported as `0`, while `atime` and `hits` are kept in the slot
and remain meaningful.

`$limit` controls the maximum number of entries returned (default 100). Passing `-1` dumps **all** entries — intended for debugging only: the whole result is materialized as a PHP array and can consume a lot of memory on a busy cache.

`$offset` (since Yac 2.4.0) skips the first `$offset` occupied entries, so
`dump($limit, $offset)` returns entries `$offset + 1` through `$offset +
$limit`. If fewer than `$offset` entries exist (including an empty cache),
an empty array is returned.

> **Note**: Since `delete()` only marks entries expired (see [Yac::delete](#yacdelete)) and `dump()` is a raw scan of all occupied slots, **deleted or expired entries may still show up in the output**. To filter them out, check the `ttl` field: `ttl == 0` means never expires; a non-zero `ttl` that is less than the current time indicates an expired or deleted entry.


```php
<?php
$entries = $yac->dump(10);
foreach ($entries as $entry) {
    echo $entry["key"] . " => ttl=" . $entry["ttl"] . "\n";
}
```

`c_len` makes it easy to see how much compression saves per entry:

```php
<?php
foreach ($yac->dump() as $entry) {
    if (isset($entry["c_len"])) {
        printf("%s: %d -> %d bytes (%.0f%% saved)\n",
            $entry["key"], $entry["v_len"], $entry["c_len"],
            100 - $entry["c_len"] / $entry["v_len"] * 100);
    }
}
```

## Implementation Notes

- **Compression**: values exceeding `yac.compress_threshold` (or `YAC_STORAGE_MAX_ENTRY_LEN`) are compressed before storage — with **LZ4** since 2.4.0 (FastLZ in earlier releases). If compression would make a value *larger* (e.g. random or already-compressed data), `set()` fails with a warning instead of storing it.
- **CRC32 acceleration**: If compiled on a CPU with SSE4.2 support, Yac uses the hardware `crc32` instruction for faster integrity checks. This is detected automatically at compile time (`./configure`).
- **Shared memory**: Yac tries `mmap(MAP_ANON)` first, then `mmap(/dev/zero)`, then falls back to SysV IPC `shmget`. The chosen backend is determined at compile time.

## Benchmark details

**Workload.** Sixteen worker processes share one cache — mmap shared memory
inherited across `fork()` for Yac/APCu (the same mechanism FPM workers use),
per-process TCP connections for Memcached. Each worker runs an interleaved
read/write loop for 5 seconds at a 100:1 read:write ratio; the cache is warmed
up first so reads are hits. Numbers are aggregate ops/s across all 16 workers,
measuring real contention behavior rather than single-process speed.

The key space is split evenly across two value classes: **6-byte** values
(small enough for Yac 2.4.0 to embed directly in the slot) and **128-byte**
values. All values are random-looking text built from a shared pool of random
words, and **compression is disabled** (`yac.compress_threshold` unset,
Memcached's client-side `OPT_COMPRESSION` off) so every backend stores values
as-is and the comparison measures raw cache mechanics.

**Environment.** MacBook Pro (macOS 26.5, Apple M5 Pro, 15-core CPU), PHP 8.5,
APCu 5.1.28, php-memcached 3.4.0, Memcached on 127.0.0.1:11211.
`yac.keys_memory_size=32M`, `yac.values_memory_size=128M`, 20,000 shared keys.
Results are stable across repeated runs.

**Reproduction.**

```bash
make                                   # build modules/yac.so
bench/run_mp.sh --procs=16 --seconds=5 --ratio=100
```

Also tunable: `--keys`, `--mixed=6,128`, `--backend=yac|apcu|memcached|all`.
Unavailable backends (extension not loaded, Memcached not running) are skipped
automatically.

**Disclaimer.** These numbers were measured on one specific machine with one
specific workload and are provided for rough orientation only. They are not a
guarantee of performance: results vary with hardware, OS, PHP version and
extensions, memory sizing, and your application's actual read/write ratio, key
distribution and value sizes. Benchmark on your own hardware and workload
before making capacity or architecture decisions.

## License

[PHP-3.01](https://www.php.net/license/3_01.txt)
