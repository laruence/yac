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

/* The slot's val word. bits [1:0] are a tag; a block pointer is 8-aligned, so
 * tag 0 means "real pointer" and the all-zero word means an empty slot.
 *
 *   0  block pointer; kind and meta live in the slot's u1.flag
 *   1  long, sign-extended from bit 2
 *   2  short string, length in bits [4:2], bytes from bit 5
 *   3  special: bit 2 inline (bytes live in the slot's key area), bit 3 double
 *      (only +/-0.0, sign in bit 4), neither means an 8-bit flag in bits [11:4]
 *
 * Byte payloads (block and inline alike) carry YAC_VAL_PACK(meta, kind): a
 * YAC_VALUE_* kind in the low 3 bits, meta above it. meta is opaque here, the
 * PHP layer decides what it means; kind STRING lets the allocator hand back a
 * zend_string buffer the caller can adopt. A long too wide for the val word
 * (32-bit builds) and any non-zero double take the byte path too, hence kind
 * covers all four.
 */
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
/* the shift must happen at the word width, where the value is signed: going
 * through uint64 first would zero-extend and lose the sign on a 32-bit build */
#define YAC_VAL_LONG_VALUE(p)     ((int64_t)((intptr_t)(p) >> 2))

#define YAC_VAL_FLAG_MAX          0xff
#define YAC_VAL_FLAG(f)           ((uintptr_t)((((uint64_t)(f)) << YAC_VAL_PAYLOAD_SHIFT) | YAC_VAL_TAG_SPECIAL))
#define YAC_VAL_FLAG_VALUE(p)     ((uint8_t)(((uintptr_t)(p)) >> YAC_VAL_PAYLOAD_SHIFT))

#define YAC_VAL_STR_MAX_LEN       ((unsigned)((sizeof(uintptr_t) * 8 - 5) / 8))
#define YAC_VAL_STR_LEN(p)        ((unsigned)((((uintptr_t)(p)) >> 2) & 0x7))
#define YAC_VAL_STR_DATA(p)       (((uintptr_t)(p)) >> 5)

/* A double never fits the val word except as a zero: only +/-0.0 rides it,
 * one sign bit. that is enough because every falsy double is a zero, and
 * empty() must be answerable from the word alone */
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

/* the inline word's payload area is narrow on a 32-bit build (25 meta bits),
 * so a setter must check meta against this before choosing the inline form */
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
#define YAC_VALUE_STRING          4
#define YAC_VALUE_BLOB            5

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
/* odd, never zero: coprime with the power-of-two slot count, so a probe
 * walk visits distinct slots */
#define YAC_HASH_STRIDE(hash, mask)  (((((hash) >> 32) ^ ((hash) >> 16) ^ (hash)) & (mask)) | 1)
/* slot.h is an unsigned long (32-bit on 32-bit builds): store and
 * compare the low bits explicitly; the key memcmp stays authoritative */
#define YAC_HASH_STORE(hash)         ((unsigned long)(hash))
#define YAC_HASH_MATCH(h, hash)      ((unsigned long)(hash) == (h))

/* Weyl sequence (2^32/phi): samples evenly, and both entry forms must scale alike */
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

typedef struct {
	volatile unsigned int pos;
	unsigned int size;
	void *p;
	/* reserved for the allocator implementation */
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
	unsigned int in_flush;
	unsigned long start_time;
	yac_shared_segment **segments;
	yac_shared_segment first_seg;
	yac_storage_stats stats;
} yac_storage_globals;

extern yac_storage_globals *yac_storage;

/* user side allocator */
typedef void* (*yac_user_alloc_t)(unsigned int size, unsigned int flag);
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
