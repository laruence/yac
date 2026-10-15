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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <math.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "yac_atomic.h"
#include "crc/yac_crc32.h"
#include "yac_storage.h"
#include "allocator/yac_allocator.h"

yac_storage_globals *yac_storage;

static yac_user_alloc_t user_alloc;
static yac_user_free_t user_free;
#ifdef YAC_DEBUG
#define YAC_DEBUG_PAUSE_AFTER_RESERVE_US 1000u
#endif

static inline unsigned int yac_storage_align_size(unsigned int size) /* {{{ */ {
	int bits = 0;
	while ((size = size >> 1)) {
		++bits;
	}
	return (1 << bits);
}
/* }}} */

int yac_storage_startup(unsigned long fsize, unsigned long size, yac_user_alloc_t alloc, yac_user_free_t free, char **msg) /* {{{ */ {
	unsigned long real_size;

	if (!yac_allocator_startup(fsize, size, msg)) {
		return 0;
	}

	/* setup user memory alloc/free */
	user_alloc = alloc;
	user_free = free;

	/* the CRC chains probe the CPU and mount themselves */
	yac_crc32_startup();

	size = YAC_SG(first_seg).size - ((char *)YAC_SG(keys) - (char *)yac_storage);
	/* rounds down to a power of two, so all slot arrays fit the first segment */
	real_size = yac_storage_align_size(size / (sizeof(yac_kv_key) +
			sizeof(yac_kv_meta) + sizeof(yac_kv_lru)));

	YAC_SG(metas) = (yac_kv_meta *)(YAC_SG(keys) + real_size);
	YAC_SG(lrus) = (yac_kv_lru *)(YAC_SG(metas) + real_size);
	YAC_SG(slots_size) 	= real_size;
	YAC_SG(slots_mask) 	= real_size - 1;
	YAC_SG(stats.fails) = 0;
	YAC_SG(stats.hits)  = 0;
	YAC_SG(stats.miss)  = 0;
	memset(&YAC_SG(stats), 0, sizeof(YAC_SG(stats)));
	YAC_SG(in_flush)    = 0;
	YAC_SG(start_time)  = time(NULL);

	memset(YAC_SG(keys), 0, sizeof(yac_kv_key) * real_size);
	memset(YAC_SG(metas), 0, sizeof(yac_kv_meta) * real_size);
	memset((void *)YAC_SG(lrus), 0, sizeof(yac_kv_lru) * real_size);

	return 1;
}
/* }}} */

void yac_storage_shutdown(void) /* {{{ */ {
	yac_allocator_shutdown();
}
/* }}} */

#ifdef YAC_DEBUG
static void yac_storage_debug_pause_after_reserve(void) {
#if defined(_WIN32)
	Sleep((YAC_DEBUG_PAUSE_AFTER_RESERVE_US + 999) / 1000);
#else
	usleep(YAC_DEBUG_PAUSE_AFTER_RESERVE_US);
#endif
}
#endif

/* {{{ MurmurHash64A (Austin Appleby, public domain).
 *
 * low bits pick the home slot, a fold of the upper half gives the odd probe
 * stride. the empty key hashes to 0, safe because find() rejects empty slots */
static inline uint64_t yac_hash(const char *data, unsigned int len) {
	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	uint64_t h = (uint64_t)len * m;

	while (len >= 8) {
		uint64_t k;

#if SIZEOF_SIZE_T == 8
		/* keys are 8-aligned: zend_string->val and yac_object->prefix both at 24 */
		k = *(uint64_t*)data;
#else
		/* 32-bit only guarantees 4-alignment */
		memcpy(&k, data, 8);
#endif
		k *= m;
		k ^= k >> 47;
		k *= m;

		h ^= k;
		h *= m;

		data += 8;
		len -= 8;
	}

	switch (len) {
	case 7: h ^= (uint64_t)(unsigned char)data[6] << 48;
	case 6: h ^= (uint64_t)(unsigned char)data[5] << 40;
	case 5: h ^= (uint64_t)(unsigned char)data[4] << 32;
	case 4: h ^= (uint64_t)(unsigned char)data[3] << 24;
	case 3: h ^= (uint64_t)(unsigned char)data[2] << 16;
	case 2: h ^= (uint64_t)(unsigned char)data[1] << 8;
	case 1: h ^= (uint64_t)(unsigned char)data[0];
		h *= m;
	}

	h ^= h >> 47;
	h *= m;
	h ^= h >> 47;

	return h;
}
/* }}} */

