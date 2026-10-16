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
  | Author:  Xinchen Hui   <laruence@php.net>                            |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_yac.h"
#include "yac_serializer.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"
#include "zend_smart_str.h"

static int yac_serialize_encode_array(smart_str *buf, HashTable *ht, int depth);
static int yac_serialize_encode_value(smart_str *buf, zval *zv, int depth);

static int yac_serialize_decode_array(const unsigned char **p, const unsigned char *end, zval *rv, int depth);
static int yac_serialize_decode_value(const unsigned char **p, const unsigned char *end, zval *rv, int depth);

static int yac_serialize_php(zval *pzval, smart_str *buf) /* {{{ */ {
	php_serialize_data_t var_hash;

	PHP_VAR_SERIALIZE_INIT(var_hash);
	php_var_serialize(buf, pzval, &var_hash);
	PHP_VAR_SERIALIZE_DESTROY(var_hash);

	return !EG(exception) && buf->s != NULL;
}
/* }}} */

static inline HashTable *yac_serialize_new_array(uint32_t size) /* {{{ */ {
#if PHP_VERSION_ID < 80000
	HashTable *ht;

	ALLOC_HASHTABLE(ht);
	zend_hash_init(ht, size, NULL, ZVAL_PTR_DTOR, 0);
	return ht;
#else
	return zend_new_array(size);
#endif
}
/* }}} */

static void yac_serialize_append_u32(smart_str *buf, uint32_t v) /* {{{ */ {
	unsigned char b[4];
	b[0] = (unsigned char)(v & 0xff);
	b[1] = (unsigned char)((v >> 8) & 0xff);
	b[2] = (unsigned char)((v >> 16) & 0xff);
	b[3] = (unsigned char)((v >> 24) & 0xff);
	smart_str_appendl(buf, (char *)b, 4);
}
/* }}} */

static void yac_serialize_append_u64(smart_str *buf, uint64_t v) /* {{{ */ {
	unsigned char b[8];
	int i;
	for (i = 0; i < 8; i++) {
		b[i] = (unsigned char)((v >> (i * 8)) & 0xff);
	}
	smart_str_appendl(buf, (char *)b, 8);
}
/* }}} */

static zend_bool yac_serialize_is_stdclass(zval *zv) /* {{{ */ {
	return Z_TYPE_P(zv) == IS_OBJECT && Z_OBJCE_P(zv) == zend_standard_class_def;
}
/* }}} */

static HashTable *yac_serialize_stdclass_properties(zval *zv) /* {{{ */ {
#if PHP_VERSION_ID < 80000
	return zend_std_get_properties(zv);
#else
	return zend_std_get_properties(Z_OBJ_P(zv));
#endif
}
/* }}} */

static zend_bool yac_serialize_can_encode_value(zval *zv, int depth) /* {{{ */ {
	zval *val;

	if (depth > YAC_SERIALIZE_MAX_DEPTH || Z_ISREF_P(zv)) {
		return 0;
	}
	ZVAL_DEREF(zv);
	switch (Z_TYPE_P(zv)) {
		case IS_NULL:
		case IS_FALSE:
		case IS_TRUE:
		case IS_LONG:
		case IS_DOUBLE:
		case IS_STRING:
			return 1;
		case IS_ARRAY:
			ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(zv), val) {
				if (!yac_serialize_can_encode_value(val, depth + 1)) {
					return 0;
				}
			} ZEND_HASH_FOREACH_END();
			return 1;
		case IS_OBJECT:
			if (!yac_serialize_is_stdclass(zv)) {
				return 0;
			}
			ZEND_HASH_FOREACH_VAL(yac_serialize_stdclass_properties(zv), val) {
				if (!yac_serialize_can_encode_value(val, depth + 1)) {
					return 0;
				}
			} ZEND_HASH_FOREACH_END();
			return 1;
		default:
			return 0;
	}
}
/* }}} */

