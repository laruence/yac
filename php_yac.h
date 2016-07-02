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

#define PHP_YAC_VERSION "2.0.2-dev"

#define YAC_CLASS_PROPERTY_PREFIX  "_prefix"
#define YAC_ENTRY_COMPRESSED	   0x0020
#define YAC_ENTRY_TYPE_MASK        0x1f
#define YAC_ENTRY_ORIG_LEN_SHIT    6 
#define YAC_ENTRY_MAX_ORIG_LEN     ((1U << ((sizeof(int)*8 - YAC_ENTRY_ORIG_LEN_SHIT))) - 1)
#define YAC_MIN_COMPRESS_THRESHOLD 1024

ZEND_BEGIN_MODULE_GLOBALS(yac)
	zend_bool enable;
	zend_bool debug;
	size_t k_msize;
	size_t v_msize;
	ulong compress_threshold;
	zend_bool enable_cli;
#ifdef PHP_WIN32
	char *mmap_base;
#endif
ZEND_END_MODULE_GLOBALS(yac)

PHP_MINIT_FUNCTION(yac);
PHP_MSHUTDOWN_FUNCTION(yac);
PHP_RINIT_FUNCTION(yac);
PHP_RSHUTDOWN_FUNCTION(yac);
PHP_MINFO_FUNCTION(yac);

#ifdef ZTS
#define YAC_G(v) TSRMG(yac_globals_id, zend_yac_globals *, v)
extern int yac_globals_id;
#else
#define YAC_G(v) (yac_globals.v)
extern zend_yac_globals yac_globals;
#endif

#endif	/* PHP_YAC_H */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
