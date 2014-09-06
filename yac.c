/*
  +----------------------------------------------------------------------+
  | Yet Another Cache                                                    |
  +----------------------------------------------------------------------+
  | Copyright (c) 2013-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui <laruence@php.net>                               |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_smart_str.h" /* for smart_str */
#include "SAPI.h"

#include "php_yac.h"
#include "storage/yac_storage.h"
#include "serializer/yac_serializer.h"
#ifdef HAVE_FASTLZ_H
#include <fastlz.h>
#else
#include "compressor/fastlz/fastlz.h"
#endif

zend_class_entry *yac_class_ce;

ZEND_DECLARE_MODULE_GLOBALS(yac);

/** {{{ ARG_INFO
 */
ZEND_BEGIN_ARG_INFO_EX(arginfo_yac_constructor, 0, 0, 0)
	ZEND_ARG_INFO(0, prefix)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_yac_add, 0, 0, 1)
	ZEND_ARG_INFO(0, keys)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_yac_get, 0, 0, 1)
	ZEND_ARG_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_yac_delete, 0, 0, 1)
	ZEND_ARG_INFO(0, keys)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_yac_void, 0, 0, 0)
ZEND_END_ARG_INFO()
/* }}} */

static PHP_INI_MH(OnChangeKeysMemoryLimit) /* {{{ */ {
	if (new_value) {
		YAC_G(k_msize) = zend_atol(new_value, new_value_length);
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnChangeValsMemoryLimit) /* {{{ */ {
	if (new_value) {
		YAC_G(v_msize) = zend_atol(new_value, new_value_length);
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnChangeCompressThreshold) /* {{{ */ {
	if (new_value) {
		YAC_G(compress_threshold) = zend_atol(new_value, new_value_length);
		if (YAC_G(compress_threshold) < YAC_MIN_COMPRESS_THRESHOLD) {
			YAC_G(compress_threshold) = YAC_MIN_COMPRESS_THRESHOLD;
		}
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_INI
 */
PHP_INI_BEGIN()
    STD_PHP_INI_BOOLEAN("yac.enable", "1", PHP_INI_SYSTEM, OnUpdateBool, enable, zend_yac_globals, yac_globals)
    STD_PHP_INI_BOOLEAN("yac.debug", "0", PHP_INI_ALL, OnUpdateBool, debug, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.keys_memory_size", "4M", PHP_INI_SYSTEM, OnChangeKeysMemoryLimit, k_msize, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.values_memory_size", "64M", PHP_INI_SYSTEM, OnChangeValsMemoryLimit, v_msize, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.compress_threshold", "-1", PHP_INI_SYSTEM, OnChangeCompressThreshold, compress_threshold, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.enable_cli", "0", PHP_INI_SYSTEM, OnUpdateBool, enable_cli, zend_yac_globals, yac_globals)
PHP_INI_END()
/* }}} */

static int yac_add_impl(char *prefix, uint prefix_len, char *key, uint len, zval *value, int ttl, int add TSRMLS_DC) /* {{{ */ {
	int ret = 0, flag = Z_TYPE_P(value);
	char *msg, buf[YAC_STORAGE_MAX_KEY_LEN];
	time_t tv;

	if ((len + prefix_len) > YAC_STORAGE_MAX_KEY_LEN) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Key%s can not be longer than %d bytes",
				prefix_len? "(include prefix)" : "", YAC_STORAGE_MAX_KEY_LEN);
		return ret;
	}

	if (prefix_len) {
		len = snprintf(buf, sizeof(buf), "%s%s", prefix, key);
		key = (char *)buf;
	}

	
	tv = time(NULL);
	switch (Z_TYPE_P(value)) {
		case IS_NULL:
			ret = yac_storage_update(key, len, (char *)&flag, sizeof(int), flag, ttl, add, tv);
			break;
		case IS_BOOL:
		case IS_LONG:
			ret = yac_storage_update(key, len, (char *)&Z_LVAL_P(value), sizeof(long), flag, ttl, add, tv);
			break;
		case IS_DOUBLE:
			ret = yac_storage_update(key, len, (char *)&Z_DVAL_P(value), sizeof(double), flag, ttl, add, tv);
			break;
		case IS_STRING:
		case IS_CONSTANT:
			{
				if (Z_STRLEN_P(value) > YAC_G(compress_threshold) || Z_STRLEN_P(value) > YAC_STORAGE_MAX_ENTRY_LEN) {
					int compressed_len;
					char *compressed;
				   
					/* if longer than this, then we can not stored the length in flag */
					if (Z_STRLEN_P(value) > YAC_ENTRY_MAX_ORIG_LEN) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Value is too long(%d bytes) to be stored", Z_STRLEN_P(value));
						return ret;
					}

					compressed = emalloc(Z_STRLEN_P(value) * 1.05);
					compressed_len = fastlz_compress(Z_STRVAL_P(value), Z_STRLEN_P(value), compressed);
					if (!compressed_len || compressed_len > Z_STRLEN_P(value)) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Compression failed");
						efree(compressed);
						return ret;
					}

					if (compressed_len > YAC_STORAGE_MAX_ENTRY_LEN) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Value is too long(%d bytes) to be stored", Z_STRLEN_P(value));
						efree(compressed);
						return ret;
					}

					flag |= YAC_ENTRY_COMPRESSED;
					flag |= (Z_STRLEN_P(value) << YAC_ENTRY_ORIG_LEN_SHIT);
					ret = yac_storage_update(key, len, compressed, compressed_len, flag, ttl, add, tv);
					efree(compressed);
				} else {
					ret = yac_storage_update(key, len, Z_STRVAL_P(value), Z_STRLEN_P(value), flag, ttl, add, tv);
				}
			}
			break;
		case IS_ARRAY:
#ifdef IS_CONSTANT_ARRAY
		case IS_CONSTANT_ARRAY:
#endif
		case IS_OBJECT:
			{
				smart_str buf = {0};
#if ENABLE_MSGPACK
				if (yac_serializer_msgpack_pack(value, &buf, &msg TSRMLS_CC)) {
#else

				if (yac_serializer_php_pack(value, &buf, &msg TSRMLS_CC)) {
#endif
					if (buf.len > YAC_G(compress_threshold) || buf.len > YAC_STORAGE_MAX_ENTRY_LEN) {
						int compressed_len;
						char *compressed;

						if (buf.len > YAC_ENTRY_MAX_ORIG_LEN) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Value is too big to be stored");
							return ret;
						}

						compressed = emalloc(buf.len * 1.05);
						compressed_len = fastlz_compress(buf.c, buf.len, compressed);
						if (!compressed_len || compressed_len > buf.len) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Compression failed");
							efree(compressed);
							return ret;
						}

						if (compressed_len > YAC_STORAGE_MAX_ENTRY_LEN) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Value is too big to be stored");
							efree(compressed);
							return ret;
						}

						flag |= YAC_ENTRY_COMPRESSED;
						flag |= (buf.len << YAC_ENTRY_ORIG_LEN_SHIT);
						ret = yac_storage_update(key, len, compressed, compressed_len, flag, ttl, add, tv);
						efree(compressed);
					} else {
						ret = yac_storage_update(key, len, buf.c, buf.len, flag, ttl, add, tv);
					}
					smart_str_free(&buf);
				} else {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Serialization failed");
					smart_str_free(&buf);
				}
			}
			break;
		case IS_RESOURCE:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Type 'IS_RESOURCE' cannot be stored");
			break;
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unsupported valued type to be stored '%d'", flag);
			break;
	}

	return ret;
}
/* }}} */

