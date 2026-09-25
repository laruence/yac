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

#ifndef PHP_YAC_H
#define PHP_YAC_H

extern zend_module_entry yac_module_entry;
#define phpext_yac_ptr &yac_module_entry

#ifdef PHP_WIN32
#define PHP_YAC_API __declspec(dllexport)
#else
#define PHP_YAC_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#define PHP_YAC_VERSION "2.5.0-dev"

#if PHP_VERSION_ID < 70400
#define YAC_WHANDLER            void
#define YAC_WHANDLER_RET(zv)    return
#else
#define YAC_WHANDLER            zval *
#define YAC_WHANDLER_RET(zv)    return zv
#endif

#define YAC_CLASS_PROPERTY_PREFIX  "_prefix"
#define YAC_ENTRY_COMPRESSED	   0x0020
#define YAC_ENTRY_TYPE_MASK        0x1f
#define YAC_ENTRY_ORIG_LEN_SHIT    6
#define YAC_ENTRY_MAX_ORIG_LEN     ((1U << ((sizeof(int)*8 - YAC_ENTRY_ORIG_LEN_SHIT))) - 1)
#define YAC_MIN_COMPRESS_THRESHOLD 1024

#define YAC_SERIALIZER_PHP         0
#define YAC_SERIALIZER_JSON        1
#define YAC_SERIALIZER_MSGPACK     2
#define YAC_SERIALIZER_IGBINARY    3

#define YAC_BUF_SIZE               1024

ZEND_BEGIN_MODULE_GLOBALS(yac)
	zend_bool enable;
	zend_bool debug;
	size_t k_msize;
	size_t v_msize;
	zend_ulong compress_threshold;
	zend_bool enable_cli;
	char *serializer;
	/* staging area for small non-string value snapshots: holds the copy
	 * from shared memory just until it is consumed, then gets reused;
	 * in YAC_G so ZTS threads each have their own */
	char yac_staging_buf[YAC_BUF_SIZE];
ZEND_END_MODULE_GLOBALS(yac)

PHP_MINIT_FUNCTION(yac);
PHP_MSHUTDOWN_FUNCTION(yac);
PHP_RINIT_FUNCTION(yac);
PHP_RSHUTDOWN_FUNCTION(yac);
PHP_MINFO_FUNCTION(yac);

ZEND_EXTERN_MODULE_GLOBALS(yac);
#define YAC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(yac, v)

#ifdef ZEND_PROPERTY_EXISTS
/* ZEND_PROPERTY_EXISTS only exists since PHP 7.4 */
#define YAC_PROPERTY_EXISTS ZEND_PROPERTY_EXISTS
#else
#define YAC_PROPERTY_EXISTS 0x2
#endif

/* Embedded scalar values (zend-type aware; the storage layer only ever sees
 * the tagged word and tests it with YAC_IS_EMBED / YAC_IS_EMBED_INLINE).
 *
 * A slot's val normally points at an 8-byte aligned block, so a real pointer
 * has zero low bits; a non-zero tag in the low 2 bits marks a value carried
 * in the word itself. The all-zero word means "empty slot".
 *
 *   tag 0x1 LONG     zend_long in the high (word bits - 2) bits
 *   tag 0x2 STR      [4..2] length 0..YAC_ZV_STR_MAX_LEN, bytes from bit 5
 *   tag 0x3 SPECIAL  [4..2] kind: NULL/TRUE/FALSE/EMPTY_ARRAY/DOUBLE,
 *                    or the top bit set instead means an INLINE value
 *
 * On 64-bit DOUBLE carries a float in [62..31], so any double that survives a
 * float round-trip embeds; 32-bit has no room and carries only +/-0.0. STR max
 * is 7 bytes on 64-bit, 3 on 32. A LONG whose payload sets the top bit still
 * never reads as INLINE: INLINE is tag 0x3, LONG is tag 0x1, and the tag is
 * what YAC_IS_EMBED_INLINE matches on.
 */
#define YAC_ZV_TAG_MASK         0x3
#define YAC_ZV_TAG_LONG         0x1
#define YAC_ZV_TAG_STR          0x2
#define YAC_ZV_TAG_SPECIAL      0x3

#define YAC_ZV_KIND_MASK        0x1f
#define YAC_ZV_NULL             0x3
#define YAC_ZV_TRUE             0x7
#define YAC_ZV_FALSE            0xb
#define YAC_ZV_EMPTY_ARRAY      0xf

#define YAC_ZV_STR_MAX_LEN      ((unsigned int)((sizeof(void*) * 8 - 5) / 8))
#define YAC_ZV_STR_LEN(p)       ((unsigned int)((((uintptr_t)(p)) >> 2) & 0x7))
#define YAC_ZV_STR_DATA(p)      (((uintptr_t)(p)) >> 5)

#if SIZEOF_SIZE_T == 8
#define YAC_ZV_HAS_DOUBLE       1
#define YAC_ZV_DOUBLE           0x13
#define YAC_ZV_DOUBLE_SHIFT     31
#else
#define YAC_ZV_DOUBLE_ZERO      0x13
#define YAC_ZV_DOUBLE_NEG_ZERO  0x17
#endif

#define yac_embed_long(v) \
	((uintptr_t)((((zend_ulong)(zend_long)(v)) << 2) | YAC_ZV_TAG_LONG))
#define yac_embed_long_val(p) \
	((zend_long)(((zend_long)(uintptr_t)(p)) >> 2))

#define yac_embed_null()        ((uintptr_t)YAC_ZV_NULL)
#define yac_embed_true()        ((uintptr_t)YAC_ZV_TRUE)
#define yac_embed_false()       ((uintptr_t)YAC_ZV_FALSE)
#define yac_embed_empty_array() ((uintptr_t)YAC_ZV_EMPTY_ARRAY)

#endif	/* PHP_YAC_H */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
