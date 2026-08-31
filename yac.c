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
#include "SAPI.h"
#include "ext/standard/info.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_exceptions.h"

#include "php_yac.h"
#if PHP_MAJOR_VERSION > 7
#include "yac_arginfo.h"
#else
#include "yac_legacy_arginfo.h"
#endif
#include "storage/yac_storage.h"
#include "storage/allocator/yac_allocator.h"
#include "serializer/yac_serializer.h"
#ifdef HAVE_LZ4_H
#include <lz4.h>
#else
#include "compressor/lz4/lz4.h"
#endif

/* Embedded value helpers (zend-type aware; tag layout in yac_storage.h).
 * shift counts are sizeof-derived, so 32/64-bit both work; encoding
 * shifts unsigned (signed left-shift of negatives would be UB), decoding
 * relies on arithmetic right shift as every supported compiler does */
static inline int yac_long_embedable(zend_long v) {
	return (((zend_ulong)(v) + ((zend_ulong)1 << (sizeof(zend_long) * 8 - 4)))
			>> (sizeof(zend_long) * 8 - 3)) == 0;
}

static inline int yac_str_embedable(zend_string *str) {
	return ZSTR_LEN(str) <= YAC_EMBED_STR_MAX_LEN;
}

static inline int yac_arr_embedable(zend_array *arr) {
	return zend_hash_num_elements(arr) == 0;
}

#define yac_embed_long(v) \
	((char *)(uintptr_t)((((zend_ulong)(zend_long)(v)) << 3) | YAC_EMBED_LONG))
#define yac_embed_long_val(p) \
	((zend_long)(((zend_long)(uintptr_t)(p)) >> 3))

#define yac_embed_null()        ((char *)(uintptr_t)YAC_EMBED_NULL)
#define yac_embed_true()        ((char *)(uintptr_t)YAC_EMBED_TRUE)
#define yac_embed_false()       ((char *)(uintptr_t)YAC_EMBED_FALSE)
#define yac_embed_empty_array() ((char *)(uintptr_t)YAC_EMBED_EMPTY_ARRAY)

static inline char *yac_embed_str(const char *s, unsigned int len) {
	uintptr_t u = YAC_EMBED_STR | ((uintptr_t)len << 3);
	unsigned int i;

	for (i = 0; i < len; i++) {
		u |= ((uintptr_t)(unsigned char)s[i]) << (6 + i * 8);
	}
	return (char *)u;
}

/* rebuild a zval straight from a tagged word; NULL means a corrupt tag
 * and the caller degrades the hit to a miss */
static zval* yac_embed_to_zval(const char *data, zval *rv) /* {{{ */ {
	switch (((uintptr_t)data) & YAC_EMBED_MASK) {
		case YAC_EMBED_NULL:
			ZVAL_NULL(rv);
			return rv;
		case YAC_EMBED_TRUE:
			ZVAL_TRUE(rv);
			return rv;
		case YAC_EMBED_FALSE:
			ZVAL_FALSE(rv);
			return rv;
		case YAC_EMBED_LONG:
			ZVAL_LONG(rv, yac_embed_long_val(data));
			return rv;
		case YAC_EMBED_STR:
			{
				unsigned int slen = YAC_EMBED_STR_LEN(data);
				uintptr_t payload = YAC_EMBED_STR_DATA(data);

				if (slen == 0) {
					ZVAL_EMPTY_STRING(rv);
				} else {
					zend_string *str = zend_string_alloc(slen, 0);
					unsigned int i;

					for (i = 0; i < slen; i++) {
						ZSTR_VAL(str)[i] = (char)((payload >> (i * 8)) & 0xff);
					}
					ZSTR_VAL(str)[slen] = '\0';
					ZVAL_NEW_STR(rv, str);
				}
				return rv;
			}
		case YAC_EMBED_EMPTY_ARRAY:
#if PHP_VERSION_ID >= 80000
			ZVAL_EMPTY_ARRAY(rv);
#else
			array_init(rv);
#endif
			return rv;
		default:
			return NULL;
	}
}
/* }}} */

zend_class_entry *yac_class_ce;

static zend_object_handlers yac_obj_handlers;

typedef struct {
	unsigned char prefix[YAC_STORAGE_MAX_KEY_LEN];
	uint16_t prefix_len;
	zend_object std;
} yac_object;

ZEND_DECLARE_MODULE_GLOBALS(yac);

static yac_serializer_t yac_serializer;
static yac_unserializer_t yac_unserializer;

