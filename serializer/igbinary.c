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
  |          Remi Collet   <remi@php.net>                                |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef YAC_ENABLE_IGBINARY

#include "php.h"
#include "ext/igbinary/igbinary.h"
#include "zend_smart_str.h" /* for smart_str */

#include "yac_serializer.h"

int yac_serializer_igbinary_pack(zval *pzval, smart_str *buf, char **msg) /* {{{ */ {
	uint8_t *ret;
	size_t ret_len;

	if (igbinary_serialize(&ret, &ret_len, pzval) == 0) {
		smart_str_appendl(buf, (const char *)ret, ret_len);
		efree(ret);
		return 1;
	}
	return 0;
} /* }}} */

zval * yac_serializer_igbinary_unpack(char *content, size_t len, char **msg, zval *rv) /* {{{ */ {

	ZVAL_NULL(rv);
	igbinary_unserialize((uint8_t *)content, len, rv);
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