static zend_bool yac_serialize_is_raw_zval(const zval *zv) /* {{{ */ {
	return Z_TYPE_P(zv) == IS_NULL || Z_TYPE_P(zv) == IS_FALSE
		|| Z_TYPE_P(zv) == IS_TRUE || Z_TYPE_P(zv) == IS_LONG
		|| Z_TYPE_P(zv) == IS_DOUBLE;
}
/* }}} */

static zend_bool yac_serialize_is_raw_zval_bytes(const unsigned char *data) /* {{{ */ {
	zval zv;

	memcpy(&zv, data, sizeof(zv));
	return yac_serialize_is_raw_zval(&zv);
}
/* }}} */

static uint32_t yac_serialize_detect_list_kind(HashTable *ht) /* {{{ */ {
	zend_string *key;
	zend_ulong h;
	zval *val;
	uint32_t index = 0;
	zend_bool all_int = 1, all_double = 1;
#if PHP_VERSION_ID >= 80200
	zend_bool all_zvals = HT_IS_PACKED(ht);
#endif

	ZEND_HASH_FOREACH_KEY_VAL(ht, h, key, val) {
		if (key || h != index++) {
			return 0;
		}
		if (Z_TYPE_P(val) != IS_LONG) {
			all_int = 0;
		}
		if (Z_TYPE_P(val) != IS_DOUBLE) {
			all_double = 0;
		}
#if PHP_VERSION_ID >= 80200
		if (!yac_serialize_is_raw_zval(val)) {
			all_zvals = 0;
		}
#endif
	} ZEND_HASH_FOREACH_END();

	if (all_int) {
		return YAC_SERIALIZE_ARRAY_INT;
	}
	if (all_double) {
		return YAC_SERIALIZE_ARRAY_DOUBLE;
	}
#if PHP_VERSION_ID >= 80200
	if (all_zvals) {
		return YAC_SERIALIZE_ARRAY_ZVALS;
	}
#endif
	return YAC_SERIALIZE_ARRAY_PACKED;
}
/* }}} */

static uint32_t yac_serialize_detect_hash_keys(HashTable *ht) /* {{{ */ {
	/* Classify a hash (non-list) array's keys: all-string, all-int, or mixed. */
	zend_string *key;
	zend_ulong h;
	zval *val;
	zend_bool has_str = 0, has_int = 0;

	ZEND_HASH_FOREACH_KEY_VAL(ht, h, key, val) {
		if (key) {
			has_str = 1;
		} else {
			has_int = 1;
		}
		if (has_str && has_int) {
			return 0; /* mixed */
		}
	} ZEND_HASH_FOREACH_END();

	if (has_str) {
		return YAC_SERIALIZE_ARRAY_STRKEY;
	}
	if (has_int) {
		return YAC_SERIALIZE_ARRAY_INTKEY;
	}
	return 0; /* empty */
}
/* }}} */

static int yac_serialize_encode_packed_values(smart_str *buf, HashTable *ht, int depth) /* {{{ */ {
	zval *val;

#if PHP_VERSION_ID >= 80200
	uint32_t i = 0;
	uint32_t count = ht->nNumUsed;

	while (i < count) {
		uint32_t start = i;

		if (yac_serialize_is_raw_zval(&ht->arPacked[i])) {
			do {
				i++;
			} while (i < count && yac_serialize_is_raw_zval(&ht->arPacked[i]));
			smart_str_appendc(buf, YAC_SERIALIZE_T_ZVALS);
			yac_serialize_append_u32(buf, i - start);
			smart_str_appendl(buf, (char *)&ht->arPacked[start], (i - start) * sizeof(zval));
			continue;
		}
		if (!yac_serialize_encode_value(buf, &ht->arPacked[i++], depth + 1)) {
			return 0;
		}
	}
	return 1;
#else
	ZEND_HASH_FOREACH_VAL(ht, val) {
		if (!yac_serialize_encode_value(buf, val, depth + 1)) {
			return 0;
		}
	} ZEND_HASH_FOREACH_END();
	return 1;
#endif
}
/* }}} */

