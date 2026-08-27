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
#define YAC_STORAGE_MAX_KEY_LEN		(48)
#define YAC_STORAGE_FACTOR 			(1.25)
#define YAC_KEY_KLEN_MASK			(255)
#define YAC_KEY_VLEN_BITS			(8)
#define YAC_KEY_KLEN(k)				((k).len & YAC_KEY_KLEN_MASK)
#define YAC_KEY_VLEN(k)				((k).len >> YAC_KEY_VLEN_BITS)
#define YAC_KEY_SET_LEN(k, kl, vl)	((k).len = (vl << YAC_KEY_VLEN_BITS) | (kl & YAC_KEY_KLEN_MASK))
#define YAC_FULL_CRC_THRESHOLD      256

#define USER_ALLOC					emalloc
#define USER_FREE					efree

typedef struct {
	unsigned int len;
	unsigned int hits;
	unsigned long atime;
	char data[1];
} yac_kv_val;

typedef struct {
	unsigned long h;
	unsigned int len;
	unsigned int ttl;
	unsigned int mutex;
	union {
		/* block values: serializer metadata (type bits +
		 * YAC_ENTRY_COMPRESSED + the original length), which shared
		 * memory cannot hold any other way */
		unsigned int flag;
		/* embedded values: the low 3 bits of val already tell what the
		 * value is and the rest of the word is payload, so the same
		 * bytes keep the find() hit count instead */
		unsigned int hits;
	} u1;
	union {
		struct {
			unsigned int crc;
			unsigned int size;
		};
		/* embedded entries don't use crc/size, atime lives here instead;
		 * must stay the same width as yac_kv_val.atime (unsigned long) */
		unsigned long atime;
	} u2;
	yac_kv_val *val;
	unsigned char key[YAC_STORAGE_MAX_KEY_LEN];
} yac_kv_key;

/* by storage form, an entry keeps its read count and atime either in
 * the slot's u1/u2 unions (embedded, no value block) or in the block
 * itself. Code that already knows the form accesses the union/fields
 * directly; these macros are only for sites where the form is still
 * unknown and must be dispatched on */
#define YAC_KV_HITS(k)  ((k).val == NULL ? 0 : \
	(YAC_IS_EMBED((k).val) ? (k).u1.hits : (k).val->hits))
#define YAC_KV_ATIME(k) (YAC_IS_EMBED((k).val) ? (k).u2.atime : (k).val->atime)

/* Embedded scalar values.
 *
 * val normally points to a block from the bump allocator. Blocks are
 * 8-byte aligned (YAC_SMM_ALIGNMENT), so the low 3 bits of a real
 * pointer are always zero; a non-zero tag there marks an embedded value
 * carried in the word itself, no block allocated. NULL still means
 * "empty slot".
 *
 * tags:
 *   0x1 NULL          (no payload)
 *   0x2 TRUE          (no payload)
 *   0x3 FALSE         (no payload)
 *   0x4 LONG          (zend_long in the high bits)
 *   0x5 SHORT_STR     ([5..3] length 0..YAC_EMBED_STR_MAX_LEN, bytes from bit 6)
 *   0x6 EMPTY_ARRAY   (no payload)
 *   0x7 reserved
 *
 * the zend-type aware helpers live in yac.c; everything here is plain C
 * so the allocator backends can include this header without php.h
 */
#define YAC_EMBED_MASK              0x7

#define YAC_EMBED_NULL              0x1
#define YAC_EMBED_TRUE              0x2
#define YAC_EMBED_FALSE             0x3
#define YAC_EMBED_LONG              0x4
#define YAC_EMBED_STR               0x5
#define YAC_EMBED_EMPTY_ARRAY       0x6

/* applies to both the slot's val and the char *data passed through
 * find()/update(): non-zero low bits mean the value word itself, zero
 * means a real block pointer (or NULL = empty slot) */
#define YAC_IS_EMBED(p)             (((uintptr_t)(p)) & YAC_EMBED_MASK)