static inline int yac_slot_snapshot(const YAC_SLOT_V yac_kv_key *p, const YAC_SLOT_V yac_kv_meta *m, yac_kv_key *k, yac_kv_meta *meta) /* {{{ */ {
	unsigned int retry = 0;

	for (;;) {
		unsigned int s2, s1 = YAC_SEQ_LOAD(&m->seq);

		if (!(s1 & 1)) {
			unsigned int w;

			for (w = 0; w < (sizeof(*k) / sizeof(uintptr_t)); w++) {
				uintptr_t word = YAC_LOAD(&((const YAC_SLOT_V uintptr_t *)p)[w]);
				memcpy((char *)k + w * sizeof(uintptr_t), &word, sizeof(word));
			}
			meta->flag = YAC_LOAD(&m->flag);
			meta->crc = YAC_LOAD(&m->crc);
			meta->epoch = YAC_LOAD(&m->epoch);
			YAC_READ_BARRIER();
			s2 = YAC_LOAD(&m->seq);
			if (s1 == s2) {
				meta->seq = s1;
				return 1;
			}
		}
		if (++retry == YAC_MAX_SPIN) {
			return 0;
		}
		yac_cpu_relax();
	}
}
/* }}} */

/* {{{ val-word codecs: all of them live here so the PHP layer never touches bits */
static inline unsigned yac_val_word_kind(uintptr_t word) {
	switch (YAC_VAL_TAG(word)) {
		case YAC_VAL_TAG_LONG:
			return YAC_VALUE_LONG;
		case YAC_VAL_TAG_STR:
			return YAC_VALUE_STRING;
		case YAC_VAL_TAG_SPECIAL:
			return (word & YAC_VAL_DOUBLE_BIT)? YAC_VALUE_DOUBLE : YAC_VALUE_FLAG;
	}
	return YAC_VALUE_MISS;
}

static inline uintptr_t yac_val_str_pack(const char *s, unsigned int len) {
	uintptr_t u = YAC_VAL_TAG_STR | ((uintptr_t)len << 2);
	unsigned int i;

	for (i = 0; i < len; i++) {
		u |= ((uintptr_t)(unsigned char)s[i]) << (5 + i * 8);
	}
	return u;
}

/* the word holds (word bits - 2) signed bits, so a 32-bit build cannot fit
 * every long and the caller must fall back to the byte path */
static inline int yac_val_long_fits(int64_t v) {
	return ((((uint64_t)v) + ((uint64_t)1 << (sizeof(uintptr_t) * 8 - 3)))
			>> (sizeof(uintptr_t) * 8 - 2)) == 0;
}

static inline uintptr_t yac_val_double_zero_pack(double d) {
	return signbit(d)? YAC_VAL_DOUBLE_NEG_ZERO: YAC_VAL_DOUBLE_ZERO;
}
/* }}} */

static inline unsigned yac_val_decode(const yac_kv_key *k, yac_value *out) /* {{{ */ {
	uintptr_t word = (uintptr_t)k->val;
	unsigned int vlen = YAC_KEY_VLEN(*k);

	if (YAC_IS_EMBED_INLINE(word)) {
		uintptr_t packed = YAC_VAL_INLINE_PAYLOAD(word);
		unsigned int kind = YAC_VAL_PACK_KIND(packed);
		const char *src = (const char *)k->key + YAC_KEY_KLEN(*k);

		if (kind == YAC_VALUE_BLOB) {
			char *s = user_alloc(vlen, (unsigned int)packed, 0);

			memcpy(s, src, vlen);
			out->u.blob.val = s;
			out->u.blob.len = vlen;
			out->u.blob.meta = YAC_VAL_PACK_META(packed);
		} else if (kind == YAC_VALUE_STRING) {
			char *s = user_alloc(vlen, (unsigned int)packed, 0);

			memcpy(s, src, vlen);
			out->u.str.val = s;
			out->u.str.len = vlen;
		} else if (kind == YAC_VALUE_LONG) {
			memcpy(&out->u.lval, src, sizeof(int64_t));
		} else {
			memcpy(&out->u.dval, src, sizeof(double));
		}
		return kind;
	}

	switch (YAC_VAL_TAG(word)) {
		case YAC_VAL_TAG_LONG:
			out->u.lval = YAC_VAL_LONG_VALUE(word);
			return YAC_VALUE_LONG;
		case YAC_VAL_TAG_STR:
			out->u.str.val = (char *)word;
			out->u.str.len = YAC_VAL_STR_LEN(word);
			return YAC_VALUE_STR;
		case YAC_VAL_TAG_SPECIAL:
			if (word & YAC_VAL_DOUBLE_BIT) {
				out->u.dval = YAC_VAL_DOUBLE_VALUE(word);
				return YAC_VALUE_DOUBLE;
			}
			out->u.flag = YAC_VAL_FLAG_VALUE(word);
			return YAC_VALUE_FLAG;
	}

	return YAC_VALUE_MISS;
}
/* }}} */