static int yac_add_multi_impl(char *prefix, uint prefix_len, zval *kvs, int ttl, int add TSRMLS_DC) /* {{{ */ {
	HashTable *ht = Z_ARRVAL_P(kvs);

	for (zend_hash_internal_pointer_reset(ht);
			zend_hash_has_more_elements(ht) == SUCCESS;
			zend_hash_move_forward(ht)) {
		char *key;
		ulong idx;
		zval **value;
		uint len, should_free = 0;

		if (zend_hash_get_current_data(ht, (void **)&value) == FAILURE) {
			continue;
		}

		switch (zend_hash_get_current_key_ex(ht, &key, &len, &idx, 0, NULL)) {
			case HASH_KEY_IS_LONG:
				len = spprintf(&key, 0, "%lu", idx) + 1;
				should_free = 1;
			case HASH_KEY_IS_STRING:
				if (yac_add_impl(prefix, prefix_len, key, len - 1, *value, ttl, add TSRMLS_CC)) {
					if (should_free) {
						efree(key);
					}
					continue;
				} else {
					if (should_free) {
						efree(key);
					}
					return 0;
				}
			default:
				continue;
		}
	}

	return 1;
}
/* }}} */

static zval * yac_get_impl(char * prefix, uint prefix_len, char *key, uint len, uint *cas TSRMLS_DC) /* {{{ */ {
	zval *ret = NULL;
	uint flag, size = 0;
	char *data, *msg, buf[YAC_STORAGE_MAX_KEY_LEN];
	time_t tv;

	if ((len + prefix_len) > YAC_STORAGE_MAX_KEY_LEN) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Key%s can not be longer than %d bytes",
				prefix_len? "(include prefix)" : "", YAC_STORAGE_MAX_KEY_LEN);
		return ret;
	}

	if (prefix_len) {
		len = snprintf(buf, sizeof(buf), "%s%s", prefix, key);
		key = (char *)buf;
	}

	tv = time(NULL);
	if (yac_storage_find(key, len, &data, &size, &flag, (int *)cas, tv)) {
		switch ((flag & YAC_ENTRY_TYPE_MASK)) {
			case IS_NULL:
				if (size == sizeof(int)) {
					MAKE_STD_ZVAL(ret);
					ZVAL_NULL(ret);
				}
				efree(data);
				break;
			case IS_BOOL:
			case IS_LONG:
				if (size == sizeof(long)) {
					MAKE_STD_ZVAL(ret);
					Z_TYPE_P(ret) = flag;
					Z_LVAL_P(ret) = *(long*)data;
				}
				efree(data);
				break;
			case IS_DOUBLE:
				if (size == sizeof(double)) {
					MAKE_STD_ZVAL(ret);
					Z_TYPE_P(ret) = flag;
					Z_DVAL_P(ret) = *(double*)data;
				}
				efree(data);
				break;
			case IS_STRING:
			case IS_CONSTANT:
				{
					if ((flag & YAC_ENTRY_COMPRESSED)) {
						size_t orig_len = ((uint)flag >> YAC_ENTRY_ORIG_LEN_SHIT);
						char *origin = emalloc(orig_len + 1);
						uint length;
					    length = fastlz_decompress(data, size, origin, orig_len);
						if (!length) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Decompression failed");
							efree(data);
							efree(origin);
							return ret;
						}
						MAKE_STD_ZVAL(ret);
						ZVAL_STRINGL(ret, origin, length, 0);
						efree(data);
					} else {
						MAKE_STD_ZVAL(ret);
						ZVAL_STRINGL(ret, data, size, 0);
						Z_TYPE_P(ret) = flag;
					}
				}
				break;
			case IS_ARRAY:
#ifdef IS_CONSTANT_ARRAY
			case IS_CONSTANT_ARRAY:
#endif
			case IS_OBJECT:
				{
					if ((flag & YAC_ENTRY_COMPRESSED)) {
						size_t length, orig_len = ((uint)flag >> YAC_ENTRY_ORIG_LEN_SHIT);
						char *origin = emalloc(orig_len + 1);
					    length = fastlz_decompress(data, size, origin, orig_len);
						if (!length) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Decompression failed");
							efree(data);
							efree(origin);
							return ret;
						}
						efree(data);
						data = origin;
						size = length;
					}
#if ENABLE_MSGPACK
					ret = yac_serializer_msgpack_unpack(data, size, &msg TSRMLS_CC);
#else
					ret = yac_serializer_php_unpack(data, size, &msg TSRMLS_CC);
#endif
					if (!ret) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unserialization failed");
					}
					efree(data);
				}
				break;
			default:
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected valued type '%d'", flag);
				break;
		}
	}

	return ret;
}
/* }}} */