/* short strings: up to 7 bytes on 64-bit, 3 bytes on 32-bit;
 * longs fit in (word bits - 3) signed bits: [-2^60, 2^60-1] on 64-bit,
 * [-2^28, 2^28-1] on 32-bit */
#define YAC_EMBED_STR_MAX_LEN       ((unsigned int)((sizeof(void*) * 8 - 6) / 8))
#define YAC_EMBED_STR_LEN(p)        ((unsigned int)((((uintptr_t)(p)) >> 3) & 0x7))
#define YAC_EMBED_STR_DATA(p)       (((uintptr_t)(p)) >> 6)

typedef struct _yac_item_list {
	unsigned int index;
	unsigned long h;
	unsigned long crc;
	unsigned long atime;
	unsigned long hits;
	unsigned int ttl;
	unsigned int k_len;
	unsigned int v_len;
	unsigned int flag;
	unsigned int size;
	unsigned char embedded;
	unsigned char key[YAC_STORAGE_MAX_KEY_LEN];
	struct _yac_item_list *next;
} yac_item_list;

typedef struct {
	volatile unsigned int pos; 
	unsigned int size;
	void *p;
} yac_shared_segment;

typedef struct {
	unsigned long k_msize;
	unsigned long v_msize;
	unsigned long miss;
	unsigned long kicks;
	unsigned long hits;
	unsigned long start_time;
	unsigned int segments_num;
	unsigned int segment_size;
	unsigned int slots_num;
	unsigned int slots_size;
	unsigned int fails;
	unsigned int recycles;
} yac_storage_info;

/* hot-path counters: bumped on every request; each is pinned to its own
 * 64-byte cache line so concurrent increments from multiple workers do
 * not bounce the line between cores. cold counters (write path only,
 * low frequency) stay plain unsigned int and share a line */
#if defined(_MSC_VER)
# define YAC_ALIGNED_TO_CACHELINE __declspec(align(64))
#else
# define YAC_ALIGNED_TO_CACHELINE __attribute__((aligned(64)))
#endif

typedef YAC_ALIGNED_TO_CACHELINE unsigned long yac_hot_counter_t;

typedef struct {
	yac_hot_counter_t hits;
	yac_hot_counter_t miss;
	yac_hot_counter_t kicks;
	/* cold counters: bumped on the write path only */
	unsigned int slots_num;
	unsigned int fails;
	unsigned int recycles;
} yac_storage_stats;

typedef struct {
	/* read-only after startup */
	yac_kv_key  *slots;
	unsigned int slots_mask;
	unsigned int slots_size;
	unsigned int segments_num;
	unsigned int segments_num_mask;
	unsigned long start_time;
	yac_shared_segment **segments;
	yac_shared_segment first_seg;
	yac_storage_stats stats;
} yac_storage_globals;

extern yac_storage_globals *yac_storage;

#define YAC_SG(element) (yac_storage->element)

int yac_storage_startup(unsigned long first_size, unsigned long size, char **err);
void yac_storage_shutdown(void);
/* data carries either a heap buffer (*data is an efree-able copy) or an
 * embedded value word (test with YAC_IS_EMBED); size is 0 for embeds */
int yac_storage_find(const char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag, int *cas, unsigned long tv);
/* if YAC_IS_EMBED(data), the tagged word itself is stored instead of
 * allocating a block (size is only kept as the displayed v_len) */
int yac_storage_update(const char *key, unsigned int len, char *data, unsigned int size, unsigned int flag, int ttl, int add, unsigned long tv);
int yac_storage_delete(const char *key, unsigned int len, int ttl, unsigned long tv);
void yac_storage_flush(void);
const char * yac_storage_shared_memory_name(void);
yac_storage_info * yac_storage_get_info(void);
void yac_storage_free_info(yac_storage_info *info);
yac_item_list * yac_storage_dump(unsigned int limit, unsigned int offset);
void yac_storage_free_list(yac_item_list *list);

#endif	/* YAC_STORAGE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