static int yac_serialize_encode_array(smart_str *buf, HashTable *ht, int depth) /* {{{ */ {
	uint32_t count = zend_hash_num_elements(ht);
	uint32_t flags = 0;
	zval *val;

	if (depth > YAC_SERIALIZE_MAX_DEPTH) {
		php_error_docref(NULL, E_WARNING, "syac: maximum depth exceeded");
		return 0;
	}

	yac_serialize_append_u32(buf, count);

	if (count > 0 && (flags = yac_serialize_detect_list_kind(ht)) != 0) {
		yac_serialize_append_u32(buf, flags);
#if PHP_VERSION_ID >= 80200
		if (flags == YAC_SERIALIZE_ARRAY_ZVALS) {
			smart_str_appendl(buf, (char *)ht->arPacked, count * sizeof(zval));
			return 1;
		}
#endif
		if (flags == YAC_SERIALIZE_ARRAY_PACKED) {
			return yac_serialize_encode_packed_values(buf, ht, depth);
		}
		ZEND_HASH_FOREACH_VAL(ht, val) {
			if (flags == YAC_SERIALIZE_ARRAY_INT) {
				yac_serialize_append_u64(buf, (uint64_t)Z_LVAL_P(val));
			} else {
				uint64_t bits;
				double d = Z_DVAL_P(val);
				memcpy(&bits, &d, sizeof(bits));
				yac_serialize_append_u64(buf, bits);
			}
		} ZEND_HASH_FOREACH_END();
		return 1;
	}

	/* hash: classify keys so decode never guesses key type */
	flags = yac_serialize_detect_hash_keys(ht);
	yac_serialize_append_u32(buf, flags);

	if (flags == YAC_SERIALIZE_ARRAY_STRKEY) {
		zend_string *key;
		ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, val) {
			size_t klen = ZSTR_LEN(key);
			yac_serialize_append_u32(buf, (uint32_t)klen);
			smart_str_appendl(buf, ZSTR_VAL(key), klen);
			if (!yac_serialize_encode_value(buf, val, depth + 1)) {
				return 0;
			}
		} ZEND_HASH_FOREACH_END();
	} else if (flags == YAC_SERIALIZE_ARRAY_INTKEY) {
		zend_ulong h;
		ZEND_HASH_FOREACH_NUM_KEY_VAL(ht, h, val) {
			yac_serialize_append_u64(buf, (uint64_t)h);
			if (!yac_serialize_encode_value(buf, val, depth + 1)) {
				return 0;
			}
		} ZEND_HASH_FOREACH_END();
	} else {
		zend_string *key;
		zend_ulong h;
		ZEND_HASH_FOREACH_KEY_VAL(ht, h, key, val) {
			if (key) {
				size_t klen = ZSTR_LEN(key);
				smart_str_appendc(buf, YAC_SERIALIZE_KEY_STR);
				yac_serialize_append_u32(buf, (uint32_t)klen);
				smart_str_appendl(buf, ZSTR_VAL(key), klen);
			} else {
				smart_str_appendc(buf, YAC_SERIALIZE_KEY_INT);
				yac_serialize_append_u64(buf, (uint64_t)h);
			}
			if (!yac_serialize_encode_value(buf, val, depth + 1)) {
				return 0;
			}
		} ZEND_HASH_FOREACH_END();
	}
	return 1;
}
/* }}} */