static PHP_INI_MH(OnChangeKeysMemoryLimit) /* {{{ */ {
	if (new_value) {
#if PHP_VERSION_ID < 80200
		YAC_G(k_msize) = zend_atol(ZSTR_VAL(new_value), ZSTR_LEN(new_value));
#else
		YAC_G(k_msize) = zend_ini_parse_quantity_warn(new_value, entry->name);
#endif
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnChangeValsMemoryLimit) /* {{{ */ {
	if (new_value) {
#if PHP_VERSION_ID < 80200
		YAC_G(v_msize) = zend_atol(ZSTR_VAL(new_value), ZSTR_LEN(new_value));
#else
		YAC_G(v_msize) = zend_ini_parse_quantity_warn(new_value, entry->name);
#endif
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnChangeCompressThreshold) /* {{{ */ {
	if (new_value) {
#if PHP_VERSION_ID < 80200
		YAC_G(compress_threshold) = zend_atol(ZSTR_VAL(new_value), ZSTR_LEN(new_value));
#else
		YAC_G(compress_threshold) = zend_ini_parse_quantity_warn(new_value, entry->name);
#endif
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
    STD_PHP_INI_ENTRY("yac.keys_memory_size", "8M", PHP_INI_SYSTEM, OnChangeKeysMemoryLimit, k_msize, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.values_memory_size", "32M", PHP_INI_SYSTEM, OnChangeValsMemoryLimit, v_msize, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.compress_threshold", "-1", PHP_INI_SYSTEM, OnChangeCompressThreshold, compress_threshold, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.enable_cli", "0", PHP_INI_SYSTEM, OnUpdateBool, enable_cli, zend_yac_globals, yac_globals)
    STD_PHP_INI_ENTRY("yac.serializer", "php", PHP_INI_SYSTEM, OnUpdateString, serializer, zend_yac_globals, yac_globals)
PHP_INI_END()
/* }}} */

#define Z_YACOBJ_P(zv)   (php_yac_fetch_object(Z_OBJ_P(zv)))

static inline yac_object *php_yac_fetch_object(zend_object *obj) /* {{{ */ {
	return (yac_object *)((char*)(obj) - offsetof(yac_object, std));
}
/* }}} */

static const char *yac_assemble_key(yac_object *yac, zend_string *name, size_t *len) /* {{{ */ {
	const char *key;

	if ((ZSTR_LEN(name) + yac->prefix_len) > YAC_STORAGE_MAX_KEY_LEN) {
		php_error_docref(NULL, E_WARNING,
				"Key '%.*s%s' exceed max key length '%d' bytes",
				yac->prefix_len, yac->prefix, ZSTR_VAL(name), YAC_STORAGE_MAX_KEY_LEN);
		return NULL;
	}

	if (yac->prefix_len) {
		memcpy(yac->prefix + yac->prefix_len, ZSTR_VAL(name), ZSTR_LEN(name));
		key = (const char*)yac->prefix;
		*len = yac->prefix_len + ZSTR_LEN(name);
	} else {
		key = ZSTR_VAL(name);
		*len = ZSTR_LEN(name);
	}

	return key;
}
/* }}} */

static int yac_add_impl(yac_object *yac, zend_string *name, zval *value, int ttl, int add) /* {{{ */ {
	int ret = 0, flag = Z_TYPE_P(value);
	char *msg;
	time_t tv;
	const char *key;
	size_t key_len;

	if ((key = yac_assemble_key(yac, name, &key_len)) == NULL) {
		return ret;
	}

	tv = time(NULL);
	switch (Z_TYPE_P(value)) {
		case IS_NULL:
			ret = yac_storage_update(key, key_len, yac_embed_null(), 0, flag, ttl, add, tv);
			break;
		case IS_TRUE:
			ret = yac_storage_update(key, key_len, yac_embed_true(), 0, flag, ttl, add, tv);
			break;
		case IS_FALSE:
			ret = yac_storage_update(key, key_len, yac_embed_false(), 0, flag, ttl, add, tv);
			break;
		case IS_LONG:
			if (yac_long_embedable(Z_LVAL_P(value))) {
				ret = yac_storage_update(key, key_len, yac_embed_long(Z_LVAL_P(value)), 0, flag, ttl, add, tv);
			} else {
				ret = yac_storage_update(key, key_len, (char *)&Z_LVAL_P(value), sizeof(zend_long), flag, ttl, add, tv);
			}
			break;
		case IS_DOUBLE:
			ret = yac_storage_update(key, key_len, (char *)&Z_DVAL_P(value), sizeof(double), flag, ttl, add, tv);
			break;
		case IS_STRING:
#ifdef IS_CONSTANT
		case IS_CONSTANT:
#endif
			{
				if (yac_str_embedable(Z_STR_P(value))) {
					ret = yac_storage_update(key, key_len,
							yac_embed_str(Z_STRVAL_P(value), (unsigned int)Z_STRLEN_P(value)),
							Z_STRLEN_P(value), flag, ttl, add, tv);
				} else if (Z_STRLEN_P(value) > YAC_G(compress_threshold) || Z_STRLEN_P(value) > YAC_STORAGE_MAX_ENTRY_LEN) {
					int compressed_len;
					char *compressed;

					/* if longer than this, then we can not stored the length in flag */
					if (Z_STRLEN_P(value) > YAC_ENTRY_MAX_ORIG_LEN) {
						php_error_docref(NULL, E_WARNING, "Value is too long(%ld bytes) to be stored", Z_STRLEN_P(value));
						return ret;
					}

					compressed = emalloc(LZ4_compressBound(Z_STRLEN_P(value)));
					compressed_len = LZ4_compress_default(Z_STRVAL_P(value), compressed, Z_STRLEN_P(value), LZ4_compressBound(Z_STRLEN_P(value)));
					if (!compressed_len) {
						php_error_docref(NULL, E_WARNING, "Compression failed");
						efree(compressed);
						return ret;
					}
					if (compressed_len > Z_STRLEN_P(value)) {
						php_error_docref(NULL, E_WARNING,
								"Compression makes the value larger(%ld -> %d bytes), skipped",
								(long)Z_STRLEN_P(value), compressed_len);
						efree(compressed);
						return ret;
					}

					if (compressed_len > YAC_STORAGE_MAX_ENTRY_LEN) {
						php_error_docref(NULL, E_WARNING, "Value is too long(%ld bytes) to be stored", Z_STRLEN_P(value));
						efree(compressed);
						return ret;
					}

					flag |= YAC_ENTRY_COMPRESSED;
					flag |= (Z_STRLEN_P(value) << YAC_ENTRY_ORIG_LEN_SHIT);
					ret = yac_storage_update(key, key_len, compressed, compressed_len, flag, ttl, add, tv);
					efree(compressed);
				} else {
					ret = yac_storage_update(key, key_len, Z_STRVAL_P(value), Z_STRLEN_P(value), flag, ttl, add, tv);
				}
			}
			break;
		case IS_ARRAY:
#ifdef IS_CONSTANT_ARRAY
		case IS_CONSTANT_ARRAY:
#endif
			if (yac_arr_embedable(Z_ARRVAL_P(value))) {
				ret = yac_storage_update(key, key_len, yac_embed_empty_array(), 0, flag, ttl, add, tv);
				break;
			}
		case IS_OBJECT:
			{
				smart_str buf = {0};

				if (yac_serializer(value, &buf, &msg)) {
					if (buf.s->len > YAC_G(compress_threshold) || buf.s->len > YAC_STORAGE_MAX_ENTRY_LEN) {
						int compressed_len;
						char *compressed;

						if (buf.s->len > YAC_ENTRY_MAX_ORIG_LEN) {
							php_error_docref(NULL, E_WARNING, "Value is too big to be stored");
							smart_str_free(&buf);
							return ret;
						}

						compressed = emalloc(LZ4_compressBound(buf.s->len));
						compressed_len = LZ4_compress_default(ZSTR_VAL(buf.s), compressed, ZSTR_LEN(buf.s), LZ4_compressBound(buf.s->len));
						if (!compressed_len) {
							php_error_docref(NULL, E_WARNING, "Compression failed");
							smart_str_free(&buf);
							efree(compressed);
							return ret;
						}
						if (compressed_len > buf.s->len) {
							php_error_docref(NULL, E_WARNING,
									"Compression makes the value larger(%ld -> %d bytes), skipped",
									(long)buf.s->len, compressed_len);
							smart_str_free(&buf);
							efree(compressed);
							return ret;
						}

						if (compressed_len > YAC_STORAGE_MAX_ENTRY_LEN) {
							php_error_docref(NULL, E_WARNING, "Value is too big to be stored");
							smart_str_free(&buf);
							efree(compressed);
							return ret;
						}

						flag |= YAC_ENTRY_COMPRESSED;
						flag |= (buf.s->len << YAC_ENTRY_ORIG_LEN_SHIT);
						ret = yac_storage_update(key, key_len, compressed, compressed_len, flag, ttl, add, tv);
						efree(compressed);
					} else {
						ret = yac_storage_update(key, key_len, ZSTR_VAL(buf.s), ZSTR_LEN(buf.s), flag, ttl, add, tv);
					}
					smart_str_free(&buf);
				} else {
					php_error_docref(NULL, E_WARNING, "Serialization failed");
					smart_str_free(&buf);
				}
			}
			break;
		case IS_RESOURCE:
			php_error_docref(NULL, E_WARNING, "Type 'IS_RESOURCE' cannot be stored");
			break;
		default:
			php_error_docref(NULL, E_WARNING, "Unsupported valued type to be stored '%d'", flag);
			break;
	}

	return ret;
}
/* }}} */

static int yac_add_multi_impl(yac_object *yac, zval *kvs, int ttl, int add) /* {{{ */ {
	HashTable *ht = Z_ARRVAL_P(kvs);
	zend_string *key;
	zend_ulong idx;
	zval *value;

	ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, value) {
		uint32_t should_free = 0;
		if (!key) {
			key = strpprintf(0, ZEND_ULONG_FMT, idx);
			should_free = 1;
		}
		if (yac_add_impl(yac, key, value, ttl, add)) {
			if (should_free) {
				zend_string_release(key);
			}
			continue;
		} else {
			if (should_free) {
				zend_string_release(key);
			}
			return 0;
		}
	} ZEND_HASH_FOREACH_END();

	return 1;
}
/* }}} */

static inline void yac_add_update_internal(INTERNAL_FUNCTION_PARAMETERS, int add) { /* {{{ */
	zend_long ttl = 0;
	zval *keys, *value = NULL;
	int ret;

	switch (ZEND_NUM_ARGS()) {
		case 1:
			if (zend_parse_parameters(ZEND_NUM_ARGS(), "a", &keys) == FAILURE) {
				return;
			}
			break;
		case 2:
			if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &keys, &value) == FAILURE) {
				return;
			}
			if (Z_TYPE_P(keys) == IS_ARRAY) {
				if (Z_TYPE_P(value) == IS_LONG) {
					ttl = Z_LVAL_P(value);
					value = NULL;
				} else {
					php_error_docref(NULL, E_WARNING, "ttl parameter must be an integer");
					return;
				}
			}
			break;
		case 3:
			if (zend_parse_parameters(ZEND_NUM_ARGS(), "zzl", &keys, &value, &ttl) == FAILURE) {
				return;
			}
			break;
		default:
			zend_wrong_param_count();
			return;
	}

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_add_multi_impl(Z_YACOBJ_P(getThis()), keys, ttl, add);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_add_impl(Z_YACOBJ_P(getThis()), Z_STR_P(keys), value, ttl, add);
	} else {
		zend_string *key = zval_get_string(keys);
		ret = yac_add_impl(Z_YACOBJ_P(getThis()), key, value, ttl, add);
		zend_string_release(key);
	}

	RETURN_BOOL(ret);
}
/* }}} */

static zval* yac_get_impl(yac_object *yac, zend_string *name, uint32_t *cas, zval *rv) /* {{{ */ {
	uint32_t flag, size = 0;
	char *data, *msg;
	time_t tv;
	const char *key;
	size_t key_len;

	if ((key = yac_assemble_key(yac, name, &key_len)) == NULL) {
		return NULL;
	}

	tv = time(NULL);
	if (yac_storage_find(key, key_len, &data, &size, &flag, (int *)cas, tv)) {
		if (YAC_IS_EMBED(data)) {
			/* the value word itself, no heap buffer to free */
			return yac_embed_to_zval(data, rv);
		}
		switch ((flag & YAC_ENTRY_TYPE_MASK)) {
			case IS_NULL:
				if (size == sizeof(int)) {
					ZVAL_NULL(rv);
					efree(data);
					return rv;
				}
				efree(data);
				break;
			case IS_TRUE:
				if (size == sizeof(int)) {
					ZVAL_TRUE(rv);
					efree(data);
					return rv;
				}
				efree(data);
				break;
			case IS_FALSE:
				if (size == sizeof(int)) {
					ZVAL_FALSE(rv);
					efree(data);
					return rv;
				}
				efree(data);
				break;
			case IS_LONG:
				if (size == sizeof(zend_long)) {
					zend_long lval;
					memcpy(&lval, data, sizeof(zend_long));
					ZVAL_LONG(rv, lval);
					efree(data);
					return rv;
				}
				efree(data);
				break;
			case IS_DOUBLE:
				if (size == sizeof(double)) {
					ZVAL_DOUBLE(rv, *(double*)data);
					efree(data);
					return rv;
				}
				efree(data);
				break;
			case IS_STRING:
#ifdef IS_CONSTANT
			case IS_CONSTANT:
#endif
				{
					if ((flag & YAC_ENTRY_COMPRESSED)) {
						size_t orig_len = ((uint32_t)flag >> YAC_ENTRY_ORIG_LEN_SHIT);
						zend_string *str = zend_string_alloc(orig_len, 0);
						int length = LZ4_decompress_safe(data, ZSTR_VAL(str), size, orig_len);
						efree(data);
						if (length <= 0) {
							/* damaged payload, degrade to a miss silently */
							zend_string_free(str);
							break;
						}
						ZSTR_VAL(str)[length] = '\0';
						ZVAL_NEW_STR(rv, str);
					} else {
						ZVAL_STRINGL(rv, data, size);
						efree(data);
					}
					return rv;
				}
			case IS_ARRAY:
#ifdef IS_CONSTANT_ARRAY
			case IS_CONSTANT_ARRAY:
#endif
			case IS_OBJECT:
				{
					if ((flag & YAC_ENTRY_COMPRESSED)) {
						size_t orig_len = ((uint32_t)flag >> YAC_ENTRY_ORIG_LEN_SHIT);
						char *origin = emalloc(orig_len + 1);
						int length = LZ4_decompress_safe(data, origin, size, orig_len);
						if (length <= 0) {
							/* damaged payload, degrade to a miss silently */
							efree(data);
							efree(origin);
							break;
						}
						efree(data);
						data = origin;
						size = length;
					}
					rv = yac_unserializer(data, size, &msg, rv);
					efree(data);
					return rv;
				}
			default:
				/* a corrupt entry flag, degrade to a miss silently */
				efree(data);
				break;
		}
	}

	return NULL;
}
/* }}} */

static zval* yac_get_multi_impl(yac_object *yac, zval *keys, zval *def, zval *rv) /* {{{ */ {
	zval *value;
	HashTable *ht = Z_ARRVAL_P(keys);

	array_init(rv);

	ZEND_HASH_FOREACH_VAL(ht, value) {
		uint32_t lcas = 0;
		zval *v, tmp;

		switch (Z_TYPE_P(value)) {
			case IS_STRING:
				if ((v = yac_get_impl(yac, Z_STR_P(value), &lcas, &tmp))) {
					zend_symtable_update(Z_ARRVAL_P(rv), Z_STR_P(value), v);
				} else if (def) {
					zend_symtable_update(Z_ARRVAL_P(rv), Z_STR_P(value), def);
				}
				continue;
			default:
				{
					zend_string *key = zval_get_string(value);
					if ((v = yac_get_impl(yac, key, &lcas, &tmp))) {
						zend_symtable_update(Z_ARRVAL_P(rv), key, v);
					} else if (def) {
						zend_symtable_update(Z_ARRVAL_P(rv), key, def);
					}
					zend_string_release(key);
				}
				continue;
		}
	} ZEND_HASH_FOREACH_END();

	return rv;
}
/* }}} */

static int yac_delete_impl(yac_object *yac, zend_string *name, int ttl) /* {{{ */ {
	time_t tv = 0;
	const char *key;
	size_t key_len;

	if ((key = yac_assemble_key(yac, name, &key_len)) == NULL) {
		return 0;
	}

	if (ttl) {
		tv = (zend_ulong)time(NULL);
	}

	return yac_storage_delete(key, key_len, ttl, tv);
}
/* }}} */

static int yac_delete_multi_impl(yac_object *yac, zval *keys, int ttl) /* {{{ */ {
	HashTable *ht = Z_ARRVAL_P(keys);
	int ret = 1;
	zval *value;

	ZEND_HASH_FOREACH_VAL(ht, value) {
		switch (Z_TYPE_P(value)) {
			case IS_STRING:
			    ret = ret & yac_delete_impl(yac, Z_STR_P(value), ttl);
				continue;
			default:
				{
					zend_string *key = zval_get_string(value);
					ret = ret & yac_delete_impl(yac, key, ttl);
					zend_string_release(key);
				}
				continue;
		}
	} ZEND_HASH_FOREACH_END();

	return ret;
}
/* }}} */

static zend_object *yac_object_new(zend_class_entry *ce) /* {{{ */ {
	yac_object *yac = emalloc(sizeof(yac_object) + zend_object_properties_size(ce));

	zend_object_std_init(&yac->std, ce);
	yac->std.handlers = &yac_obj_handlers;
	yac->prefix_len = 0;


	return &yac->std;
}
/* }}} */

static void yac_object_free(zend_object *object) /* {{{ */ {
	zend_object_std_dtor(object);
}
/* }}} */

static zval* yac_read_property_ptr(void *zobj, void *name, int type, void **cache_slot) /* {{{ */ {
	zend_string *member;
#if PHP_VERSION_ID < 80000
	member = Z_STR_P((zval*)name);
#else
	member = (zend_string*)name;
#endif
	zend_throw_exception_ex(NULL, 0, "Retrieval of Yac->%s for modification is unsupported", ZSTR_VAL(member));
	return &EG(error_zval);
}
/* }}} */

static zval* yac_read_property(void /* for PHP8 compatibility */ *zobj, void *name, int type, void **cache_slot, zval *rv) /* {{{ */ {
	yac_object *yac;
	zend_string *member;

	if (UNEXPECTED(type == BP_VAR_RW||type == BP_VAR_W)) {
		return &EG(error_zval);
	}
#if PHP_VERSION_ID < 80000
	yac = Z_YACOBJ_P((zval*)zobj);
	member = Z_STR_P((zval*)name);
#else
	yac = php_yac_fetch_object((zend_object*)zobj);
	member = (zend_string*)name;
#endif

	if (yac_get_impl(yac, member, NULL, rv)) {
		return rv;
	}

	return &EG(uninitialized_zval);
}
/* }}} */

static YAC_WHANDLER yac_write_property(void *zobj, void *name, zval *value, void **cache_slot) /* {{{ */ {
	yac_object *yac;
	zend_string *member;

#if PHP_VERSION_ID < 80000
	yac = Z_YACOBJ_P((zval*)zobj);
	member = Z_STR_P((zval*)name);
#else
	yac = php_yac_fetch_object((zend_object*)zobj);
	member = (zend_string*)name;
#endif

	yac_add_impl(yac, member, value, 0, 0);
    Z_TRY_ADDREF_P(value);

	YAC_WHANDLER_RET(value);
}
/* }}} */

static void yac_unset_property(void *zobj, void *name, void **cache_slot) /* {{{ */ {
	yac_object *yac;
	zend_string *member;

#if PHP_VERSION_ID < 80000
	yac = Z_YACOBJ_P((zval*)zobj);
	member = Z_STR_P((zval*)name);
#else
	yac = php_yac_fetch_object((zend_object*)zobj);
	member = (zend_string*)name;
#endif

	yac_delete_impl(yac, member, 0);
}
/* }}} */

/** {{{ proto public Yac::__construct([string $prefix])
*/
PHP_METHOD(yac, __construct) {
	zend_string *prefix = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|S", &prefix) == FAILURE) {
		return;
	}

	if (!YAC_G(enable)) {
		zend_throw_exception(NULL, "Yac is not enabled", 0);
		return;
	}

	if (prefix && ZSTR_LEN(prefix)) {
		yac_object *yac;
		if (ZSTR_LEN(prefix) > YAC_STORAGE_MAX_KEY_LEN) {
			zend_throw_exception_ex(NULL, 0,
					"Prefix '%s' exceed max key length '%d' bytes", ZSTR_VAL(prefix), YAC_STORAGE_MAX_KEY_LEN);
			return;
		}
		yac = Z_YACOBJ_P(getThis());
		yac->prefix_len = ZSTR_LEN(prefix);
		memcpy(yac->prefix, ZSTR_VAL(prefix), ZSTR_LEN(prefix));
	}
}
/* }}} */

