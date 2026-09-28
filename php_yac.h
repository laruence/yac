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
#define YAC_MIN_COMPRESS_THRESHOLD 1024

/* The PHP layer's meta, carried opaquely inside YAC_VAL_PACK(meta, kind). Only
 * BLOB payloads use it; STRING and scalars store meta 0.
 *
 *   bit 0    compressed (LZ4)
 *   bit 1    serialized: the payload is a serialized value, not a raw string
 *   bit 2+   orig_len, meaningful only when compressed
 */
#define YAC_META_COMPRESSED      0x1u
#define YAC_META_SERIALIZED      0x2u
#define YAC_META_ORIG_SHIFT      2
#define YAC_META_MAX_ORIG_LEN    ((1u << 26) - 1)
#define YAC_META_ORIG_LEN(meta)  ((uint32_t)((meta) >> YAC_META_ORIG_SHIFT))
#define YAC_META_PACK_LEN(orig)  (((uint32_t)(orig) << YAC_META_ORIG_SHIFT))

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

#define YAC_FLAG_NULL           0x1
#define YAC_FLAG_TRUE           0x2
#define YAC_FLAG_FALSE          0x3
#define YAC_FLAG_EMPTY_ARRAY    0x4

#endif	/* PHP_YAC_H */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
