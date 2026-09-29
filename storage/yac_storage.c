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
#endif

#include "yac_atomic.h"
#include "crc/yac_crc32.h"
#include "yac_storage.h"
#include "allocator/yac_allocator.h"

yac_storage_globals *yac_storage;

static yac_user_alloc_t user_alloc;
static yac_user_free_t user_free;

static inline void yac_ctx_refresh_tv(yac_ctx *ctx) /* {{{ */ {
	unsigned long now = (unsigned long)time(NULL);
	if (ctx->tv != now) {
		ctx->tv = now;
	}
}
/* }}} */

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

	size = YAC_SG(first_seg).size - ((char *)YAC_SG(slots) - (char *)yac_storage);
	/* rounds down to a power of two, so the slot array fits the first segment */
	real_size = yac_storage_align_size(size / sizeof(yac_kv_key));

	YAC_SG(slots_size) 	= real_size;
	YAC_SG(slots_mask) 	= real_size - 1;
	YAC_SG(stats.fails) = 0;
	YAC_SG(stats.hits)  = 0;
	YAC_SG(stats.miss)  = 0;
	YAC_SG(stats.kicks) = 0;
	YAC_SG(in_flush)    = 0;
	YAC_SG(start_time)  = time(NULL);

   	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * real_size);

	return 1;
}
/* }}} */

void yac_storage_shutdown(void) /* {{{ */ {
	yac_allocator_shutdown();
}
/* }}} */

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

