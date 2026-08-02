# Yac - Yet Another Cache

[![Build status](https://ci.appveyor.com/api/projects/status/6bu09pw8ukyx61m2/branch/master?svg=true)](https://ci.appveyor.com/project/laruence/yac/branch/master) [![Build Status](https://github.com/laruence/yac/workflows/integrate/badge.svg)](https://github.com/laruence/yac/actions?query=workflow%3Aintegrate)

Yac is a shared and lockless memory user data cache for PHP.

It can be used to replace APC or local memcached.

## Requirement

- PHP 7+

## Install

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
2. Cache value cannot be longer than 64M (YAC_MAX_VALUE_RAW_LEN) bytes.
3. Cache value after compression cannot be longer than 1M (YAC_MAX_RAW_COMPRESSED_LEN) bytes.

## INI Settings

```ini
yac.enable = 1

yac.keys_memory_size = 4M  ; 4M can hold ~30K key slots, 32M can hold ~100K key slots

yac.values_memory_size = 64M

yac.compress_threshold = -1 ; -1 means no compression. A positive value N means
                            ; values larger than N bytes will be compressed before storage.

yac.enable_cli = 0  ; whether to enable Yac in CLI mode, default 0

yac.serializer = php ; since Yac 2.2.0, specify which serializer Yac uses.
                      ; Available: php (--enable-json), msgpack (--enable-msgpack),
                      ; or igbinary (--enable-igbinary)
```

## Constants

```php
YAC_VERSION

YAC_MAX_KEY_LEN = 48
; If your key is longer than 48 bytes, consider using md5() of the key instead.

YAC_MAX_VALUE_RAW_LEN = 67108864   ; 64M in bytes

YAC_MAX_RAW_COMPRESSED_LEN = 1048576  ; 1M in bytes

YAC_SERIALIZER_PHP      = 0  ; since Yac 2.2.0
YAC_SERIALIZER_JSON     = 1  ; since Yac 2.2.0
YAC_SERIALIZER_MSGPACK  = 2  ; since Yac 2.2.0
YAC_SERIALIZER_IGBINARY = 3  ; since Yac 2.2.0

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

```php
<?php
$yac = new Yac("myproduct_");
?>
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

Get cache info.

```php
<?php
var_dump($yac->info());
/* will return an array like:
array(11) {
    ["memory_size"]        => int(541065216)
    ["slots_memory_size"]  => int(4194304)
    ["values_memory_size"] => int(536870912)
    ["segment_size"]       => int(4194304)
    ["segment_num"]        => int(128)
    ["miss"]               => int(0)
    ["hits"]               => int(955)
    ["fails"]              => int(0)
    ["kicks"]              => int(0)
    ["slots_size"]         => int(32768)
    ["slots_used"]         => int(955)
}
*/
```

## License

[PHP-3.01](https://www.php.net/license/3_01.txt)