static int yac_serialize_encode_value(smart_str *buf, zval *zv, int depth) /* {{{ */ {
	if (depth > YAC_SERIALIZE_MAX_DEPTH) {
		php_error_docref(NULL, E_WARNING, "syac: maximum depth exceeded");
		return 0;
	}

	ZVAL_DEREF(zv);

	switch (Z_TYPE_P(zv)) {
		case IS_NULL:
			smart_str_appendc(buf, YAC_SERIALIZE_T_NULL);
			break;
		case IS_FALSE:
			smart_str_appendc(buf, YAC_SERIALIZE_T_FALSE);
			break;
		case IS_TRUE:
			smart_str_appendc(buf, YAC_SERIALIZE_T_TRUE);
			break;
		case IS_LONG:
			smart_str_appendc(buf, YAC_SERIALIZE_T_LONG);
			yac_serialize_append_u64(buf, (uint64_t)Z_LVAL_P(zv));
			break;
		case IS_DOUBLE: {
			double d = Z_DVAL_P(zv);
			uint64_t bits;
			memcpy(&bits, &d, sizeof(bits));
			smart_str_appendc(buf, YAC_SERIALIZE_T_DOUBLE);
			yac_serialize_append_u64(buf, bits);
			break;
		}
		case IS_STRING: {
			zend_string *s = Z_STR_P(zv);
			size_t len = ZSTR_LEN(s);
			smart_str_appendc(buf, YAC_SERIALIZE_T_STRING);
			yac_serialize_append_u32(buf, (uint32_t)len);
			smart_str_appendl(buf, ZSTR_VAL(s), len);
			break;
		}
		case IS_ARRAY:
			smart_str_appendc(buf, YAC_SERIALIZE_T_ARRAY);
			if (!yac_serialize_encode_array(buf, Z_ARRVAL_P(zv), depth + 1)) {
				return 0;
			}
			break;
		case IS_OBJECT:
			if (!yac_serialize_is_stdclass(zv)) {
				return 0;
			}
			smart_str_appendc(buf, YAC_SERIALIZE_T_STDCLASS);
			if (!yac_serialize_encode_array(buf, yac_serialize_stdclass_properties(zv), depth + 1)) {
				return 0;
			}
			break;
		default:
			php_error_docref(NULL, E_WARNING, "syac: unsupported value type in array");
			return 0;
	}
	return 1;
}
/* }}} */

static zend_always_inline uint32_t yac_serialize_read_u32(const unsigned char *p) /* {{{ */ {
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}
/* }}} */

static zend_always_inline uint64_t yac_serialize_read_u64(const unsigned char *p) /* {{{ */ {
	return (uint64_t)p[0]
		| ((uint64_t)p[1] << 8)
		| ((uint64_t)p[2] << 16)
		| ((uint64_t)p[3] << 24)
		| ((uint64_t)p[4] << 32)
		| ((uint64_t)p[5] << 40)
		| ((uint64_t)p[6] << 48)
		| ((uint64_t)p[7] << 56);
}
/* }}} */

