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

#ifndef YAC_STORAGE_H
#define YAC_STORAGE_H

#define YAC_STORAGE_MAX_ENTRY_LEN  	(1 << 20)
#define YAC_STORAGE_MAX_KEY_LEN		(32)
#define YAC_STORAGE_FACTOR 			(1.25)
#define YAC_KEY_KLEN_MASK			(255)
#define YAC_KEY_VLEN_BITS			(8)
#define YAC_KEY_KLEN(k)				((k).len & YAC_KEY_KLEN_MASK)
#define YAC_KEY_VLEN(k)				((k).len >> YAC_KEY_VLEN_BITS)
#define YAC_KEY_SET_LEN(k, kl, vl)	((k).len = (vl << YAC_KEY_VLEN_BITS) | (kl & YAC_KEY_KLEN_MASK))

typedef struct { 
	unsigned long crc;
	unsigned long atime;
	char data[1];
} yac_kv_val;

/* 64 bytes */
typedef struct {
	unsigned long h;
	unsigned int ttl;
	unsigned int len;
	unsigned int flag;
	unsigned int size;
	yac_kv_val *val;
	unsigned char key[32];
} yac_kv_key;

typedef struct {
	volatile unsigned int pos; 
	unsigned int size;
	void *p;
} yac_shared_segment;

typedef struct {
	unsigned long k_msize;
	unsigned long v_msize;
	unsigned int segments_num;
	unsigned int segment_size;
	unsigned int slots_num;
	unsigned int slots_size;
	unsigned int miss;
	unsigned int fails;
	unsigned int kicks;
	unsigned long hits;
} yac_storage_info;

typedef struct {
	yac_kv_key  *slots;
	unsigned int slots_mask;
	unsigned int slots_num;
	unsigned int slots_size;
	unsigned int miss;
	unsigned int fails;
	unsigned int kicks;
	unsigned long hits;
	yac_shared_segment **segments;
	unsigned int segments_num;
	unsigned int segments_num_mask;
	yac_shared_segment first_seg;
} yac_storage_globals;

extern yac_storage_globals *yac_storage;

#define YAC_SG(element) (yac_storage->element)

int yac_storage_startup(unsigned long first_size, unsigned long size, char **err);
void yac_storage_shutdown(void);
int yac_storage_find(char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag, int *cas);
int yac_storage_update(char *key, unsigned int len, char *data, unsigned int size, unsigned int falg, int ttl);
void yac_storage_delete(char *key, unsigned int len, int ttl);
void yac_storage_flush(void);
const char * yac_storage_shared_memory_name(void);
yac_storage_info * yac_storage_get_info(void);
void yac_storage_free_info(yac_storage_info *info);
#define yac_storage_exists(ht, key, len)  yac_storage_find(ht, key, len, NULL)

#endif	/* YAC_STORAGE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
