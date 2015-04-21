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

#if ENABLE_MSGPACK
int yac_serializer_msgpack_pack(zval *pzval, smart_str *buf, char **msg);
zval * yac_serializer_msgpack_unpack(char *content, size_t len, char **msg, zval *rv);
#else
int yac_serializer_php_pack(zval *pzval, smart_str *buf, char **msg);
zval * yac_serializer_php_unpack(char *content, size_t len, char **msg, zval *rv);
#endif

#endif	/* YAC_SERIALIZER_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
