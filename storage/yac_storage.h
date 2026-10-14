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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
	unsigned int capacity;
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
			unsigned int epoch;
		};
		unsigned long atime;
	} u2;
	yac_kv_val *val;
	unsigned char key[YAC_STORAGE_MAX_KEY_LEN];
} yac_kv_key;

#if UINTPTR_MAX > 0xffffffffU
typedef char yac_kv_key_must_be_88_bytes[(sizeof(yac_kv_key) == 88) ? 1 : -1];
#endif

/* Embedded entries keep hits/atime in the slot. */
#define YAC_KV_HITS(k)  (YAC_IS_EMBED((k).val) ? (k).u1.hits : (k).val->hits)
#define YAC_KV_ATIME(k) (YAC_IS_EMBED((k).val) ? (k).u2.atime : (k).val->atime)

/* val is an aligned block pointer or a tagged embedded value. */
#define YAC_VAL_TAG_MASK          0x3
#define YAC_VAL_TAG_LONG          0x1
#define YAC_VAL_TAG_STR           0x2
#define YAC_VAL_TAG_SPECIAL       0x3

#define YAC_VAL_INLINE_BIT        0x4
#define YAC_VAL_DOUBLE_BIT        0x8
#define YAC_VAL_PAYLOAD_SHIFT     4

#define YAC_VAL_TAG(p)            ((unsigned)(((uintptr_t)(p)) & YAC_VAL_TAG_MASK))
#define YAC_IS_EMBED(p)           (YAC_VAL_TAG(p) != 0x0)
#define YAC_IS_EMBED_INLINE(p)    (YAC_VAL_TAG(p) == YAC_VAL_TAG_SPECIAL && \
                                   (((uintptr_t)(p)) & YAC_VAL_INLINE_BIT) != 0)
#define YAC_IS_EMBED_DOUBLE(p)    (YAC_VAL_TAG(p) == YAC_VAL_TAG_SPECIAL && \
                                   (((uintptr_t)(p)) & YAC_VAL_DOUBLE_BIT) != 0)

#define YAC_VAL_LONG(v)           ((uintptr_t)((((uint64_t)(v)) << 2) | YAC_VAL_TAG_LONG))
/* Shift at word width to preserve the 32-bit sign. */
#define YAC_VAL_LONG_VALUE(p)     ((int64_t)((intptr_t)(p) >> 2))

#define YAC_VAL_FLAG_MAX          0xff
#define YAC_VAL_FLAG(f)           ((uintptr_t)((((uint64_t)(f)) << YAC_VAL_PAYLOAD_SHIFT) | YAC_VAL_TAG_SPECIAL))
#define YAC_VAL_FLAG_VALUE(p)     ((uint8_t)(((uintptr_t)(p)) >> YAC_VAL_PAYLOAD_SHIFT))

#define YAC_VAL_STR_MAX_LEN       ((unsigned)((sizeof(uintptr_t) * 8 - 5) / 8))
#define YAC_VAL_STR_LEN(p)        ((unsigned)((((uintptr_t)(p)) >> 2) & 0x7))
#define YAC_VAL_STR_DATA(p)       (((uintptr_t)(p)) >> 5)

/* Only +/-0.0 fit in the val word. */
#define YAC_VAL_NEG_ZERO_BIT      0x10
#define YAC_VAL_DOUBLE_ZERO       (YAC_VAL_DOUBLE_BIT | YAC_VAL_TAG_SPECIAL)
#define YAC_VAL_DOUBLE_NEG_ZERO   (YAC_VAL_NEG_ZERO_BIT | YAC_VAL_DOUBLE_ZERO)
#define YAC_VAL_DOUBLE_VALUE(p)   ((((uintptr_t)(p)) & YAC_VAL_NEG_ZERO_BIT)? -0.0: 0.0)