/** {{{ proto public Yac::add(mixed $keys, mixed $value[, int $ttl])
*/
PHP_METHOD(yac, add) {
	yac_add_update_internal(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */

/** {{{ proto public Yac::set(mixed $keys, mixed $value[, int $ttl])
*/
PHP_METHOD(yac, set) {
	yac_add_update_internal(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/** {{{ proto public Yac::get(mixed $keys[, mixed $default = NULL])
*/
PHP_METHOD(yac, get) {
	uint32_t lcas = 0;
	zval *ret, *keys, *def = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|z", &keys, &def) == FAILURE) {
		return;
	}

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_get_multi_impl(Z_YACOBJ_P(getThis()), keys, def, return_value);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_get_impl(Z_YACOBJ_P(getThis()), Z_STR_P(keys), &lcas, return_value);
	} else {
		zend_string *key = zval_get_string(keys);
		ret = yac_get_impl(Z_YACOBJ_P(getThis()), key, &lcas, return_value);
		zend_string_release(key);
	}

	if (ret == NULL) {
		/* miss: return the caller-provided default when given, otherwise
		 * false (the historical behavior) */
		if (def) {
			RETURN_ZVAL(def, 1, 0);
		}
		RETURN_FALSE;
	}
}
/* }}} */

/** {{{ proto public Yac::delete(mixed $key[, int $delay = 0])
*/
PHP_METHOD(yac, delete) {
	zend_long time = 0;
	zval *keys;
	int ret;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|l", &keys, &time) == FAILURE) {
		return;
	}

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		ret = yac_delete_multi_impl(Z_YACOBJ_P(getThis()), keys, time);
	} else if (Z_TYPE_P(keys) == IS_STRING) {
		ret = yac_delete_impl(Z_YACOBJ_P(getThis()), Z_STR_P(keys), time);
	} else {
		zend_string *key = zval_get_string(keys);
		ret = yac_delete_impl(Z_YACOBJ_P(getThis()), key, time);
		zend_string_release(key);
	}

	RETURN_BOOL(ret);
}
/* }}} */