static int yac_serialize_decode_array(const unsigned char **p, const unsigned char *end, zval *rv, int depth) /* {{{ */ {
	uint32_t count, flags, i;

	if (*p + 8 > end) {
		return 0;
	}
	count = yac_serialize_read_u32(*p); *p += 4;
	flags = yac_serialize_read_u32(*p); *p += 4;

	if (depth > YAC_SERIALIZE_MAX_DEPTH) {
		php_error_docref(NULL, E_WARNING, "syac: maximum depth exceeded");
		return 0;
	}

	#if PHP_VERSION_ID >= 80200
	if (flags & YAC_SERIALIZE_ARRAY_ZVALS) {
		HashTable *ht;
		size_t size = (size_t)count * sizeof(zval);

		if (*p + size > end) {
			return 0;
		}
		ht = yac_serialize_new_array(count);
		YAC_SERIALIZE_HASH_INIT_PACKED(ht);
		memcpy(ht->arPacked, *p, size);
		ht->nNumUsed = count;
		ht->nNumOfElements = count;
		ht->nNextFreeElement = count;
		*p += size;
		ZVAL_ARR(rv, ht);
		return 1;
	}
	#endif

	/* homogeneous int list: one bounds check, one switch for N elements */
	if (flags & YAC_SERIALIZE_ARRAY_INT) {
		HashTable *ht;
		if (*p + (size_t)count * 8 > end) {
			return 0;
		}
		ht = yac_serialize_new_array(count);
		YAC_SERIALIZE_HASH_INIT_PACKED(ht);
		YAC_SERIALIZE_PREFETCH(*p);
		ZVAL_ARR(rv, ht);
		ZEND_HASH_FILL_PACKED(ht) {
			for (i = 0; i < count; i++) {
				YAC_SERIALIZE_HASH_FILL_LONG(yac_serialize_read_u64(*p));
				*p += 8;
			}
		} ZEND_HASH_FILL_END();
		return 1;
	}

	/* homogeneous double list */
	if (flags & YAC_SERIALIZE_ARRAY_DOUBLE) {
		HashTable *ht;
		if (*p + (size_t)count * 8 > end) {
			return 0;
		}
		ht = yac_serialize_new_array(count);
		YAC_SERIALIZE_HASH_INIT_PACKED(ht);
		YAC_SERIALIZE_PREFETCH(*p);
		ZVAL_ARR(rv, ht);
		ZEND_HASH_FILL_PACKED(ht) {
			for (i = 0; i < count; i++) {
				uint64_t bits = yac_serialize_read_u64(*p);
				double d;
				*p += 8;
				memcpy(&d, &bits, sizeof(d));
				YAC_SERIALIZE_HASH_FILL_DOUBLE(d);
			}
		} ZEND_HASH_FILL_END();
		return 1;
	}

	if (flags & YAC_SERIALIZE_ARRAY_PACKED) {
#if PHP_VERSION_ID >= 80200
		HashTable *ht = yac_serialize_new_array(count);
		zval *dst;

		YAC_SERIALIZE_HASH_INIT_PACKED(ht);
		YAC_SERIALIZE_PREFETCH(*p);
		dst = ht->arPacked;
		for (i = 0; i < count;) {
			zval tmp;

			if (*p < end && **p == YAC_SERIALIZE_T_ZVALS) {
				uint32_t run;
				size_t size;

				(*p)++;
				if (*p + 4 > end) {
					goto packed_fail;
				}
				run = yac_serialize_read_u32(*p);
				*p += 4;
				size = (size_t)run * sizeof(zval);
				if (!run || run > count - i || *p + size > end) {
					goto packed_fail;
				}
				for (uint32_t j = 0; j < run; j++) {
					if (!yac_serialize_is_raw_zval_bytes(*p + j * sizeof(zval))) {
						goto packed_fail;
					}
				}
				memcpy(dst + i, *p, size);
				*p += size;
				i += run;
				continue;
			}
			ZVAL_UNDEF(&tmp);
			if (!yac_serialize_decode_value(p, end, &tmp, depth + 1)) {
				zval_ptr_dtor(&tmp);
				goto packed_fail;
			}
			ZVAL_COPY_VALUE(dst + i++, &tmp);
		}
		ht->nNumUsed = count;
		ht->nNumOfElements = count;
		ht->nNextFreeElement = count;
		ZVAL_ARR(rv, ht);
		return 1;

packed_fail:
		for (uint32_t j = 0; j < i; j++) {
			zval_ptr_dtor(dst + j);
		}
		zend_hash_destroy(ht);
		FREE_HASHTABLE(ht);
		return 0;
#else
		HashTable *ht = yac_serialize_new_array(count);
		int ok = 1;

		YAC_SERIALIZE_HASH_INIT_PACKED(ht);
		YAC_SERIALIZE_PREFETCH(*p);
		ZVAL_ARR(rv, ht);
		ZEND_HASH_FILL_PACKED(ht) {
			for (i = 0; i < count; i++) {
				zval tmp;

				ZVAL_UNDEF(&tmp);
				if (!yac_serialize_decode_value(p, end, &tmp, depth + 1)) {
					zval_ptr_dtor(&tmp);
					ok = 0;
					break;
				}
				ZEND_HASH_FILL_ADD(&tmp);
			}
		} ZEND_HASH_FILL_END();
		if (!ok) {
			zval_ptr_dtor(rv);
			ZVAL_UNDEF(rv);
			return 0;
		}
		return 1;
#endif
	}

	/* hash arrays: fill buckets and hash chains directly. */
	{
		HashTable *ht = yac_serialize_new_array(count);

#if PHP_VERSION_ID >= 80000
		zend_hash_real_init_mixed(ht);
		HT_FLAGS(ht) &= ~HASH_FLAG_STATIC_KEYS;
#else
		zend_hash_real_init(ht, 0);
		ht->u.flags &= ~HASH_FLAG_STATIC_KEYS;
#endif
		ZVAL_ARR(rv, ht);
		for (i = 0; i < count; i++) {
			Bucket *bucket = ht->arData + i;
			uint32_t nIndex;

			if (flags & YAC_SERIALIZE_ARRAY_STRKEY) {
				uint32_t klen;
				if (*p + 4 > end) {
					goto hash_fail;
				}
				klen = yac_serialize_read_u32(*p); *p += 4;
				if (*p + klen > end) {
					goto hash_fail;
				}
				bucket->key = zend_string_init((const char *)*p, klen, 0);
				*p += klen;
				bucket->h = zend_string_hash_val(bucket->key);
			} else if (flags & YAC_SERIALIZE_ARRAY_INTKEY) {
				if (*p + 8 > end) {
					goto hash_fail;
				}
				bucket->key = NULL;
				bucket->h = (zend_ulong)yac_serialize_read_u64(*p);
				*p += 8;
			} else if (*p < end && **p == YAC_SERIALIZE_KEY_STR) {
				uint32_t klen;
				(*p)++;
				if (*p + 4 > end) {
					goto hash_fail;
				}
				klen = yac_serialize_read_u32(*p); *p += 4;
				if (*p + klen > end) {
					goto hash_fail;
				}
				bucket->key = zend_string_init((const char *)*p, klen, 0);
				*p += klen;
				bucket->h = zend_string_hash_val(bucket->key);
			} else if (*p < end && **p == YAC_SERIALIZE_KEY_INT) {
				(*p)++;
				if (*p + 8 > end) {
					goto hash_fail;
				}
				bucket->key = NULL;
				bucket->h = (zend_ulong)yac_serialize_read_u64(*p);
				*p += 8;
			} else {
				goto hash_fail;
			}
			ZVAL_UNDEF(&bucket->val);
			if (!yac_serialize_decode_value(p, end, &bucket->val, depth + 1)) {
				zval_ptr_dtor(&bucket->val);
				if (bucket->key) {
					zend_string_release(bucket->key);
				}
				goto hash_fail;
			}
			nIndex = (uint32_t)bucket->h | ht->nTableMask;
			Z_NEXT(bucket->val) = HT_HASH(ht, nIndex);
			HT_HASH(ht, nIndex) = HT_IDX_TO_HASH(i);
			ht->nNumUsed++;
			ht->nNumOfElements++;
			if (!bucket->key && bucket->h <= (zend_ulong)ZEND_LONG_MAX
					&& bucket->h >= ht->nNextFreeElement) {
				ht->nNextFreeElement = bucket->h < (zend_ulong)ZEND_LONG_MAX
					? bucket->h + 1 : ZEND_LONG_MAX;
			}
		}
		return 1;

	hash_fail:
		zval_ptr_dtor(rv);
		ZVAL_UNDEF(rv);
		return 0;
	}
}
/* }}} */