static inline int yac_storage_epoch_safe(const yac_kv_meta *meta, const yac_seg_pos *segpos) /* {{{ */ {
	return segpos->epoch == YAC_EPOCH_VALUE(meta->epoch);
}
/* }}} */

static inline void yac_storage_touch_lru(volatile yac_kv_lru *lru, unsigned long tv) /* {{{ */ {
	if (lru->atime != tv) {
		lru->atime = tv;
	}
	lru->hits += YAC_HITS_PER_SAMPLE;
}
/* }}} */

int yac_storage_find(yac_ctx *ctx, const char *key, unsigned int len, yac_value *out) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	yac_kv_meta meta;
	YAC_SLOT_V yac_kv_key *p;
	YAC_SLOT_V yac_kv_meta *m;
	volatile yac_kv_lru *lru;
	unsigned long tv;

	tv = ctx->tv;
	if (YAC_SG(in_flush)) {
		++ctx->miss;
		return YAC_VALUE_MISS;
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(keys)[h]);
		m = &(YAC_SG(metas)[h]);
		lru = &(YAC_SG(lrus)[h]);
		if (!yac_slot_snapshot(p, m, &k, &meta)) {
			break;
		}
		if (k.val == NULL) {
			break;
		}
		yac_prefetch(&YAC_SG(keys)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (k.ttl) {
				tv = (unsigned long)time(NULL);
				ctx->tv = tv;
				if (k.ttl <= tv) {
					break;
				}
			}
			if (YAC_IS_EMBED(k.val)) {
				unsigned int kind = yac_val_decode(&k, out);

				if (YAC_HITS_SAMPLE(ctx)) {
					yac_storage_touch_lru(lru, tv);
				}
				++ctx->hits;
				return kind;
			} else {
				yac_seg_pos oldpos, newpos;
				unsigned int packed = meta.flag;
				unsigned int kind = YAC_VAL_PACK_KIND(packed);
				unsigned int vlen = YAC_KEY_VLEN(k);
				unsigned int epoch = YAC_EPOCH_VALUE(meta.epoch);
				char *s;
				int heap = (kind == YAC_VALUE_STRING || kind == YAC_VALUE_BLOB);
				int first = 0, second, fast, valid;

				if (!YAC_EPOCH_SAFE(meta.epoch)) {
					first = yac_allocator_resolve(k.val, k.h, &oldpos);
				}
				if (heap) {
					s = user_alloc(vlen, packed, 0);
				} else {
					s = (char *)&out->u;
					vlen = (unsigned int)sizeof(int64_t);
				}

				memcpy(s, k.val->data, vlen);
				YAC_READ_BARRIER();
				second = yac_allocator_resolve(k.val, k.h, &newpos);
				fast = second && YAC_EPOCH_SAFE(meta.epoch) && newpos.epoch == epoch;
				valid = fast || meta.crc == yac_crc32(s, vlen);
				if (valid && YAC_SEQ_LOAD(&m->seq) == meta.seq) {
#ifdef YAC_DEBUG
					if (fast) {
						YAC_ATOMIC_ADD(&YAC_SG(stats.fast_reads), 1);
					} else {
						YAC_ATOMIC_ADD(&YAC_SG(stats.crc_fallbacks), 1);
					}
#endif
					if (!fast && first && second && oldpos.epoch == epoch &&
							newpos.epoch == epoch && newpos.pos > newpos.offset &&
							YAC_SLOT_CLAIM(m)) {
						if (YAC_LOAD(&m->seq) == meta.seq + 1 &&
								YAC_LOAD(&p->val) == k.val &&
								YAC_LOAD(&m->epoch) == meta.epoch) {
							YAC_STORE(&m->epoch, epoch | YAC_EPOCH_SAFE_BIT);
						}
						YAC_SLOT_PUBLISH(m);
					}
					if (kind == YAC_VALUE_BLOB) {
						out->u.blob.val = s;
						out->u.blob.len = YAC_KEY_VLEN(k);
						out->u.blob.meta = YAC_VAL_PACK_META(packed);
					} else if (kind == YAC_VALUE_STRING) {
						out->u.str.val = s;
						out->u.str.len = YAC_KEY_VLEN(k);
					}
					if (YAC_HITS_SAMPLE(ctx)) {
						yac_storage_touch_lru(lru, tv);
					}
					++ctx->hits;
					return kind;
				}
#ifdef YAC_DEBUG
				YAC_ATOMIC_ADD(&YAC_SG(stats.frontier_invalidations), 1);
#endif
				if (heap) {
					user_free(s, packed);
				}
			}
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	++ctx->miss;
	return YAC_VALUE_MISS;
}
/* }}} */

