# Yac - Yet Another Cache
[![Build Status](https://secure.travis-ci.org/laruence/yac.png)](http://travis-ci.org/laruence/yac) [![Build status](https://ci.appveyor.com/api/projects/status/6bu09pw8ukyx61m2/branch/master?svg=true)](https://ci.appveyor.com/project/laruence/yac/branch/master) [![Build Status](https://github.com/laruence/yac/workflows/integrate/badge.svg?branch=master)](https://github.com/laruence/yac/actions?query=workflow%3Aintegrate)

Yac is a shared and lockless memory user data cache for PHP.

it can be used to replace APC or local memcached.

## Requirement

- PHP 7 +

### Install

```
$/path/to/phpize
$./configure --with-php-config=/path/to/php-config
$make && make install
```

## Note

1.  Yac is a lockless cache, you should try to avoid or reduce the probability of multiple processes set one same key
2.  Yac use partial crc, you'd better re-arrange your cache content, place the most important(mutable) bytes at the head or tail

## Restrictions

1.  Cache key cannot be longer than 48 (YAC_MAX_KEY_LEN) bytes
2.  Cache Value cannot be longer than 64M (YAC_MAX_VALUE_RAW_LEN) bytes
3.  Cache Value after compressed cannot be longer than 1M (YAC_MAX_VALUE_COMPRESSED_LEN) bytes

## InIs
````
yac.enable = 1

yac.keys_memory_size = 4M ; 4M can get 30K key slots, 32M can get 100K key slots

yac.values_memory_size = 64M

yac.compress_threshold = -1

yac.enable_cli = 0 ; whether enable yac with cli, default 0

yac.serializer = php ; since yac 2.2.0 , specific seralizer yac used
                       could be json(--enable-json), msgpack(--enable-msgpack) or igbinary(--enable-igbinary)
````
## Constants
````
YAC_VERSION

YAC_MAX_KEY_LEN = 48 ; if your key is longer than this, maybe you can use md5 result as the key

YAC_MAX_VALUE_RAW_LEN = 64M

YAC_MAX_VALUE_COMPRESSED_LEN = 1M

YAC_SERIALIZER_PHP = 0   ; since yac-2.2.0

YAC_SERIALIZER_JSON = 1  ; since yac-2.2.0

YAC_SERIALIZER_MSGPACK = 2 ; since yac-2.2.0

YAC_SERIALIZER_IGBINARY = 3 ; since yac-2.2.0

YAC_SERIALIZER  ; serializer according to yac.serializer, default is YAC_SERIALIZER_PHP
````
## Methods

### Yac::\_\_construct

```php
   Yac::__construct([string $prefix = ""])
```

Constructor of Yac, you can specify a prefix which will used to prepend to any keys when doing set/get/delete

```php
<?php
   $yac = new Yac("myproduct_");
?>
```

### Yac::set

```php
   Yac::set($key, $value[, $ttl = 0])
   Yac::set(array $kvs[, $ttl = 0])
```

Store a value into Yac cache, keys are cache-unique, so storing a second value with the same key will overwrite the original value.

Return true on success, return false on error (Like no memory, can not obtain cas write right)
```php
<?php
$yac = new Yac();
$yac->set("foo", "bar");
$yac->set(
    array(
        "dummy" => "foo",
        "dummy2" => "foo",
        )
    );
?>
```
#### Note:
As Yac 2.1, Store may failure if cas competition fails, you may need to do:
```php
while (!($yac->set("important", "value")));
```
if you need the value to be stored properly.

### Yac::get

```
   Yac::get(array|string $key[, &$cas = NULL])
```

Fetches a stored variable from the cache. If an array is passed then each element is fetched and returned.

Return the value on success, return false on error(Like no memory, can not obtain cas write right)
```php
<?php
$yac = new Yac();
$yac->set("foo", "bar");
$yac->set(
    array(
        "dummy" => "foo",
        "dummy2" => "foo",
        )
    );
$yac->get("dummy");
$yac->get(array("dummy", "dummy2"));
?>
```

### Yac::delete

```
   Yac::delete(array|string $keys[, $delay=0])
```

Removes a stored variable from the cache. If delay is specified, then the value will be deleted after \$delay seconds.

### Yac::flush

```
   Yac::flush()
```

Immediately invalidates all existing items. it doesn't actually free any resources, it only marks all the items as invalid.

### Yac::info

```
   Yac::info(void)
```

Get cache info

```php
<?php
  ....
  var_dump($yac->info());
  /* will return an array like:
  array(11) {
      ["memory_size"]=> int(541065216)
      ["slots_memory_size"]=> int(4194304)
      ["values_memory_size"]=> int(536870912)
      ["segment_size"]=> int(4194304)
      ["segment_num"]=> int(128)
      ["miss"]=> int(0)
      ["hits"]=> int(955)
      ["fails"]=> int(0)
      ["kicks"]=> int(0)
      ["slots_size"]=> int(32768)
      ["slots_used"]=> int(955)
  }
  */
```