static int yac_serialize_decode_value(const unsigned char **p, const unsigned char *end, zval *rv, int depth) /* {{{ */ {
	const unsigned char *cur = *p;

	if (cur >= end) {
		return 0;
	}

	switch (*cur++) {
		case YAC_SERIALIZE_T_NULL:
			ZVAL_NULL(rv);
			break;
		case YAC_SERIALIZE_T_FALSE:
			ZVAL_FALSE(rv);
			break;
		case YAC_SERIALIZE_T_TRUE:
			ZVAL_TRUE(rv);
			break;
		case YAC_SERIALIZE_T_LONG:
			if (cur + 8 > end) {
				return 0;
			}
			ZVAL_LONG(rv, (zend_long)yac_serialize_read_u64(cur));
			cur += 8;
			break;
		case YAC_SERIALIZE_T_DOUBLE: {
			uint64_t bits;
			double d;
			if (cur + 8 > end) {
				return 0;
			}
			bits = yac_serialize_read_u64(cur);
			memcpy(&d, &bits, sizeof(d));
			ZVAL_DOUBLE(rv, d);
			cur += 8;
			break;
		}
		case YAC_SERIALIZE_T_STRING: {
			uint32_t len;
			zend_string *s;
			if (cur + 4 > end) {
				return 0;
			}
			len = yac_serialize_read_u32(cur);
			cur += 4;
			if (cur + (size_t)len > end) {
				return 0;
			}
			YAC_SERIALIZE_PREFETCH(cur);
			s = zend_string_init((const char *)cur, len, 0);
			cur += len;
			ZVAL_STR(rv, s);
			break;
		}
		case YAC_SERIALIZE_T_ARRAY:
			*p = cur;
			return yac_serialize_decode_array(p, end, rv, depth);
		case YAC_SERIALIZE_T_STDCLASS: {
			zval properties;

			ZVAL_UNDEF(&properties);
			*p = cur;
			if (!yac_serialize_decode_array(p, end, &properties, depth)) {
				return 0;
			}
			object_and_properties_init(rv, zend_standard_class_def, Z_ARRVAL(properties));
			return 1;
		}
		default:
			return 0;
	}

	*p = cur;
	return 1;
}
/* }}} */