int yac_storage_delete(yac_ctx *ctx, const char *key, unsigned int len, int ttl) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	yac_kv_meta meta;
	YAC_SLOT_V yac_kv_key *p;
	YAC_SLOT_V yac_kv_meta *m;
	unsigned long tv;

	tv = (unsigned long)time(NULL);
	if (YAC_SG(in_flush)) {
		return 0;
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(keys)[h]);
		m = &(YAC_SG(metas)[h]);
		if (!yac_slot_snapshot(p, m, &k, &meta) || k.val == NULL) {
			return 0;
		}
		yac_prefetch(&YAC_SG(keys)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
			if (!YAC_SLOT_CLAIM(m)) {
				return 0;
			}
			if (YAC_LOAD(&m->seq) != meta.seq + 1) {
				YAC_SLOT_PUBLISH(m);
				return 0;
			}
			YAC_STORE(&p->ttl, ttl ? ttl + tv : 1);
			YAC_SLOT_PUBLISH(m);
			return 1;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}
	return 0;
}
/* }}} */

static inline int yac_item_fill(yac_item_list *item, const yac_kv_key *k, const yac_kv_meta *meta, const volatile yac_kv_lru *lru, unsigned int index) /* {{{ */ {
	uintptr_t word = (uintptr_t)k->val;
	yac_seg_pos segpos;

	item->index = index;
	item->h = k->h;
	item->ttl = k->ttl;
	item->k_len = YAC_KEY_KLEN(*k);
	item->val.len = YAC_KEY_VLEN(*k);
	item->val.valword = word;
	item->val.meta = 0;
	item->atime = lru->atime;
	item->hits = lru->hits;
	item->crc = 0;
	item->size = 0;

	if (YAC_IS_EMBED_INLINE(word)) {
		item->kind = YAC_VAL_PACK_KIND(YAC_VAL_INLINE_PAYLOAD(word));
		item->val.meta = YAC_VAL_PACK_META(YAC_VAL_INLINE_PAYLOAD(word));
		item->val.embed = YAC_EMBED_INLINE;
	} else if (YAC_IS_EMBED(word)) {
		item->kind = yac_val_word_kind(word);
		item->val.embed = YAC_EMBED_VALWORD;
	} else {
		if (!yac_allocator_resolve(k->val, k->h, &segpos) ||
				!yac_storage_epoch_safe(meta, &segpos)) {
			return 0;
		}
		item->kind = YAC_VAL_PACK_KIND(meta->flag);
		item->val.meta = YAC_VAL_PACK_META(meta->flag);
		item->val.embed = YAC_EMBED_BLOCK;
		item->crc = meta->crc;
		item->size = k->val->capacity;
	}
	memcpy(item->key, k->key, item->k_len);
	return 1;
}
/* }}} */

