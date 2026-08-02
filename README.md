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
$ ./configure --with-php-config=/path/to/php-config
$ make && make install
```

## Note

1. Yac is a lockless cache, you should try to avoid or reduce the probability of multiple processes setting the same key simultaneously.
2. Yac uses partial CRC for integrity checks. You'd better re-arrange your cache content and place the most important (mutable) bytes at the head or tail of the value.

## Restrictions

1. Cache key cannot be longer than 48 (YAC_MAX_KEY_LEN) bytes.
2. Cache value cannot be longer than 67108863 (YAC_MAX_VALUE_RAW_LEN) bytes, i.e. `(1 << 26) - 1`.
3. Cache value after compression cannot be longer than 1M (YAC_MAX_RAW_COMPRESSED_LEN) bytes.

## INI Settings

```ini
yac.enable = 1

yac.debug = 0  ; enable debug mode (PHP_INI_ALL)

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

### Property Access

Yac also supports array-like property access, which maps directly to `get`/`set`/`delete`:

```php
<?php
$yac = new Yac();
$yac->foo = "bar";       // equivalent to $yac->set("foo", "bar")
echo $yac->foo;          // equivalent to $yac->get("foo")
unset($yac->foo);        // equivalent to $yac->delete("foo")
```

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

Returns `true` on success, `false` on error (e.g. no memory left, or cannot obtain CAS write lock).

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
while (!$yac->set("important", "value"));
```

### Yac::get

```php
Yac::get(string|array $key[, int &$cas = null]): mixed
```

Fetches a stored variable from the cache. If an array is passed, each element is fetched and returned as a key-value array.

`$cas` is an output parameter that receives the CAS token of the retrieved value, useful for implementing compare-and-swap patterns.

Returns the cached value on success, `false` on failure.

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

Returns `true` on success, `false` on failure.

### Yac::flush

```php
Yac::flush(): bool
```

Immediately invalidates all existing items. This does not actually free any resources — it only marks all items as invalid.

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

```php
<?php
$entries = $yac->dump(10);
foreach ($entries as $entry) {
    echo $entry["key"] . " => ttl=" . $entry["ttl"] . "\n";
}
```

## License

[PHP-3.01](https://www.php.net/license/3_01.txt)