/** {{{ proto public Yac::flush(void)
*/
PHP_METHOD(yac, flush) {

	yac_storage_flush();

	RETURN_TRUE;
}
/* }}} */

/** {{{ proto public Yac::info(void)
*/
PHP_METHOD(yac, info) {
	yac_storage_info *inf;

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
	add_assoc_long(return_value, "start_time", inf->start_time);
	add_assoc_long(return_value, "slots_size", inf->slots_size);
	add_assoc_long(return_value, "slots_used", inf->occupied);

	yac_storage_free_info(inf);
	return;
}
/* }}} */

/** {{{ proto public Yac::dump(int $limit, int $offset)
*/
PHP_METHOD(yac, dump) {
	zend_long limit = 100, offset = 0;
	yac_item_list *list, *l;

	array_init(return_value);

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|ll", &limit, &offset) == FAILURE) {
		return;
	}

	list = l = yac_storage_dump(limit, offset);
	for (; l; l = l->next) {
		zval item;
		array_init(&item);
		add_assoc_long(&item, "index", l->index);
		add_assoc_long(&item, "hash", l->h);
		add_assoc_long(&item, "crc", l->crc);
		add_assoc_long(&item, "ttl", l->ttl);
		add_assoc_long(&item, "k_len", l->k_len);
		if ((l->flag & YAC_ENTRY_COMPRESSED)) {
			add_assoc_long(&item, "v_len", (((uint32_t)l->flag) >> YAC_ENTRY_ORIG_LEN_SHIT));
			add_assoc_long(&item, "c_len", l->v_len);
		} else {
			add_assoc_long(&item, "v_len", l->v_len);
		}
		add_assoc_long(&item, "size", l->size);
		add_assoc_long(&item, "atime", l->atime);
		add_assoc_long(&item, "hits", l->hits);
		add_assoc_bool(&item, "embedded", l->embedded);
		add_assoc_stringl(&item, "key", (char*)l->key, l->k_len);
		add_next_index_zval(return_value, &item);
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
	PHP_ME(yac, __construct, arginfo_class_Yac___construct, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(yac, add, arginfo_class_Yac_add, ZEND_ACC_PUBLIC)
	PHP_ME(yac, set, arginfo_class_Yac_set, ZEND_ACC_PUBLIC)
	PHP_ME(yac, get, arginfo_class_Yac_get, ZEND_ACC_PUBLIC)
	PHP_ME(yac, delete, arginfo_class_Yac_delete, ZEND_ACC_PUBLIC)
	PHP_ME(yac, flush, arginfo_class_Yac_flush, ZEND_ACC_PUBLIC)
	PHP_ME(yac, info, arginfo_class_Yac_info, ZEND_ACC_PUBLIC)
	PHP_ME(yac, dump, arginfo_class_Yac_dump, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ PHP_GINIT_FUNCTION
 */
PHP_GINIT_FUNCTION(yac)
{
	memset(yac_globals, 0, sizeof(*yac_globals));
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(yac)
{
	char *msg;
	zend_class_entry ce;

	REGISTER_INI_ENTRIES();

	if (!YAC_G(enable_cli) && !strcmp(sapi_module.name, "cli")) {
		YAC_G(enable) = 0;
	}

	if (YAC_G(enable)) {
		if (YAC_G(v_msize) < YAC_SMM_SEGMENT_MIN_SIZE) {
			php_error(E_WARNING,
					"yac.values_memory_size(%lu) is below the segment minimum(%d), a single segment will be used",
					(unsigned long)YAC_G(v_msize), YAC_SMM_SEGMENT_MIN_SIZE);
		}
		if (!yac_storage_startup(YAC_G(k_msize), YAC_G(v_msize), &msg)) {
			php_error(E_ERROR, "Shared memory allocator startup failed at '%s': %s", msg, strerror(errno));
			return FAILURE;
		}
	}

	REGISTER_STRINGL_CONSTANT("YAC_VERSION", PHP_YAC_VERSION, 	sizeof(PHP_YAC_VERSION) - 1, 	CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_KEY_LEN", YAC_STORAGE_MAX_KEY_LEN, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_VALUE_RAW_LEN", YAC_ENTRY_MAX_ORIG_LEN, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_MAX_RAW_COMPRESSED_LEN", YAC_STORAGE_MAX_ENTRY_LEN, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("YAC_SERIALIZER_PHP", YAC_SERIALIZER_PHP, CONST_PERSISTENT | CONST_CS);
#if YAC_ENABLE_MSGPACK
	REGISTER_LONG_CONSTANT("YAC_SERIALIZER_MSGPACK", YAC_SERIALIZER_MSGPACK, CONST_PERSISTENT | CONST_CS);
#endif
#if YAC_ENABLE_IGBINARY
	REGISTER_LONG_CONSTANT("YAC_SERIALIZER_IGBINARY", YAC_SERIALIZER_IGBINARY, CONST_PERSISTENT | CONST_CS);
#endif
#if YAC_ENABLE_JSON
	REGISTER_LONG_CONSTANT("YAC_SERIALIZER_JSON", YAC_SERIALIZER_JSON, CONST_PERSISTENT | CONST_CS);
#endif

#if YAC_ENABLE_MSGPACK
	if (strcmp(YAC_G(serializer), "msgpack") == 0) {
		yac_serializer = yac_serializer_msgpack_pack;
		yac_unserializer = yac_serializer_msgpack_unpack;
		REGISTER_LONG_CONSTANT("YAC_SERIALIZER", YAC_SERIALIZER_MSGPACK, CONST_PERSISTENT | CONST_CS);
	} else
#endif
#if YAC_ENABLE_IGBINARY
	if (strcmp(YAC_G(serializer), "igbinary") == 0) {
		yac_serializer = yac_serializer_igbinary_pack;
		yac_unserializer = yac_serializer_igbinary_unpack;
		REGISTER_LONG_CONSTANT("YAC_SERIALIZER", YAC_SERIALIZER_IGBINARY, CONST_PERSISTENT | CONST_CS);
	} else
#endif
#if YAC_ENABLE_JSON
	if (strcmp(YAC_G(serializer), "json") == 0) {
		yac_serializer = yac_serializer_json_pack;
		yac_unserializer = yac_serializer_json_unpack;
		REGISTER_LONG_CONSTANT("YAC_SERIALIZER", YAC_SERIALIZER_JSON, CONST_PERSISTENT | CONST_CS);
	} else
#endif
	{
		yac_serializer = yac_serializer_php_pack;
		yac_unserializer = yac_serializer_php_unpack;
		REGISTER_LONG_CONSTANT("YAC_SERIALIZER", YAC_SERIALIZER_PHP, CONST_PERSISTENT | CONST_CS);
	}

	INIT_CLASS_ENTRY(ce, "Yac", yac_methods);
	yac_class_ce = zend_register_internal_class(&ce);
	yac_class_ce->ce_flags |= ZEND_ACC_FINAL;
	yac_class_ce->create_object = yac_object_new;

	memcpy(&yac_obj_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	yac_obj_handlers.offset = offsetof(yac_object, std);
	yac_obj_handlers.free_obj = yac_object_free;
	if (YAC_G(enable)) {
		yac_obj_handlers.read_property  = (zend_object_read_property_t)yac_read_property;
		yac_obj_handlers.write_property = (zend_object_write_property_t)yac_write_property;
		yac_obj_handlers.unset_property = (zend_object_unset_property_t)yac_unset_property;
		yac_obj_handlers.get_property_ptr_ptr = (zend_object_get_property_ptr_ptr_t)yac_read_property_ptr;
	}

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
	smart_str names = {0,};

	php_info_print_table_start();
	php_info_print_table_header(2, "yac support", "enabled");
	php_info_print_table_row(2, "Version", PHP_YAC_VERSION);
	php_info_print_table_row(2, "Shared Memory", yac_storage_shared_memory_name());

	smart_str_appends(&names, "php");
#if YAC_ENABLE_MSGPACK
	smart_str_appends(&names, ", msgpack");
#endif
#if YAC_ENABLE_IGBINARY
	smart_str_appends(&names, ", igbinary");
#endif
#if YAC_ENABLE_JSON
	smart_str_appends(&names, ", json");
#endif
	smart_str_0(&names);
	php_info_print_table_row(2, "Serializer", ZSTR_VAL(names.s));
	smart_str_free(&names);

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
		snprintf(buf, sizeof(buf), "%d", inf->segment_size);
		php_info_print_table_row(2, "Size of Shared Memory Segment(segment_size)", buf);
		snprintf(buf, sizeof(buf), "%d", inf->segments_num);
		php_info_print_table_row(2, "Number of Segments (segment_num)", buf);
		snprintf(buf, sizeof(buf), "%d", inf->slots_size);
		php_info_print_table_row(2, "Total Slots Number(slots_size)", buf);
		snprintf(buf, sizeof(buf), "%d", inf->occupied);
		php_info_print_table_row(2, "Total Used Slots(slots_num)", buf);
		php_info_print_table_end();

		yac_storage_free_info(inf);
	}
}
/* }}} */

#ifdef COMPILE_DL_YAC
ZEND_GET_MODULE(yac)
#endif

static zend_module_dep yac_module_deps[] = {
#if YAC_ENABLE_MSGPACK
	ZEND_MOD_REQUIRED("msgpack")
#endif
#if YAC_ENABLE_IGBINARY
	ZEND_MOD_REQUIRED("igbinary")
#endif
#if YAC_ENABLE_JSON
	ZEND_MOD_REQUIRED("json")
#endif
	{NULL, NULL, NULL, 0}
};

/* {{{ yac_module_entry
 */
zend_module_entry yac_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	yac_module_deps,
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

