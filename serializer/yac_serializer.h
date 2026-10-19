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

#ifndef YAC_SERIALIZER_H
#define YAC_SERIALIZER_H

#include "Zend/zend_smart_str.h"

#define YAC_SERIALIZE_MAX_DEPTH 512

#define YAC_SERIALIZE_T_NULL     0x00
#define YAC_SERIALIZE_T_FALSE    0x01
#define YAC_SERIALIZE_T_TRUE     0x02
#define YAC_SERIALIZE_T_LONG     0x10
#define YAC_SERIALIZE_T_DOUBLE   0x11
#define YAC_SERIALIZE_T_STRING   0x20
#define YAC_SERIALIZE_T_ARRAY    0x30
#define YAC_SERIALIZE_T_STDCLASS 0x31
#define YAC_SERIALIZE_T_ZVALS    0x40

#define YAC_SERIALIZE_ARRAY_PACKED 0x1
#define YAC_SERIALIZE_ARRAY_INT    0x2
#define YAC_SERIALIZE_ARRAY_DOUBLE 0x4
#define YAC_SERIALIZE_ARRAY_STRKEY 0x8
#define YAC_SERIALIZE_ARRAY_INTKEY 0x10
#define YAC_SERIALIZE_ARRAY_ZVALS  0x20

#define YAC_SERIALIZE_KEY_STR 0x0
#define YAC_SERIALIZE_KEY_INT 0x1

#if PHP_VERSION_ID >= 70300
#define YAC_SERIALIZE_HASH_INIT_PACKED(ht) zend_hash_real_init_packed(ht)
#else
#define YAC_SERIALIZE_HASH_INIT_PACKED(ht) zend_hash_real_init((ht), 1)
#endif

#define YAC_SERIALIZE_HASH_FILL_LONG(v) do { \
	zval yac_serialize_fill_tmp; \
	ZVAL_LONG(&yac_serialize_fill_tmp, (zend_long)(v)); \
	ZEND_HASH_FILL_ADD(&yac_serialize_fill_tmp); \
} while (0)

#define YAC_SERIALIZE_HASH_FILL_DOUBLE(v) do { \
	zval yac_serialize_fill_tmp; \
	ZVAL_DOUBLE(&yac_serialize_fill_tmp, (v)); \
	ZEND_HASH_FILL_ADD(&yac_serialize_fill_tmp); \
} while (0)

#if defined(__GNUC__)
#define YAC_SERIALIZE_PREFETCH(p) __builtin_prefetch((p), 0, 3)
#else
#define YAC_SERIALIZE_PREFETCH(p) do { } while (0)
#endif

#define YAC_UNSERIALIZE_SUPPRESS_BEGIN() int _yac_old_error_reporting = EG(error_reporting); EG(error_reporting) = 0;
#define YAC_UNSERIALIZE_SUPPRESS_END()   EG(error_reporting) = _yac_old_error_reporting;

int yac_serialize(zval *pzval, smart_str *buf, unsigned int *meta);
zval* yac_unserialize(char *content, size_t len, unsigned int meta, zval *rv);

#endif	/* YAC_SERIALIZER_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
