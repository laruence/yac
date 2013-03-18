#Yac - Yet Another Cache


Yac is a user data cache based on shared memory for PHP

it can be used to replace APC or local memcached.


## Requirement
- PHP 5.2 +

### Install
```
$/path/to/phpize
$./configure --with-php-config=/path/to/php-config
$make && make install


## InIs

   yac.enable 

   yac.keys_memory_size
 
   yac.values_memory_size

   yac.compress_threshold


## Methods

   Yac::__construct([string $prefix = ""])

   Yac::set($key, $value[, $ttl])
   Yac::set(array $kvs[, $ttl])

   Yac::get(array|string $key)


   Yac::delete(array|string $keys[, $delay=0])