int yac_storage_peek(yac_ctx *ctx, const char *key, unsigned int len, yac_item *out) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	yac_kv_meta meta;
	YAC_SLOT_V yac_kv_key *p;
	YAC_SLOT_V yac_kv_meta *m;

	if (YAC_SG(in_flush)) {
		return YAC_VALUE_MISS;
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(keys)[h]);
		m = &(YAC_SG(metas)[h]);
		if (!yac_slot_snapshot(p, m, &k, &meta) || k.val == NULL) {
			return YAC_VALUE_MISS;
		}
		yac_prefetch(&YAC_SG(keys)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
			uintptr_t word = (uintptr_t)k.val;
			unsigned int kind;

			if (k.ttl) {
				unsigned long tv = (unsigned long)time(NULL);
				ctx->tv = tv;
				if (k.ttl <= tv) {
					return YAC_VALUE_MISS;
				}
			}
			out->valword = word;
			out->len = YAC_KEY_VLEN(k);
			out->meta = 0;
			if (YAC_IS_EMBED_INLINE(word)) {
				kind = YAC_VAL_PACK_KIND(YAC_VAL_INLINE_PAYLOAD(word));
				out->meta = YAC_VAL_PACK_META(YAC_VAL_INLINE_PAYLOAD(word));
				out->embed = YAC_EMBED_INLINE;
			} else if (YAC_IS_EMBED(word)) {
				kind = yac_val_word_kind(word);
				out->embed = YAC_EMBED_VALWORD;
			} else {
				kind = YAC_VAL_PACK_KIND(meta.flag);
				out->meta = YAC_VAL_PACK_META(meta.flag);
				out->embed = YAC_EMBED_BLOCK;
			}
			return kind;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}
	return YAC_VALUE_MISS;
}
/* }}} */

static inline int yac_storage_entry_metrics(const yac_kv_key *k, const yac_kv_meta *meta, const volatile yac_kv_lru *lru, unsigned long *atime, unsigned long *hits) /* {{{ */ {
	yac_seg_pos segpos;

	if (!YAC_IS_EMBED(k->val) && (!yac_allocator_resolve(k->val, k->h, &segpos) ||
			!yac_storage_epoch_safe(meta, &segpos))) {
		return 0;
	}
	*atime = lru->atime;
	*hits = lru->hits;
	return 1;
}
/* }}} */

static inline unsigned int yac_storage_pick_victim(const yac_kv_key *keys, const yac_kv_meta *metas, const volatile yac_kv_lru *lrus) /* {{{ */ {
	unsigned long atime, oldest = 0;
	unsigned long hits, least_hits = 0;
	unsigned int victim = 0, i;
	int have_victim = 0;

	for (i = 0; i < 4; i++) {
		if (!yac_storage_entry_metrics(&keys[i], &metas[i], &lrus[i], &atime, &hits)) {
			continue;
		}
		if (!have_victim || atime < oldest || (atime == oldest && hits < least_hits)) {
			oldest = atime;
			least_hits = hits;
			victim = i;
			have_victim = 1;
		}
	}
	return victim;
}
/* }}} */

static inline int yac_storage_fill_value(yac_ctx *ctx, yac_kv_key *k, yac_kv_meta *meta, volatile yac_kv_lru *lru, unsigned int len, const char *data, unsigned int size, unsigned int kind, unsigned int val_meta, uintptr_t word, uint64_t hash, unsigned long tv, yac_alloc_ref *alloc) /* {{{ */ {
	memset(alloc, 0, sizeof(*alloc));
	if (word) {
		if (YAC_IS_EMBED_INLINE(word)) {
			memcpy(k->key + len, data, size);
		}
		k->val = (yac_kv_val *)word;
		meta->flag = 0;
		meta->crc = 0;
		meta->epoch = 0;
		lru->atime = tv;
		lru->hits = 0;
		return 1;
	}

	{
		unsigned int epoch = YAC_EPOCH_VALUE(meta->epoch);
		unsigned int need = sizeof(yac_kv_val) + size - 1;
		int reuse = k->val && !YAC_IS_EMBED(k->val) &&
			yac_allocator_hold(k->val, hash, epoch, alloc);

		if (reuse && k->val->capacity >= need &&
				meta->crc == yac_crc32(k->val->data, YAC_KEY_VLEN(*k))) {
			meta->epoch = epoch;
		} else {
			unsigned int real_size;

			if (reuse) {
				yac_allocator_release(alloc);
			}
			real_size = yac_allocator_real_size(sizeof(yac_kv_val) +
					(size * YAC_STORAGE_FACTOR) - 1);
			if (!real_size || !yac_allocator_alloc(real_size, hash, alloc)) {
				++YAC_SG(stats.fails);
				return 0;
			}
			k->val = alloc->value;
			k->val->capacity = real_size;
			meta->epoch = alloc->epoch;
#ifdef YAC_DEBUG
			YAC_ATOMIC_ADD(&YAC_SG(stats.fresh_allocs), 1);
#endif
		}
	}
	lru->atime = tv;
	lru->hits = 0;
	meta->crc = yac_crc32_snapshot(k->val->data, data, size);
	meta->flag = YAC_VAL_PACK(val_meta, kind);
	return 1;
}
/* }}} */

