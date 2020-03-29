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

#ifdef ENABLE_JSON

#include "php.h"
#include "ext/json/php_json.h"
#include "zend_smart_str.h" /* for smart_str */

#include "yac_serializer.h"

int yac_serializer_json_pack(zval *pzval, smart_str *buf, char **msg) /* {{{ */ {
#if ((PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 3))
	php_json_encode(buf, pzval);
#else
	php_json_encode(buf, pzval, 0); /* options */
#endif

	return 1;
} /* }}} */

zval* yac_serializer_json_unpack(char *content, size_t len, char **msg, zval *rv) /* {{{ */ {
	ZVAL_NULL(rv);
	php_json_decode(rv, content, len, 1, 512);

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
