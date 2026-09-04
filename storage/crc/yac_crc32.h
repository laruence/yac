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

#ifndef YAC_CRC32_H
#define YAC_CRC32_H

#include <stdint.h>

/* CRC-32C (Castagnoli, reflected, poly 0x82F63B78): the polynomial the
 * hardware CRC instructions compute, so software and hardware chains agree
 * bit for bit */
#define YAC_CRC32_POLY          0x82F63B78u

/* above this size, large values run through three interleaved hardware
 * chains instead of the serial one */
#define YAC_CRC32_INTER_THRESHOLD 1024

/* probe the CPU and mount the matching chains; call once from
 * yac_storage_startup(), pointers are read-only afterwards */
void yac_crc32_startup(void);

uint32_t yac_crc32(const char *data, unsigned int size);

/* copy data into dst while checksumming it; bit-identical to yac_crc32() */
uint32_t yac_crc32_snapshot(char *dst, const char *data, unsigned int size);

#endif	/* YAC_CRC32_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