static zval * yac_get_multi_impl(char *prefix, uint prefix_len, zval *keys, zval *cas TSRMLS_DC) /* {{{ */ {
	zval *ret;
	HashTable *ht = Z_ARRVAL_P(keys);

	MAKE_STD_ZVAL(ret);
	array_init(ret);

	for (zend_hash_internal_pointer_reset(ht);
			zend_hash_has_more_elements(ht) == SUCCESS;
			zend_hash_move_forward(ht)) {
		uint cas;
		zval **value, *v;

		if (zend_hash_get_current_data(ht, (void **)&value) == FAILURE) {
			continue;
		}

		switch (Z_TYPE_PP(value)) {
			case IS_STRING:
				if ((v = yac_get_impl(prefix, prefix_len, Z_STRVAL_PP(value), Z_STRLEN_PP(value), &cas TSRMLS_CC))) {
					add_assoc_zval_ex(ret, Z_STRVAL_PP(value), Z_STRLEN_PP(value) + 1, v);
				} else {
					add_assoc_bool_ex(ret, Z_STRVAL_PP(value), Z_STRLEN_PP(value) + 1, 0);
				}
				continue;
			default:
				{
					zval copy;
					int use_copy;
					zend_make_printable_zval(*value, &copy, &use_copy);
					if ((v = yac_get_impl(prefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), &cas TSRMLS_CC))) {
						add_assoc_zval_ex(ret, Z_STRVAL(copy), Z_STRLEN(copy) + 1, v);
					} else {
						add_assoc_bool_ex(ret, Z_STRVAL(copy), Z_STRLEN(copy) + 1, 0);
					}
					zval_dtor(&copy);
				}
				continue;
		}
	}

	return ret;
}
/* }}} */