static inline int yac_slot_snapshot(const YAC_SLOT_V yac_kv_key *p, yac_kv_key *k) /* {{{ */ {
	/* metadata only: k.val's block can be recycled right after this returns */
	unsigned int retry = 0;

	for (;;) {
		unsigned int s2, s1 = YAC_SEQ_LOAD(&p->seq);

		if (!(s1 & 1)) {
			unsigned int w;

			/* not *k = *p: that may tear; memcpy because punning k would alias */
			for (w = 0; w < (sizeof(yac_kv_key) / sizeof(uintptr_t)); w++) {
				uintptr_t word = YAC_LOAD(&((const YAC_SLOT_V uintptr_t *)p)[w]);
				memcpy((char *)k + w * sizeof(uintptr_t), &word, sizeof(word));
			}

			/* a stale word must not pass a fresh check */
			YAC_READ_BARRIER();
			s2 = YAC_LOAD(&p->seq);
			if (s1 == s2) {
				return 1; /* counter even and unchanged: every word is one version */
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

static inline void yac_val_str_unpack(uintptr_t word, char *dst) {
	uintptr_t payload = YAC_VAL_STR_DATA(word);
	unsigned int i, len = YAC_VAL_STR_LEN(word);

	for (i = 0; i < len; i++) {
		dst[i] = (char)((payload >> (i * 8)) & 0xff);
	}
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
			char *s = user_alloc(vlen, (unsigned int)packed);

			memcpy(s, src, vlen);
			out->u.blob.val = s;
			out->u.blob.len = vlen;
			out->u.blob.meta = YAC_VAL_PACK_META(packed);
		} else if (kind == YAC_VALUE_STRING) {
			char *s = user_alloc(vlen, (unsigned int)packed);

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
		case YAC_VAL_TAG_STR: {
			char *s = user_alloc(vlen, YAC_VAL_PACK(0, YAC_VALUE_STRING));

			yac_val_str_unpack(word, s);
			out->u.str.val = s;
			out->u.str.len = vlen;
			return YAC_VALUE_STRING;
		}
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

int yac_storage_find(yac_ctx *ctx, const char *key, unsigned int len, yac_value *out) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	YAC_SLOT_V yac_kv_key *p;
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		/* report the miss the flush is about to make true anyway */
		++ctx->miss;
		return YAC_VALUE_MISS;
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &k)) {
			break;
		}
		if (k.val == NULL) {
			/* insert takes the first empty probe slot, so the key can't be beyond */
			break;
		}
		/* pull the next probe slot in while this one's compare is in flight */
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (k.ttl && k.ttl <= tv) {
				break; /* expired */
			}
			if (YAC_IS_EMBED(k.val)) {
				/* atime is never sampled; being second-granular it claims at most once/sec */
				int stale = k.u2.atime != tv;
				int sampled = YAC_HITS_SAMPLE(ctx);
				unsigned int kind = yac_val_decode(&k, out);

				/* touching the slot's hits/atime means publishing: the one read path that claims */
				if ((stale || sampled) && YAC_SLOT_CLAIM(p)) {
					/* only bump if no writer replaced the entry under us */
					if ((yac_kv_val *)YAC_LOAD(&p->val) == k.val) {
						if (stale) {
							YAC_STORE(&p->u2.atime, tv);
						}
						if (sampled) {
							YAC_STORE(&p->u1.hits,
									YAC_LOAD(&p->u1.hits) + YAC_HITS_PER_SAMPLE);
						}
					}
					YAC_SLOT_PUBLISH(p);
				}
				++ctx->hits;
				return kind;
			} else {
				/* snapshot the header while live; p->val may change behind our back */
				yac_kv_val v = *(k.val);
				unsigned int packed = k.u1.flag;
				unsigned int kind = YAC_VAL_PACK_KIND(packed);
				unsigned int vlen = YAC_KEY_VLEN(k);
				char *s;
				int heap = (kind == YAC_VALUE_STRING || kind == YAC_VALUE_BLOB);

				if (heap) {
					s = user_alloc(vlen, packed);
				} else {
					/* a scalar is snapshot on the stack; the CRC length is what the
					 * setter used, not sizeof(out->u) (the union is wider) */
					s = (char *)&out->u;
					vlen = (unsigned int)sizeof(int64_t);
				}

				/* reject a block recycled behind our back; crc32_snapshot copies+checks */
				if (k.len == v.len && k.u2.crc == yac_crc32_snapshot(s, (char *)k.val->data, vlen)) {
					if (k.val->atime != tv) {
						k.val->atime = tv;
					}
					if (kind == YAC_VALUE_BLOB) {
						out->u.blob.val = s;
						out->u.blob.len = YAC_KEY_VLEN(k);
						out->u.blob.meta = YAC_VAL_PACK_META(packed);
					} else if (kind == YAC_VALUE_STRING) {
						out->u.str.val = s;
						out->u.str.len = YAC_KEY_VLEN(k);
					}
					/* same sample rate as the embedded form: YAC_KV_HITS compares them */
					if (YAC_HITS_SAMPLE(ctx)) {
						k.val->hits += YAC_HITS_PER_SAMPLE;
					}
					++ctx->hits;
					return kind;
				}
				if (heap) {
					user_free(s, packed);
				}
				/* recycled or corrupted: tombstone, but only if it is still our entry */
				if (YAC_SLOT_CLAIM(p)) {
					if ((yac_kv_val *)YAC_LOAD(&p->val) == k.val) {
						YAC_STORE(&p->ttl, 1);
					}
					YAC_SLOT_PUBLISH(p);
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
	YAC_SLOT_V yac_kv_key *p;
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		return 0; /* the flush removes the key regardless */
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &k)) {
			return 0;
		}
		if (k.val == NULL) {
			return 0; /* the key was never stored */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
			/* outside the counter: a lone ttl store cannot tear a snapshot */
			YAC_STORE(&p->ttl, ttl ? ttl + tv : 1);
			return 1;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	return 0;
}
/* }}} */

static inline void yac_item_fill(yac_item_list *item, const yac_kv_key *k, unsigned int index) /* {{{ */ {
	uintptr_t word = (uintptr_t)k->val;

	item->index = index;
	item->h = k->h;
	item->ttl = k->ttl;
	item->k_len = YAC_KEY_KLEN(*k);
	item->val.len = YAC_KEY_VLEN(*k);
	item->val.valword = word;
	item->val.meta = 0;

	if (YAC_IS_EMBED_INLINE(word)) {
		item->kind = YAC_VAL_PACK_KIND(YAC_VAL_INLINE_PAYLOAD(word));
		item->val.meta = YAC_VAL_PACK_META(YAC_VAL_INLINE_PAYLOAD(word));
		item->val.embed = YAC_EMBED_INLINE;
		item->atime = k->u2.atime;
		item->hits = k->u1.hits;
		item->crc = 0;
		item->size = 0;
	} else if (YAC_IS_EMBED(word)) {
		item->kind = yac_val_word_kind(word);
		item->val.embed = YAC_EMBED_VALWORD;
		item->atime = k->u2.atime;
		item->hits = k->u1.hits;
		item->crc = 0;
		item->size = 0;
	} else {
		item->kind = YAC_VAL_PACK_KIND(k->u1.flag);
		item->val.meta = YAC_VAL_PACK_META(k->u1.flag);
		item->val.embed = YAC_EMBED_BLOCK;
		item->atime = k->val->atime;
		item->hits = k->val->hits;
		item->crc = k->u2.crc;
		item->size = k->u2.size;
	}
	memcpy(item->key, k->key, item->k_len);
}
/* }}} */

int yac_storage_peek(yac_ctx *ctx, const char *key, unsigned int len, yac_item *out) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	YAC_SLOT_V yac_kv_key *p;
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		return YAC_VALUE_MISS;
	}

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &k)) {
			return YAC_VALUE_MISS;
		}
		if (k.val == NULL) {
			return YAC_VALUE_MISS;
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
			uintptr_t word = (uintptr_t)k.val;
			unsigned int kind;

			if (k.ttl && k.ttl <= tv) {
				return YAC_VALUE_MISS;
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
				kind = YAC_VAL_PACK_KIND(k.u1.flag);
				out->meta = YAC_VAL_PACK_META(k.u1.flag);
				out->embed = YAC_EMBED_BLOCK;
			}
			return kind;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	return YAC_VALUE_MISS;
}
/* }}} */