static int yac_storage_store(yac_ctx *ctx, const char *key, unsigned int len, const char *data, unsigned int size, unsigned int kind, unsigned int val_meta, uintptr_t word, int ttl, int add) /* {{{ */ {
	unsigned int i, w, paths[4];
	uint64_t h, hash, stride;
	yac_kv_key k, snaps[4];
	yac_kv_meta meta, metas[4];
	yac_alloc_ref alloc;
	YAC_SLOT_V yac_kv_key *p;
	YAC_SLOT_V yac_kv_meta *m;
	volatile yac_kv_lru *lru;
	unsigned long tv;

	tv = (unsigned long)time(NULL);
	ctx->tv = tv;
	if (YAC_SG(in_flush)) {
		return 0;
	}

	hash = yac_hash(key, len);
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		paths[i] = h;
		p = &(YAC_SG(keys)[h]);
		m = &(YAC_SG(metas)[h]);
		if (!yac_slot_snapshot(p, m, &snaps[i], &metas[i])) {
			return 0;
		}
		k = snaps[i];
		meta = metas[i];
		if (k.val == NULL) {
			goto do_update;
		}
		yac_prefetch(&YAC_SG(keys)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (add && (!k.ttl || k.ttl > tv)) {
				return 0;
			}
			goto do_update;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	for (i = 0; i < 4; i++) {
		k = snaps[i];
		meta = metas[i];
		if (k.ttl && k.ttl <= tv) {
			h = paths[i];
			goto do_update;
		}
	}

	{
		yac_kv_lru lru_snaps[4];
		for (i = 0; i < 4; i++) {
			lru_snaps[i].atime = YAC_SG(lrus)[paths[i]].atime;
			lru_snaps[i].hits = YAC_SG(lrus)[paths[i]].hits;
		}
		i = yac_storage_pick_victim(snaps, metas, lru_snaps);
	}
	h = paths[i];
	k = snaps[i];
	meta = metas[i];
	++YAC_SG(stats.kicks);

do_update:
	p = &(YAC_SG(keys)[h]);
	m = &(YAC_SG(metas)[h]);
	lru = &(YAC_SG(lrus)[h]);
	if (!YAC_SLOT_CLAIM(m)) {
		return 0;
	}
	if (YAC_LOAD(&m->seq) != meta.seq + 1) {
		YAC_SLOT_PUBLISH(m);
		return 0;
	}
	if (!yac_storage_fill_value(ctx, &k, &meta, lru, len, data, size, kind,
			val_meta, word, hash, tv, &alloc)) {
		YAC_SLOT_PUBLISH(m);
		return 0;
	}
#ifdef YAC_DEBUG
	if (alloc.value) {
		yac_storage_debug_pause_after_reserve();
	}
#endif

	k.h = YAC_HASH_STORE(hash);
	k.ttl = ttl ? tv + ttl : 0;
	memcpy(k.key, key, len);
	YAC_KEY_SET_LEN(k, len, size);
	YAC_STORE(&p->h, k.h);
	YAC_STORE(&p->ttl, k.ttl);
	{
		unsigned int span = YAC_IS_EMBED_INLINE(k.val) ? len + size : len;
		for (w = 0; w < (span + sizeof(uintptr_t) - 1) / sizeof(uintptr_t); w++) {
			uintptr_t key_word;
			memcpy(&key_word, k.key + w * sizeof(uintptr_t), sizeof(key_word));
			YAC_STORE(&((YAC_SLOT_V uintptr_t *)p->key)[w], key_word);
		}
	}
	YAC_STORE(&p->len, k.len);
	YAC_STORE(&m->flag, meta.flag);
	YAC_STORE(&m->crc, meta.crc);
	YAC_STORE(&m->epoch, meta.epoch);
	YAC_STORE(&p->val, k.val);
	YAC_SLOT_PUBLISH(m);
	yac_allocator_release(&alloc);
	return 1;
}
/* }}} */

int yac_storage_set_flag(yac_ctx *ctx, const char *key, unsigned int len, uint8_t flag, int ttl, int add) /* {{{ */ {
	return yac_storage_store(ctx, key, len, NULL, 0, YAC_VALUE_FLAG, 0,
			YAC_VAL_FLAG(flag), ttl, add);
}
/* }}} */

int yac_storage_set_long(yac_ctx *ctx, const char *key, unsigned int len, int64_t v, int ttl, int add) /* {{{ */ {
	if (yac_val_long_fits(v)) {
		return yac_storage_store(ctx, key, len, NULL, 0, YAC_VALUE_LONG, 0,
				YAC_VAL_LONG(v), ttl, add);
	}
	if (YAC_INLINE_FITS(len, sizeof(int64_t))) {
		return yac_storage_store(ctx, key, len, (const char *)&v, sizeof(int64_t),
				YAC_VALUE_LONG, 0, YAC_VAL_INLINE_WORD(0, YAC_VALUE_LONG), ttl, add);
	}
	return yac_storage_store(ctx, key, len, (const char *)&v, sizeof(int64_t),
			YAC_VALUE_LONG, 0, 0, ttl, add);
}
/* }}} */

int yac_storage_set_double(yac_ctx *ctx, const char *key, unsigned int len, double d, int ttl, int add) /* {{{ */ {
	if (d == 0.0) {
		uintptr_t word = yac_val_double_zero_pack(d);
		return yac_storage_store(ctx, key, len, NULL, 0, YAC_VALUE_DOUBLE, 0,
				word, ttl, add);
	}
	if (YAC_INLINE_FITS(len, sizeof(double))) {
		return yac_storage_store(ctx, key, len, (const char *)&d, sizeof(double),
				YAC_VALUE_DOUBLE, 0, YAC_VAL_INLINE_WORD(0, YAC_VALUE_DOUBLE), ttl, add);
	}
	return yac_storage_store(ctx, key, len, (const char *)&d, sizeof(double),
			YAC_VALUE_DOUBLE, 0, 0, ttl, add);
}
/* }}} */

int yac_storage_set_string(yac_ctx *ctx, const char *key, unsigned int len, const char *v, unsigned int vlen, int ttl, int add) /* {{{ */ {
	/* always a raw string, meta 0; compressed strings go through set_blob */
	if (vlen <= YAC_VAL_STR_MAX_LEN) {
		return yac_storage_store(ctx, key, len, NULL, vlen, YAC_VALUE_STRING, 0,
				yac_val_str_pack(v, vlen), ttl, add);
	}
	if (YAC_INLINE_FITS(len, vlen)) {
		return yac_storage_store(ctx, key, len, v, vlen, YAC_VALUE_STRING, 0,
				YAC_VAL_INLINE_WORD(0, YAC_VALUE_STRING), ttl, add);
	}
	return yac_storage_store(ctx, key, len, v, vlen, YAC_VALUE_STRING, 0,
			0, ttl, add);
}
/* }}} */

int yac_storage_set_blob(yac_ctx *ctx, const char *key, unsigned int len, const char *v, unsigned int vlen, unsigned int meta, int ttl, int add) /* {{{ */ {
	/* no val word form: the STR tag has no kind field, so a blob would read
	 * back as a string */
	if (YAC_INLINE_FITS(len, vlen) && meta <= YAC_VAL_INLINE_META_MAX) {
		return yac_storage_store(ctx, key, len, v, vlen, YAC_VALUE_BLOB, meta,
				YAC_VAL_INLINE_WORD(meta, YAC_VALUE_BLOB), ttl, add);
	}
	return yac_storage_store(ctx, key, len, v, vlen, YAC_VALUE_BLOB, meta,
			0, ttl, add);
}
/* }}} */

void yac_storage_flush(void) /* {{{ */ {
	unsigned int i;

	if (!YAC_CAS(&YAC_SG(in_flush), 0, 1)) {
		return;
	}
	for (i = 0; i < YAC_SG(slots_size); i++) {
		while (!YAC_SLOT_CLAIM(&YAC_SG(metas)[i])) {
			yac_cpu_relax();
		}
	}
	memset(YAC_SG(keys), 0, sizeof(yac_kv_key) * YAC_SG(slots_size));
	for (i = 0; i < YAC_SG(slots_size); i++) {
		yac_kv_meta *meta = &YAC_SG(metas)[i];

		YAC_STORE(&meta->flag, 0);
		YAC_STORE(&meta->crc, 0);
		YAC_STORE(&meta->epoch, 0);
		YAC_SG(lrus)[i].atime = 0;
		YAC_SG(lrus)[i].hits = 0;
		YAC_SLOT_PUBLISH(meta);
	}
	YAC_ATOMIC_ADD(&YAC_SG(in_flush), -1);
}
/* }}} */

void yac_storage_commit_stats(yac_ctx *ctx) /* {{{ */ {
	if (ctx->hits) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.hits), ctx->hits);
		ctx->hits = 0;
	}
	if (ctx->miss) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.miss), ctx->miss);
		ctx->miss = 0;
	}
}
/* }}} */