void yac_delete_impl(char *prefix, uint prefix_len, char *key, uint len, int ttl TSRMLS_DC) /* {{{ */ {
	char buf[YAC_STORAGE_MAX_KEY_LEN];
	time_t tv = 0;

	if ((len + prefix_len) > YAC_STORAGE_MAX_KEY_LEN) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Key%s can not be longer than %d bytes",
				prefix_len? "(include prefix)" : "", YAC_STORAGE_MAX_KEY_LEN);
		return;
	}

	if (prefix_len) {
		len = snprintf(buf, sizeof(buf), "%s%s", prefix, key);
		key = (char *)buf;
	}

	if (ttl) {
		tv = (ulong)time(NULL);
	}

	yac_storage_delete(key, len, ttl, tv);
}
/* }}} */

static void yac_delete_multi_impl(char *prefix, uint prefix_len, zval *keys, int ttl TSRMLS_DC) /* {{{ */ {
	HashTable *ht = Z_ARRVAL_P(keys);

	for (zend_hash_internal_pointer_reset(ht);
			zend_hash_has_more_elements(ht) == SUCCESS;
			zend_hash_move_forward(ht)) {
		zval **value;

		if (zend_hash_get_current_data(ht, (void **)&value) == FAILURE) {
			continue;
		}

		switch (Z_TYPE_PP(value)) {
			case IS_STRING:
				yac_delete_impl(prefix, prefix_len, Z_STRVAL_PP(value), Z_STRLEN_PP(value), ttl TSRMLS_CC);
				continue;
			default:
				{
					zval copy;
					int use_copy;
					zend_make_printable_zval(*value, &copy, &use_copy);
					yac_delete_impl(prefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), ttl TSRMLS_CC);
					zval_dtor(&copy);
				}
				continue;
		}
	}
}
/* }}} */