static inline unsigned int yac_storage_pick_victim(const yac_kv_key *snaps) /* {{{ */ {
	/* LRU; ties fall to least-hit then earliest probe. snapshots only */
	unsigned long atime, oldest;
	unsigned int victim, i;

	victim = 0;
	oldest = YAC_KV_ATIME(snaps[victim]);
	for (i = 1; i < 4; i++) {
		atime = YAC_KV_ATIME(snaps[i]);
		if (atime < oldest ||
				(atime == oldest && YAC_KV_HITS(snaps[i]) < YAC_KV_HITS(snaps[victim]))) {
			oldest = atime;
			victim = i;
		}
	}

	return victim;
}
/* }}} */

static inline int yac_storage_fill_value(yac_ctx *ctx, yac_kv_key *k, unsigned int len, const char *data, unsigned int size, unsigned int kind, unsigned int meta, uintptr_t word, uint64_t hash) /* {{{ */ {
	unsigned long tv = ctx->tv;
	/* fill every field but h/ttl/key/len; word=0 means the block path */
	if (word) {
		if (YAC_IS_EMBED_INLINE(word)) {
			memcpy(k->key + len, data, size);
		}
		k->val = (yac_kv_val *)word;
		k->u2.atime = tv;
		k->u1.hits = 0;
		return 1;
	}
	{
		/* reuse the old block if big enough and intact, else allocate fresh */
		int has_block = k->val && !YAC_IS_EMBED(k->val);
		if (!(has_block && k->u2.size >= sizeof(yac_kv_val) + size - 1 &&
				k->u2.crc == yac_crc32(k->val->data, YAC_KEY_VLEN(*k)))) {
			unsigned int real_size = yac_allocator_real_size(sizeof(yac_kv_val) + (size * YAC_STORAGE_FACTOR) - 1);
			yac_kv_val *val;

			if (!real_size) {
				++YAC_SG(stats.fails);
				return 0;
			}

			val = yac_allocator_alloc(real_size, hash);
			if (val == NULL) {
				++YAC_SG(stats.fails);
				return 0;
			}
			k->val = val;
			k->u2.size = real_size;
		}
		k->val->atime = tv;
		k->val->hits = 0; /* every (re)write starts cold */
		YAC_KEY_SET_LEN(*k->val, len, size);
		k->u2.crc = yac_crc32_snapshot(k->val->data, data, size);
		k->u1.flag = YAC_VAL_PACK(meta, kind);
	}
	return 1;
}
/* }}} */