yac_storage_info* yac_storage_get_info(yac_ctx *ctx) /* {{{ */ {
	yac_storage_info *info;
	unsigned int i, occupied = 0;
	unsigned long values_size = 0, recycles = 0, tv;

	tv = (unsigned long)time(NULL);
	info = user_alloc(sizeof(yac_storage_info), 0, 0);
	for (i = 0; i < YAC_SG(segments_num); i++) {
		yac_shared_segment *segment = YAC_SG(segments)[i];
		yac_segment_control *control = yac_allocator_segment_control(segment);
		unsigned int epoch = YAC_LOAD(&control->epoch);

		values_size += segment->size;
		if (epoch) {
			recycles += epoch - 1;
		}
	}
	info->k_msize = (unsigned long)YAC_SG(first_seg).size;
	info->v_msize = values_size;
	info->segment_size = YAC_SG(max_segment_size);
	info->segments_num = YAC_SG(segments_num);
	info->hits = YAC_SG(stats.hits);
	info->miss = YAC_SG(stats.miss);
	info->fails = YAC_SG(stats.fails);
	info->kicks = YAC_SG(stats.kicks);
	info->recycles = (unsigned int)recycles;
#ifdef YAC_DEBUG
	info->fast_reads = YAC_LOAD(&YAC_SG(stats.fast_reads));
	info->crc_fallbacks = YAC_LOAD(&YAC_SG(stats.crc_fallbacks));
	info->frontier_invalidations = YAC_LOAD(&YAC_SG(stats.frontier_invalidations));
	info->fresh_allocs = YAC_LOAD(&YAC_SG(stats.fresh_allocs));
	info->writer_retries = YAC_LOAD(&YAC_SG(stats.writer_retries));
#endif
	info->start_time = YAC_SG(start_time);
	info->slots_size = YAC_SG(slots_size);
	for (i = 0; i < YAC_SG(slots_size); i++) {
		yac_kv_key key;
		yac_kv_meta meta;

		if (yac_slot_snapshot(&YAC_SG(keys)[i], &YAC_SG(metas)[i], &key, &meta) &&
				key.val != NULL && !(key.ttl && key.ttl <= tv)) {
			++occupied;
		}
	}
	info->occupied = occupied;
	return info;
}
/* }}} */