/** {{{ proto public Yac::__construct([string $prefix])
*/
PHP_METHOD(yac, __construct) {
	char *prefix;
	uint len = 0;

	if (!YAC_G(enable)) {
		return;
	}

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s", &prefix, &len) == FAILURE) {
		return;
	}

	if (len == 0) {
		return;
	}

	zend_update_property_stringl(yac_class_ce, getThis(), ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, prefix, len TSRMLS_CC);

}
/* }}} */

/** {{{ proto public Yac::add(mixed $keys, mixed $value[, int $ttl])
*/
PHP_METHOD(yac, add) {
    long ttl = 0;
	zval *keys, *prefix, *value = NULL;
	char *sprefix = NULL;
	uint ret, prefix_len = 0;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}

	switch (ZEND_NUM_ARGS()) {
		case 1:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &keys) == FAILURE) {
				return;
			}
			break;
		case 2:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz", &keys, &value) == FAILURE) {
				return;
			}
			if (Z_TYPE_P(keys) == IS_ARRAY) {
				if (Z_TYPE_P(value) == IS_LONG) {
					ttl = Z_LVAL_P(value);
					value = NULL;
				} else {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "ttl parameter must be an integer");
					return;
				}
			}
			break;
		case 3:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zzl", &keys, &value, &ttl) == FAILURE) {
				return;
			}
			break;
		default:
			WRONG_PARAM_COUNT;
	}

	prefix = zend_read_property(yac_class_ce, getThis(), ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, 0 TSRMLS_CC);
	sprefix = Z_STRVAL_P(prefix);
	prefix_len = Z_STRLEN_P(prefix);

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_add_multi_impl(sprefix, prefix_len, keys, ttl, 1 TSRMLS_CC);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_add_impl(sprefix, prefix_len, Z_STRVAL_P(keys), Z_STRLEN_P(keys), value, ttl, 1 TSRMLS_CC);
	} else {
		zval copy;
		int use_copy;
		zend_make_printable_zval(keys, &copy, &use_copy);
		ret = yac_add_impl(sprefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), value, ttl, 1 TSRMLS_CC);
		zval_dtor(&copy);
	}

	RETURN_BOOL(ret? 1 : 0);
}
/* }}} */

/** {{{ proto public Yac::set(mixed $keys, mixed $value[, int $ttl])
*/
PHP_METHOD(yac, set) {
    long ttl = 0;
	zval *keys, *prefix, *value = NULL;
	char *sprefix = NULL;
	uint ret, prefix_len = 0;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}

	switch (ZEND_NUM_ARGS()) {
		case 1:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &keys) == FAILURE) {
				return;
			}
			break;
		case 2:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz", &keys, &value) == FAILURE) {
				return;
			}
			if (Z_TYPE_P(keys) == IS_ARRAY) {
				if (Z_TYPE_P(value) == IS_LONG) {
					ttl = Z_LVAL_P(value);
					value = NULL;
				} else {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "ttl parameter must be an integer");
					return;
				}
			}
			break;
		case 3:
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zzl", &keys, &value, &ttl) == FAILURE) {
				return;
			}
			break;
		default:
			WRONG_PARAM_COUNT;
	}

	prefix = zend_read_property(yac_class_ce, getThis(), ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, 0 TSRMLS_CC);
	sprefix = Z_STRVAL_P(prefix);
	prefix_len = Z_STRLEN_P(prefix);

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_add_multi_impl(sprefix, prefix_len, keys, ttl, 0 TSRMLS_CC);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_add_impl(sprefix, prefix_len, Z_STRVAL_P(keys), Z_STRLEN_P(keys), value, ttl, 0 TSRMLS_CC);
	} else {
		zval copy;
		int use_copy;
		zend_make_printable_zval(keys, &copy, &use_copy);
		ret = yac_add_impl(sprefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), value, ttl, 0 TSRMLS_CC);
		zval_dtor(&copy);
	}

	RETURN_BOOL(ret? 1 : 0);
}
/* }}} */