static int yac_storage_store(yac_ctx *ctx, const char *key, unsigned int len, const char *data, unsigned int size, unsigned int kind, unsigned int meta, uintptr_t word, int ttl, int add) /* {{{ */ {
	unsigned int i, w;
	uint64_t h, hash, stride;
	yac_kv_key k, snaps[4];
	YAC_SLOT_V yac_kv_key *p, *paths[4];
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		/* the flush clears this write anyway */
		return 0;
	}

	hash = yac_hash(key, len);
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));

	/* 1. find the key or an empty slot on the probe path */
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		paths[i] = p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &snaps[i])) {
			return 0;
		}
		k = snaps[i];
		if (k.val == NULL) {
			goto do_update; /* first empty slot on the path */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (add && (!k.ttl || k.ttl > tv)) {
				return 0; /* add() must not overwrite a live entry */
			}
			goto do_update;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	/* 2. no empty slot: recycle an expired one for free */
	for (i = 0; i < 4; i++) {
		k = snaps[i];
		if (k.ttl && k.ttl <= tv) {
			p = paths[i];
			goto do_update;
		}
	}

	/* 3. all live: displace a victim (a kick) */
	i = yac_storage_pick_victim(snaps);
	p = paths[i];
	k = snaps[i];
	++YAC_SG(stats.kicks);

do_update:
	/* 4. fill the value; only blocks can go stale */
	if (!yac_storage_fill_value(ctx, &k, len, data, size, kind, meta, word, hash)) {
		return 0;
	}

	/* 5. claim and publish per-field; a whole-slot copy would clobber the unions */
	k.h = YAC_HASH_STORE(hash);
	k.ttl = ttl ? tv + ttl : 0;
	memcpy(k.key, key, len);
	YAC_KEY_SET_LEN(k, len, size);
	if (!YAC_SLOT_CLAIM(p)) {
		return 0;
	}
	YAC_STORE(&p->h, k.h);
	YAC_STORE(&p->ttl, k.ttl);
	/* nothing compares past klen, so a longer predecessor's tail may stay */
	{
		unsigned int span = YAC_IS_EMBED_INLINE(k.val) ? len + size : len;
		for (w = 0; w < (span + sizeof(uintptr_t) - 1) / sizeof(uintptr_t); w++) {
			uintptr_t word;

			memcpy(&word, k.key + w * sizeof(uintptr_t), sizeof(word));
			YAC_STORE(&((YAC_SLOT_V uintptr_t *)p->key)[w], word);
		}
	}
	YAC_STORE(&p->len, k.len);
	YAC_STORE(&p->u1.flag, k.u1.flag);
	YAC_STORE(&p->u2.crc, k.u2.crc);
	YAC_STORE(&p->u2.size, k.u2.size);
	YAC_STORE(&p->val, k.val);
	YAC_SLOT_PUBLISH(p);

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

	if (YAC_SG(in_flush)) {
		return;
	}

	/* writers check this and give up; can't exclude one already past its check */
	YAC_ATOMIC_ADD(&YAC_SG(in_flush), 1);

	/* claim and hold the whole table: odd counters block every writer, so
	 * nothing lands in a slot the memset already passed */
	for (i = 0; i < YAC_SG(slots_size); i++) {
		YAC_SLOT_V yac_kv_key *p = &(YAC_SG(slots)[i]);
		(void)YAC_SLOT_CLAIM(p);
	}

	/* seq zero is even: clears the entries and publishes every slot claimed above */
	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * YAC_SG(slots_size));

	/* full barrier: the memset is plain stores, this publishes them */
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
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	info = user_alloc(sizeof(yac_storage_info), 0);

	info->k_msize = (unsigned long)YAC_SG(first_seg).size;
	info->v_msize = (unsigned long)YAC_SG(segments)[0]->size * (unsigned long)YAC_SG(segments_num);
	info->segment_size = YAC_SG(segments)[0]->size;
	info->segments_num = YAC_SG(segments_num);
	info->hits = YAC_SG(stats.hits);
	info->miss = YAC_SG(stats.miss);
	info->fails = YAC_SG(stats.fails);
	info->kicks = YAC_SG(stats.kicks);
	info->recycles = YAC_SG(stats.recycles);
	info->start_time = YAC_SG(start_time);
	info->slots_size = YAC_SG(slots_size);

	/* no snapshot: occupied is a ballpark, and relaxed loads avoid spinning */
	for (i = 0; i < YAC_SG(slots_size); i++) {
		YAC_SLOT_V yac_kv_key *p = &YAC_SG(slots)[i];
		unsigned int ttl = YAC_LOAD(&p->ttl);

		if (YAC_LOAD(&p->val) != NULL && !(ttl && ttl <= tv)) {
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
	yac_item_list *item, *list = NULL;
	unsigned int size = YAC_SG(slots_size);
	unsigned int i = 0, n = 0, skipped = 0, max = limit;

	if (YAC_SG(in_flush)) {
		return NULL;
	}

	for (; i < size && n < max; i++) {
		if (!yac_slot_snapshot(&YAC_SG(slots)[i], &k)) {
			continue; /* a writer holds it: skip rather than report a tear */
		}
		if (k.val == NULL) {
			continue;
		}
		if (filter && !filter(k.key, YAC_KEY_KLEN(k), ctx)) {
			continue;
		}
		if (skipped < offset) {
			++skipped; /* the first offset matching entries are not reported */
			continue;
		}
		item = user_alloc(offsetof(yac_item_list, key) + YAC_KEY_KLEN(k), 0);
		yac_item_fill(item, &k, i);
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