#define YAC_VAL_KIND_BITS         3
#define YAC_VAL_PACK(meta, kind)  ((((uint64_t)(meta)) << YAC_VAL_KIND_BITS) | (kind))
#define YAC_VAL_PACK_KIND(x)      ((unsigned)(((uint64_t)(x)) & 0x7))
#define YAC_VAL_PACK_META(x)      ((unsigned)(((uint64_t)(x)) >> YAC_VAL_KIND_BITS))

#define YAC_VAL_INLINE_WORD(meta, kind) \
	((uintptr_t)((YAC_VAL_PACK(meta, kind) << YAC_VAL_PAYLOAD_SHIFT) | \
	YAC_VAL_INLINE_BIT | YAC_VAL_TAG_SPECIAL))
#define YAC_VAL_INLINE_PAYLOAD(p) (((uintptr_t)(p)) >> YAC_VAL_PAYLOAD_SHIFT)

/* Inline metadata is narrower on 32-bit builds. */
#define YAC_VAL_INLINE_META_BITS \
	((unsigned)(sizeof(uintptr_t) * 8 - YAC_VAL_PAYLOAD_SHIFT - YAC_VAL_KIND_BITS))
#define YAC_VAL_INLINE_META_MAX \
	((YAC_VAL_INLINE_META_BITS >= 32) ? 0xffffffffu : \
	 ((1u << YAC_VAL_INLINE_META_BITS) - 1))

/* the kind peek and find return, and the exposed YAC_KIND_* constants */
#define YAC_VALUE_MISS            0
#define YAC_VALUE_FLAG            1
#define YAC_VALUE_LONG            2
#define YAC_VALUE_DOUBLE          3
#define YAC_VALUE_STR             4
#define YAC_VALUE_STRING          5
#define YAC_VALUE_BLOB            6

/* where the value physically lives */
#define YAC_EMBED_BLOCK           0
#define YAC_EMBED_VALWORD         1
#define YAC_EMBED_INLINE          2

typedef struct {
	union {
		uint8_t  flag;
		int64_t  lval;
		double   dval;
		struct {
			char         *val;
			unsigned int len;
		} str;
		struct {
			char         *val;
			unsigned int len;
			unsigned int meta;
		} blob;
	} u;
} yac_value;

#define YAC_HASH_HOME(hash, mask)    ((hash) & (mask))
/* Odd stride visits distinct power-of-two slots. */
#define YAC_HASH_STRIDE(hash, mask)  (((((hash) >> 32) ^ ((hash) >> 16) ^ (hash)) & (mask)) | 1)
/* The key comparison remains authoritative. */
#define YAC_HASH_STORE(hash)         ((unsigned long)(hash))
#define YAC_HASH_MATCH(h, hash)      ((unsigned long)(hash) == (h))

/* Sample embedded and block entries alike. */
#define YAC_HITS_PER_SAMPLE	3
#define YAC_HITS_SAMPLE(ctx) \
	((((++(ctx)->sample_clock) * 0x9E3779B1u) < (0xFFFFFFFFu / YAC_HITS_PER_SAMPLE)))

#define YAC_INLINE_FITS(len, size) ((len) + (size) <= YAC_STORAGE_MAX_KEY_LEN)

typedef struct _yac_item {
	unsigned char embed;
	unsigned int  len;
	unsigned int  meta;
	uintptr_t     valword;
} yac_item;

typedef struct _yac_item_list {
	unsigned int index;
	unsigned long h;
	unsigned long crc;
	unsigned int k_len;
	unsigned int size;
	unsigned int ttl;
	unsigned long atime;
	unsigned long hits;
	unsigned char kind;
	yac_item val;
	struct _yac_item_list *next;
	unsigned char key[1];
} yac_item_list;

/* Backend mapping descriptor. */
typedef struct {
	unsigned int size;
	void *p;
	unsigned int reserved;
} yac_shared_segment;

/* Value concurrency state lives in the key segment. */
typedef struct {
	volatile unsigned int pos;
	unsigned int epoch;
	unsigned int gate;
	volatile unsigned int writers;
	unsigned char pad[64 - sizeof(unsigned int) * 4];
} yac_segment_control;