/** {{{ proto public Yac::get(mixed $keys[, int &$cas])
*/
PHP_METHOD(yac, get) {
	char *sprefix = NULL;
	uint prefix_len = 0, lcas = 0;
	zval *ret, *keys, *prefix, *cas = NULL;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|z", &keys, &cas) == FAILURE) {
		return;
	}

	prefix = zend_read_property(yac_class_ce, getThis(), ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, 0 TSRMLS_CC);
	sprefix = Z_STRVAL_P(prefix);
	prefix_len = Z_STRLEN_P(prefix);

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_get_multi_impl(sprefix, prefix_len, keys, cas TSRMLS_CC);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_get_impl(sprefix, prefix_len, Z_STRVAL_P(keys), Z_STRLEN_P(keys), &lcas TSRMLS_CC);
	} else {
		zval copy;
		int use_copy;
		zend_make_printable_zval(keys, &copy, &use_copy);
		ret = yac_get_impl(sprefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), &lcas TSRMLS_CC);
		zval_dtor(&copy);
	}

	if (ret) {
		RETURN_ZVAL(ret, 1, 1);
	} else {
		RETURN_FALSE;
	}
}
/* }}} */

/** {{{ proto public Yac::delete(mixed $key[, int $delay = 0])
*/
PHP_METHOD(yac, delete) {
	long time = 0;
	zval *keys, *prefix;
	char *sprefix = NULL;
	uint prefix_len = 0;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|l", &keys, &time) == FAILURE) {
		return;
	}

	prefix = zend_read_property(yac_class_ce, getThis(), ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, 0 TSRMLS_CC);
	sprefix = Z_STRVAL_P(prefix);
	prefix_len = Z_STRLEN_P(prefix);

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		yac_delete_multi_impl(sprefix, prefix_len, keys, time TSRMLS_CC);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		yac_delete_impl(sprefix, prefix_len, Z_STRVAL_P(keys), Z_STRLEN_P(keys), time TSRMLS_CC);
	} else {
		zval copy;
		int use_copy;
		zend_make_printable_zval(keys, &copy, &use_copy);
		yac_delete_impl(sprefix, prefix_len, Z_STRVAL(copy), Z_STRLEN(copy), time TSRMLS_CC);
		zval_dtor(&copy);
	}

	RETURN_TRUE;
}
/* }}} */

/** {{{ proto public Yac::flush(void)
*/
PHP_METHOD(yac, flush) {

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}

	yac_storage_flush();

	RETURN_TRUE;
}
/* }}} */

/** {{{ proto public Yac::info(void)
*/
PHP_METHOD(yac, info) {
	yac_storage_info *inf;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}
	
	inf = yac_storage_get_info();

	array_init(return_value);

	add_assoc_long(return_value, "memory_size", inf->k_msize + inf->v_msize);
	add_assoc_long(return_value, "slots_memory_size", inf->k_msize);
	add_assoc_long(return_value, "values_memory_size", inf->v_msize);
	add_assoc_long(return_value, "segment_size", inf->segment_size);
	add_assoc_long(return_value, "segment_num", inf->segments_num);
	add_assoc_long(return_value, "miss", inf->miss);
	add_assoc_long(return_value, "hits", inf->hits);
	add_assoc_long(return_value, "fails", inf->fails);
	add_assoc_long(return_value, "kicks", inf->kicks);
	add_assoc_long(return_value, "recycles", inf->recycles);
	add_assoc_long(return_value, "slots_size", inf->slots_size);
	add_assoc_long(return_value, "slots_used", inf->slots_num);

	yac_storage_free_info(inf);
}
/* }}} */

