#Yac - Yet Another Cache
[![Build Status](https://secure.travis-ci.org/laruence/yac.png)](http://travis-ci.org/laruence/yac)

Yac is a user data cache based on shared memory for PHP

it can be used to replace APC or local memcached.


## Requirement
- PHP 5.2 +

### Install
```
$/path/to/phpize
$./configure --with-php-config=/path/to/php-config
$make && make install
```


## InIs

   yac.enable = 1

   yac.keys_memory_size = 4M
  
   yac.values_memory_size = 64M
 
   yac.compress_threshold = -1 

## Constants

   YAC_VERSION
   
   YAC_MAX_KEY_LEN  =  32
   
   YAC_MAX_VALUE_RAW_LEN = 64M
   
   YAC_MAX_VALUE_COMPRESSED_LEN = 1M

## Methods

### Yac::__construct
```
   Yac::__construct([string $prefix = ""])
```
   Constructor of Yac, you can specific a prefix, which will used to prepend to any keys when you doing set/get/delete
```php
<?php
   $yac = new Yac("myproduct_");
?>
```

### Yac::set
```
   Yac::set($key, $value[, $ttl])
   Yac::set(array $kvs[, $ttl])
```
   Store a value into Yac cache, keys are cache-unique, so storing a second value with the same key will overwrite the original value. 
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

### Yac::get
```
   Yac::get(array|string $key)
```
   Fetchs a stored variable from the cache. If an array is passed then each element is fetched and returned.


### Yac::delete
```
   Yac::delete(array|string $keys[, $delay=0])
```
   Removes a stored variable from the cache. If delay is specificed, then the value will be delete after $delay seconds.

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

## TODO
   1. Windows supports
   2. Test in real life applications