typedef char yac_segment_control_must_be_64_bytes[(sizeof(yac_segment_control) == 64) ? 1 : -1];

#define YAC_EPOCH_SAFE_BIT  0x80000000u
#define YAC_EPOCH_VALUE(e)  ((e) & ~YAC_EPOCH_SAFE_BIT)
#define YAC_EPOCH_SAFE(e)   ((e) & YAC_EPOCH_SAFE_BIT)

typedef struct {
	yac_segment_control control;
	yac_shared_segment segment;
	unsigned char pad[64 - sizeof(yac_shared_segment)];
} yac_value_segment;

typedef char yac_value_segment_must_be_two_cache_lines[(sizeof(yac_value_segment) == 128) ? 1 : -1];

typedef struct {
	void *value;
	unsigned int epoch;
	yac_segment_control *control;
} yac_alloc_ref;

typedef struct {
	unsigned int epoch;
	unsigned int pos;
	unsigned int offset;
} yac_seg_pos;

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
#ifdef YAC_DEBUG
	unsigned int fast_reads;
	unsigned int crc_fallbacks;
	unsigned int frontier_invalidations;
	unsigned int fresh_allocs;
	unsigned int writer_retries;
#endif
} yac_storage_info;

typedef struct {
	unsigned int hits;
	unsigned int miss;
	unsigned int kicks;
	unsigned int fails;
#ifdef YAC_DEBUG
	unsigned int fast_reads;
	unsigned int crc_fallbacks;
	unsigned int frontier_invalidations;
	unsigned int fresh_allocs;
	unsigned int writer_retries;
#endif
} yac_storage_stats;

/* per-call context, passed from the caller layer */
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
	unsigned int max_segment_size;
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

int yac_storage_startup(unsigned long first_size, unsigned long size, yac_user_alloc_t alloc, yac_user_free_t free, char **err);
void yac_storage_shutdown(void);
/* returns a YAC_VALUE_* kind (0 = miss), filling only the fields that kind uses */
int yac_storage_find(yac_ctx *ctx, const char *key, unsigned int len, yac_value *out);

/* one setter per storable kind; each decides val-word vs inline vs block and
 * encodes the slot. meta rides along for BLOB only, opaque to storage; STRING
 * is always a raw string with meta 0 */
int yac_storage_set_flag(yac_ctx *ctx, const char *key, unsigned int len, uint8_t flag, int ttl, int add);
int yac_storage_set_long(yac_ctx *ctx, const char *key, unsigned int len, int64_t v, int ttl, int add);
int yac_storage_set_double(yac_ctx *ctx, const char *key, unsigned int len, double d, int ttl, int add);
int yac_storage_set_string(yac_ctx *ctx, const char *key, unsigned int len, const char *v, unsigned int vlen, int ttl, int add);
int yac_storage_set_blob(yac_ctx *ctx, const char *key, unsigned int len, const char *v, unsigned int vlen, unsigned int meta, int ttl, int add);
int yac_storage_delete(yac_ctx *ctx, const char *key, unsigned int len, int ttl);

/* like find but reads no payload: returns the kind and fills valword/embed/len/meta */
int yac_storage_peek(yac_ctx *ctx, const char *key, unsigned int len, yac_item *out);
void yac_storage_flush(void);
/* fold a call context's accumulated hits/miss into the shared stats; the
 * ctx keeps counting afterwards */
void yac_storage_commit_stats(yac_ctx *ctx);
const char* yac_storage_shared_memory_name(void);
yac_storage_info* yac_storage_get_info(yac_ctx *ctx);
void yac_storage_free_info(yac_storage_info *info);
typedef int (*yac_dump_filter_t)(const unsigned char *key, unsigned int k_len, void *ctx);
yac_item_list* yac_storage_dump(unsigned int limit, unsigned int offset, unsigned int *num, yac_dump_filter_t filter, void *ctx);
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