/** {{{ proto public Yac::dump(int $limit)
*/
PHP_METHOD(yac, dump) {
	long limit = 100;
	yac_item_list *list, *l;

	if (!YAC_G(enable)) {
		RETURN_FALSE;
	}
	
	array_init(return_value);

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &limit) == FAILURE) {
		return;
	}

	list = l = yac_storage_dump(limit);
	for (; l; l = l->next) {
		zval *item;
		MAKE_STD_ZVAL(item);
		array_init(item);
		add_assoc_long(item, "index", l->index);
		add_assoc_long(item, "hash", l->h);
		add_assoc_long(item, "crc", l->crc);
		add_assoc_long(item, "ttl", l->ttl);
		add_assoc_long(item, "k_len", l->k_len);
		add_assoc_long(item, "v_len", l->v_len);
		add_assoc_long(item, "size", l->size);
		add_assoc_string(item, "key", l->key, 1);
		add_next_index_zval(return_value, item);
	}

	yac_storage_free_list(list);
	return;
}
/* }}} */

#if 0
only OO-style APIs is supported now
/* {{{{ proto bool yac_add(mixed $keys, mixed $value[, int $ttl])
 */
PHP_FUNCTION(yac_add)
{
	PHP_MN(yac_add)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto bool yac_set(mixed $keys, mixed $value[, int $ttl])
 */
PHP_FUNCTION(yac_set)
{
	PHP_MN(yac_set)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto bool yac_get(mixed $keys[, int &$cas])
 */
PHP_FUNCTION(yac_get)
{
	PHP_MN(yac_get)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto bool yac_delete(mixed $keys[, int $delay = 0])
 */
PHP_FUNCTION(yac_delete)
{
	PHP_MN(yac_delete)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto bool yac_flush(void)
 */
PHP_FUNCTION(yac_flush)
{
	PHP_MN(yac_flush)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto bool yac_info(void)
 */
PHP_FUNCTION(yac_info)
{
	PHP_MN(yac_info)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ yac_functions[] */
zend_function_entry yac_functions[] = {
	PHP_FE(yac_add, arginfo_yac_add)
	PHP_FE(yac_set, arginfo_yac_add)
	PHP_FE(yac_get, arginfo_yac_get)
	PHP_FE(yac_delete, arginfo_yac_delete)
	PHP_FE(yac_flush, arginfo_yac_void)
	PHP_FE(yac_info, arginfo_yac_void)
	{NULL, NULL}
};
/* }}} */
#endif

/** {{{ yac_methods
*/
zend_function_entry yac_methods[] = {
	PHP_ME(yac, __construct, arginfo_yac_constructor, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(yac, add, arginfo_yac_add, ZEND_ACC_PUBLIC)
	PHP_ME(yac, set, arginfo_yac_add, ZEND_ACC_PUBLIC)
	PHP_ME(yac, get, arginfo_yac_get, ZEND_ACC_PUBLIC)
	PHP_ME(yac, delete, arginfo_yac_delete, ZEND_ACC_PUBLIC)
	PHP_ME(yac, flush, arginfo_yac_void, ZEND_ACC_PUBLIC)
	PHP_ME(yac, info, arginfo_yac_void, ZEND_ACC_PUBLIC)
	PHP_ME(yac, dump, arginfo_yac_void, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ PHP_GINIT_FUNCTION
 */
PHP_GINIT_FUNCTION(yac)
{
	yac_globals->enable = 1;
	yac_globals->k_msize = (4 * 1024 * 1024);
	yac_globals->v_msize = (64 * 1024 * 1024);
	yac_globals->debug = 0;
	yac_globals->compress_threshold = -1;
	yac_globals->enable_cli = 0;
#ifdef PHP_WIN32
	yac_globals->mmap_base = NULL;
#endif
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(yac)
{
	char *msg;
	zend_class_entry ce;

	REGISTER_INI_ENTRIES();

	if(!YAC_G(enable_cli) && !strcmp(sapi_module.name, "cli")) {
		YAC_G(enable) = 0;
	}

	if (YAC_G(enable)) {
		if (!yac_storage_startup(YAC_G(k_msize), YAC_G(v_msize), &msg)) {
			php_error(E_ERROR, "Shared memory allocator startup failed at '%s': %s", msg, strerror(errno));
			return FAILURE;
		}
	}

	REGISTER_STRINGL_CONSTANT("YAC_VERSION", PHP_YAC_VERSION, 	sizeof(PHP_YAC_VERSION) - 1, 	CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_KEY_LEN", YAC_STORAGE_MAX_KEY_LEN, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_VALUE_RAW_LEN", YAC_ENTRY_MAX_ORIG_LEN, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_RAW_COMPRESSED_LEN", YAC_STORAGE_MAX_ENTRY_LEN, CONST_PERSISTENT | CONST_CS);
#if ENABLE_MSGPACK
	REGISTER_STRINGL_CONSTANT("YAC_SERIALIZER", "PHP", sizeof("MSGPACK") -1, CONST_PERSISTENT | CONST_CS);
#else
	REGISTER_STRINGL_CONSTANT("YAC_SERIALIZER", "PHP", sizeof("PHP") -1, CONST_PERSISTENT | CONST_CS);
#endif

	INIT_CLASS_ENTRY(ce, "Yac", yac_methods);
	yac_class_ce = zend_register_internal_class(&ce TSRMLS_CC);
	zend_declare_property_stringl(yac_class_ce, ZEND_STRS(YAC_CLASS_PROPERTY_PREFIX) - 1, "", 0, ZEND_ACC_PROTECTED TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(yac)
{
	UNREGISTER_INI_ENTRIES();
	if (YAC_G(enable)) {
		yac_storage_shutdown();
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(yac)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "yac support", "enabled");
	php_info_print_table_row(2, "Version", PHP_YAC_VERSION);
	php_info_print_table_row(2, "Shared Memory", yac_storage_shared_memory_name());
#if ENABLE_MSGPACK
	php_info_print_table_row(2, "Serializer", "msgpack");
#else
	php_info_print_table_row(2, "Serializer", "php");
#endif
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();

	if (YAC_G(enable)) {
		char buf[64];
		yac_storage_info *inf;
		inf = yac_storage_get_info();

		php_info_print_table_start();
		php_info_print_table_colspan_header(2, "Cache info");
		snprintf(buf, sizeof(buf), "%ld", inf->k_msize + inf->v_msize);
		php_info_print_table_row(2, "Total Shared Memory Usage(memory_size)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->k_msize);
		php_info_print_table_row(2, "Total Shared Memory Usage for keys(keys_memory_size)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->v_msize);
		php_info_print_table_row(2, "Total Shared Memory Usage for values(values_memory_size)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->segment_size);
		php_info_print_table_row(2, "Size of Shared Memory Segment(segment_size)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->segments_num);
		php_info_print_table_row(2, "Number of Segments (segment_num)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->slots_size);
		php_info_print_table_row(2, "Total Slots Number(slots_size)", buf);
		snprintf(buf, sizeof(buf), "%ld", inf->slots_num);
		php_info_print_table_row(2, "Total Used Slots(slots_num)", buf);
		php_info_print_table_end();

		yac_storage_free_info(inf);
	}
}
/* }}} */

#ifdef COMPILE_DL_YAC
ZEND_GET_MODULE(yac)
#endif

/* {{{ yac_module_entry
 */
zend_module_entry yac_module_entry = {
	STANDARD_MODULE_HEADER,
	"yac",
	NULL, /* yac_functions, */
	PHP_MINIT(yac),
	PHP_MSHUTDOWN(yac),
	NULL,
	NULL,
	PHP_MINFO(yac),
	PHP_YAC_VERSION,
	PHP_MODULE_GLOBALS(yac),
	PHP_GINIT(yac),
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
