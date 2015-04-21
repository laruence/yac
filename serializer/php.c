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

#if !ENABLE_MSGPACK 

#include "php.h"
#include "ext/standard/php_var.h" /* for serialize */
#include "zend_smart_str.h"

#include "yac_serializer.h"

int yac_serializer_php_pack(zval *pzval, smart_str *buf, char **msg) /* {{{ */ {
	php_serialize_data_t var_hash;

	PHP_VAR_SERIALIZE_INIT(var_hash);
	php_var_serialize(buf, pzval, &var_hash);
	PHP_VAR_SERIALIZE_DESTROY(var_hash);

	return 1;
} /* }}} */

zval * yac_serializer_php_unpack(char *content, size_t len, char **msg, zval *rv) /* {{{ */ {
	const unsigned char *p;
	php_unserialize_data_t var_hash;
	p = (const unsigned char*)content;

	ZVAL_FALSE(rv);
	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	if (!php_var_unserialize(rv, &p, p + len,  &var_hash)) {
		zval_ptr_dtor(rv);
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
		spprintf(msg, 0, "unpack error at offset %ld of %ld bytes", (long)((char*)p - content), len);
		return NULL;
	}
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);

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