static zend_bool yac_serialize_can_pack(zval *pzval) /* {{{ */ {
	return (Z_TYPE_P(pzval) == IS_ARRAY || yac_serialize_is_stdclass(pzval))
		&& yac_serialize_can_encode_value(pzval, 0);
}
/* }}} */

static int yac_serialize_pack(zval *pzval, smart_str *buf) /* {{{ */ {
	return yac_serialize_encode_value(buf, pzval, 0);
}
/* }}} */

static zval *yac_serialize_unpack(char *content, size_t len, zval *rv) /* {{{ */ {
	const unsigned char *p = (const unsigned char *)content;
	const unsigned char *end = p + len;
	ZVAL_UNDEF(rv);
	if (!yac_serialize_decode_value(&p, end, rv, 0) || p != end) {
		zval_ptr_dtor(rv);
		return NULL;
	}
	return rv;
}
/* }}} */

int yac_serialize(zval *pzval, smart_str *buf, unsigned int *meta) /* {{{ */ {
	if (yac_serialize_can_pack(pzval)) {
		*meta = YAC_META_SERIALIZER_YAC;
		return yac_serialize_pack(pzval, buf);
	}

	*meta = 0;
	return yac_serialize_php(pzval, buf);
}
/* }}} */

static zval *yac_unserialize_php(char *content, size_t len, zval *rv) /* {{{ */ {
	const unsigned char *p = (const unsigned char *)content;
	php_unserialize_data_t var_hash;

	YAC_UNSERIALIZE_SUPPRESS_BEGIN();
	ZVAL_FALSE(rv);
	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	if (!php_var_unserialize(rv, &p, p + len, &var_hash)
			|| (size_t)(p - (const unsigned char *)content) != len) {
		zval_ptr_dtor(rv);
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
		YAC_UNSERIALIZE_SUPPRESS_END();
		return NULL;
	}
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
	YAC_UNSERIALIZE_SUPPRESS_END();
	return rv;
}
/* }}} */

zval *yac_unserialize(char *content, size_t len, unsigned int meta, zval *rv) /* {{{ */ {
	if (meta & YAC_META_SERIALIZER_YAC) {
		return yac_serialize_unpack(content, len, rv);
	}

	return yac_unserialize_php(content, len, rv);
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
