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

#include <stdint.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b)? (a) : (b))
#endif

#define YAC_STORAGE_MAX_ENTRY_LEN  	(1 << 20)
#define YAC_STORAGE_MAX_KEY_LEN		(48)
#define YAC_STORAGE_FACTOR 			(1.25)
#define YAC_KEY_KLEN_MASK			(255)
#define YAC_KEY_VLEN_BITS			(8)
#define YAC_KEY_KLEN(k)				((k).len & YAC_KEY_KLEN_MASK)
#define YAC_KEY_VLEN(k)				((k).len >> YAC_KEY_VLEN_BITS)
#define YAC_KEY_SET_LEN(k, kl, vl)	((k).len = (vl << YAC_KEY_VLEN_BITS) | (kl & YAC_KEY_KLEN_MASK))

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
	unsigned int seq; /* even: stable, odd: writing */
	union {
		unsigned int flag;
		unsigned int hits;
	} u1;
	union {
		struct {
			unsigned int crc;
			unsigned int size;
		};
		unsigned long atime;
	} u2;
	yac_kv_val *val;
	unsigned char key[YAC_STORAGE_MAX_KEY_LEN];
} yac_kv_key;

/* hits/atime live in the slot's unions for embedded entries, in the block otherwise */
#define YAC_KV_HITS(k)  (YAC_IS_EMBED((k).val) ? (k).u1.hits : (k).val->hits)
#define YAC_KV_ATIME(k) (YAC_IS_EMBED((k).val) ? (k).u2.atime : (k).val->atime)

#define YAC_EMBED_TAG_MASK          0x3
#define YAC_EMBED_TAG(p)            (((uintptr_t)(p)) & YAC_EMBED_TAG_MASK)
#define YAC_IS_EMBED(p)             (YAC_EMBED_TAG(p) != 0)

#define YAC_EMBED_INLINE_TAG        0x3
#define YAC_EMBED_INLINE_BIT        (((uintptr_t)1) << (sizeof(uintptr_t) * 8 - 1))
#define YAC_EMBED_INLINE_WORD(flag) ((((uintptr_t)(flag)) << 5) | YAC_EMBED_INLINE_BIT | YAC_EMBED_INLINE_TAG)
#define YAC_EMBED_INLINE_FLAG(p)    ((unsigned int)((((uintptr_t)(p)) >> 5) & 0x1f))
#define YAC_IS_EMBED_INLINE(p)      (YAC_EMBED_TAG(p) == YAC_EMBED_INLINE_TAG && \
                                     (((uintptr_t)(p)) & YAC_EMBED_INLINE_BIT) != 0)

#define YAC_HASH_HOME(hash, mask)    ((hash) & (mask))
/* odd, never zero: coprime with the power-of-two slot count, so a probe
 * walk visits distinct slots */
#define YAC_HASH_STRIDE(hash, mask)  (((((hash) >> 32) ^ ((hash) >> 16) ^ (hash)) & (mask)) | 1)
/* slot.h is an unsigned long (32-bit on 32-bit builds): store and
 * compare the low bits explicitly; the key memcmp stays authoritative */
#define YAC_HASH_STORE(hash)         ((unsigned long)(hash))
#define YAC_HASH_MATCH(h, hash)      ((unsigned long)(hash) == (h))

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
	unsigned int embed;
	uintptr_t val;
	unsigned char key[YAC_STORAGE_MAX_KEY_LEN];
	struct _yac_item_list *next;
} yac_item_list;

typedef struct {
	volatile unsigned int pos;
	unsigned int size;
	void *p;
	/* reserved for the allocator implementation, opaque here: whatever the
	 * backend in use needs to keep per segment. it lives in the common
	 * struct so there is a single segment type and sizeof() is the only
	 * stride the allocator ever needs */
	unsigned int reserved;
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
	unsigned int occupied;
	unsigned int slots_size;
	unsigned int fails;
	unsigned int recycles;
} yac_storage_info;

typedef struct {
	unsigned int hits;
	unsigned int miss;
	unsigned int kicks;
	unsigned int fails;
	unsigned int recycles;
} yac_storage_stats;

/* per-call context, passed from the PHP layer; the storage layer uses
 * tv for TTL comparisons, sample_clock for hit-count sampling, and
 * hits/miss are accumulated here and flushed to shared stats by the caller */
typedef struct {
	unsigned int hits;
	unsigned int miss;
	unsigned int sample_clock;
	unsigned long tv;
} yac_ctx;

typedef struct {
	const char *prefix;
	unsigned int prefix_len;
} yac_dump_prefix_ctx;

typedef struct {
	/* read-only after startup */
	yac_kv_key  *slots;
	unsigned int slots_mask;
	unsigned int slots_size;
	unsigned int segments_num;
	unsigned int segments_num_mask;
	unsigned int in_flush;
	unsigned long start_time;
	yac_shared_segment **segments;
	yac_shared_segment first_seg;
	yac_storage_stats stats;
} yac_storage_globals;

extern yac_storage_globals *yac_storage;

/* user side allocator */
typedef void* (*yac_user_alloc_t)(unsigned int size, unsigned int flag, int single);
typedef void (*yac_user_free_t)(void *address, unsigned int flag);

#define YAC_SG(element) (yac_storage->element)

/* CRC chain selection happens inside yac_crc32_startup(): it probes the
 * CPU at runtime (a binary built with -msse4.2 can run on older CPUs) */
int yac_storage_startup(unsigned long first_size, unsigned long size, yac_user_alloc_t alloc, yac_user_free_t free, char **err);
void yac_storage_shutdown(void);
/* data carries either a heap buffer (*data is an efree-able copy) or an
 * embedded value word (test with YAC_IS_EMBED); size is 0 for embeds */
int yac_storage_find(yac_ctx *ctx, const char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag);
/* if YAC_IS_EMBED(data), the tagged word itself is stored instead of
 * allocating a block (size is only kept as the displayed v_len) */
int yac_storage_update(yac_ctx *ctx, const char *key, unsigned int len, char *data, unsigned int size, unsigned int flag, uintptr_t word, int ttl, int add);
int yac_storage_delete(yac_ctx *ctx, const char *key, unsigned int len, int ttl);
/* non-destructive probe: on a live hit, folds the slot into *out (the dump()
 * item form, val word included) and returns 1, else 0. reads no value block
 * and touches no atime/hits, so peeking is not an access the way find() is */
int yac_storage_peek(yac_ctx *ctx, const char *key, unsigned int len, yac_item_list *out);
void yac_storage_flush(void);
/* fold a call context's accumulated hits/miss into the shared stats; the
 * ctx keeps counting afterwards */
void yac_storage_commit_stats(yac_ctx *ctx);
const char * yac_storage_shared_memory_name(void);
/* occupied is counted live from the slots using ctx->tv for the TTL filter,
 * so it needs a context (a zeroed one is fine when there is no object) */
yac_storage_info * yac_storage_get_info(yac_ctx *ctx);
void yac_storage_free_info(yac_storage_info *info);
/* per-entry predicate for yac_storage_dump(): return non-zero to report the
 * entry; called before the entry is copied out, so filtered entries cost
 * nothing. offset/limit count entries that pass the predicate */
typedef int (*yac_dump_filter_t)(const unsigned char *key, unsigned int k_len, void *ctx);
yac_item_list * yac_storage_dump(unsigned int limit, unsigned int offset, unsigned int *num, yac_dump_filter_t filter, void *ctx);
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
