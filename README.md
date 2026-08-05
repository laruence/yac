# Yac - Yet Another Cache

[![Build status](https://ci.appveyor.com/api/projects/status/6bu09pw8ukyx61m2/branch/master?svg=true)](https://ci.appveyor.com/project/laruence/yac/branch/master) [![Build Status](https://github.com/laruence/yac/workflows/integrate/badge.svg)](https://github.com/laruence/yac/actions?query=workflow%3Aintegrate)

Yac is a shared and lockless memory user data cache for PHP.

It can be used to replace APC or local memcached.

## Requirement

- PHP 7+

## Install

### Install via PECL

```bash
$ pecl install yac
```

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

## When to use Yac

Yac is a **lockless, shared memory cache**. It lives in the same process space as PHP (no network round-trip), and it never locks — which means:

- **Best for**: read-heavy workloads with large, relatively stable key sets. Think configuration, routing tables, precomputed data, HTML fragments — things you read far more often than you write.
- **Not ideal for**: high-contention write scenarios where multiple processes frequently `set()` the same keys simultaneously. There is no lock to arbitrate, so writes can corrupt under heavy contention. For those use cases, Redis or Memcached are safer choices.

It trades perfect consistency for raw speed. `get()` is essentially a hash lookup in shared memory — microsecond-level latency.

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

yac.keys_memory_size = 4M  ; 4M can hold ~30K key slots, 32M can hold ~100K key slots

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
| `null` | Stored as-is |
| `bool` | Both `true` and `false` — see [gotcha below](#false-ambiguity) |
| `int` / `long` | Stored directly |
| `float` / `double` | Stored directly |
| `string` | Stored directly; compressed if larger than `yac.compress_threshold` |
| `array` | Serialized (php/msgpack/igbinary/json), then compressed if needed |
| `object` | Serialized and compressed same as array |
| `resource` | **Not supported** — triggers a warning |

### False ambiguity

Since `get()` returns `false` both when a key is **not found** and when the cached value is literally `false`, you cannot distinguish the two cases by return value alone. Avoid storing `false` as a cache value.

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
Yac::get(string|array $key): mixed
```

Fetches a stored variable from the cache. If an array is passed, each element is fetched and returned as a key-value array.

Returns the cached value on success, `false` on failure (key not found, or integrity check failed).

> **Warning**: If the stored value is `false`, `get()` also returns `false`. See [False ambiguity](#false-ambiguity).

```php
<?php
$yac = new Yac();
$yac->set("foo", "bar");
$yac->set([
    "dummy"  => "foo",
    "dummy2" => "foo",
]);

$yac->get("dummy");
$yac->get(["dummy", "dummy2"]);
?>
```

### Yac::delete

```php
Yac::delete(string|array $keys[, int $delay = 0]): bool
```

Removes a stored variable from the cache. If `$delay` is specified (in seconds), the value will be deleted after `$delay` seconds — a delayed deletion.

> **Note**: `$delay` is a logical deletion: it marks the entry with a shorter TTL, and the space is reclaimed on the next access after expiry. It does not immediately free memory.

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
array(12) {
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
    ["slots_size"]         => int(32768)
    ["slots_used"]         => int(955)
}
*/
```

### Yac::dump

```php
Yac::dump([int $limit = 100]): array
```

Dump cache entries for debugging. Returns an array of entries, each containing `index`, `hash`, `crc`, `ttl`, `k_len`, `v_len`, `size`, and `key`.

`$limit` controls the maximum number of entries returned (default 100).

> **Note**: `delete()` is a logical deletion — it marks the entry as expired
> (sets its TTL to 1, or to a future timestamp for delayed deletion) but keeps
> the slot until the space is reclaimed on a future access. Since `dump()` is a
> raw scan of all occupied slots, **deleted entries may still show up in the
> output**. To filter them out, check the `ttl` field: `ttl == 0` means never
> expires; a non-zero `ttl` that is less than the current time indicates an
> expired or deleted entry.


```php
<?php
$entries = $yac->dump(10);
foreach ($entries as $entry) {
    echo $entry["key"] . " => ttl=" . $entry["ttl"] . "\n";
}
```

## Implementation Notes

- **Compression**: Yac uses FastLZ for compression. Values exceeding `yac.compress_threshold` (or `YAC_STORAGE_MAX_ENTRY_LEN`) are compressed before storage.
- **CRC32 acceleration**: If compiled on a CPU with SSE4.2 support, Yac uses the hardware `crc32` instruction for faster integrity checks. This is detected automatically at compile time (`./configure`).
- **Shared memory**: Yac tries `mmap(MAP_ANON)` first, then `mmap(/dev/zero)`, then falls back to SysV IPC `shmget`. The chosen backend is determined at compile time.

## License

[PHP-3.01](https://www.php.net/license/3_01.txt)