void yac_storage_free_info(yac_storage_info *info) /* {{{ */ {
	user_free(info, 0);
}
/* }}} */

yac_item_list* yac_storage_dump(unsigned int limit, unsigned int offset, unsigned int *num, yac_dump_filter_t filter, void *ctx) /* {{{ */ {
	yac_kv_key k;
	yac_kv_meta meta;
	yac_item_list *item, *list = NULL;
	unsigned int size = YAC_SG(slots_size);
	unsigned int i = 0, n = 0, skipped = 0, max = limit;

	if (YAC_SG(in_flush)) {
		return NULL;
	}
	for (; i < size && n < max; i++) {
		if (!yac_slot_snapshot(&YAC_SG(keys)[i], &YAC_SG(metas)[i], &k, &meta)) {
			continue;
		}
		if (k.val == NULL || (filter && !filter(k.key, YAC_KEY_KLEN(k), ctx))) {
			continue;
		}
		if (skipped < offset) {
			++skipped;
			continue;
		}
		item = user_alloc(offsetof(yac_item_list, key) + YAC_KEY_KLEN(k), 0, 1);
		if (!yac_item_fill(item, &k, &meta, &YAC_SG(lrus)[i], i)) {
			user_free(item, 0);
			continue;
		}
		item->next = list;
		list = item;
		++n;
	}
	*num = n;
	return list;
}
/* }}} */

void yac_storage_free_list(yac_item_list *list) /* {{{ */ {
	yac_item_list *l;
	while (list) {
		l = list;
		list = list->next;
		user_free(l, 0);
	}
}
/* }}} */

const char* yac_storage_shared_memory_name(void) /* {{{ */ {
	return YAC_SHARED_MEMORY_HANDLER_NAME;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
