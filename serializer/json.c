/*
  +----------------------------------------------------------------------+
  | Yet Another Cache                                                    |
  +----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
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

#if YAC_ENABLE_JSON

#include "php.h"
#include "ext/json/php_json.h"
#include "zend_smart_str.h" /* for smart_str */

#include "yac_serializer.h"

int yac_serializer_json_pack(zval *pzval, smart_str *buf, char **msg) /* {{{ */ {
	int failed;

#if ((PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 3))
	failed = (php_json_encode(buf, pzval) == FAILURE);
#else
	failed = (php_json_encode(buf, pzval, 0) == FAILURE); /* options */
#endif

	/* the return value alone is not enough: INF/NAN only set an error code,
	 * then write a 0 and report success, so a value json_encode() itself
	 * refuses would be cached as that 0. the error code alone is not enough
	 * either, the two paths that fail on a pending exception leave it unset.
	 * PHP_JSON_THROW_ON_ERROR cannot help, it is read by json_encode()
	 * rather than by the encoder. the code is assigned on every call, so it
	 * is never a leftover from an earlier one */
#if PHP_VERSION_ID >= 80600
	failed |= (JSON_G(error_details).code != PHP_JSON_ERROR_NONE);
#else
	failed |= (JSON_G(error_code) != PHP_JSON_ERROR_NONE);
#endif

	/* buf is empty or half written whenever encoding stopped, and the caller
	 * dereferences buf->s on success */
	if (failed || EG(exception) || buf->s == NULL) {
		return 0;
	}

	return 1;
} /* }}} */

zval* yac_serializer_json_unpack(char *content, size_t len, char **msg, zval *rv) /* {{{ */ {
	char *copy;

	YAC_UNSERIALIZE_SUPPRESS_BEGIN();

	/* php_json_decode()'s scanner requires a NUL-terminated buffer; stored
	 * payloads carry no terminator, so hand it a terminated copy — decoding
	 * the unterminated original fails depending on whatever byte happens to
	 * follow the buffer in memory */
	copy = emalloc(len + 1);
	memcpy(copy, content, len);
	copy[len] = '\0';
	ZVAL_NULL(rv);
	php_json_decode(rv, copy, len, PHP_JSON_OBJECT_AS_ARRAY, 512);
	efree(copy);
	/* php_json_decode leaves rv as null on failure; a stored array or
	 * object can never legitimately decode to null */
	if (UNEXPECTED(Z_TYPE_P(rv) == IS_NULL || EG(exception))) {
		YAC_UNSERIALIZE_SUPPRESS_END();
		return NULL;
	}

	YAC_UNSERIALIZE_SUPPRESS_END();
	return rv;
} /* }}} */

#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
