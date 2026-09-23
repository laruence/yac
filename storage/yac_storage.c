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
#include <string.h>
#include <time.h>
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

/* hits counts one read in YAC_HITS_PER_SAMPLE and adds that much, so a hot
 * entry claims its slot a third as often. the 2^32/phi multiplier makes it
 * a Weyl sequence: samples never drift, so a key read at a fixed period is
 * counted like any other. both entry forms must scale alike, YAC_KV_HITS
 * compares them against each other */
#define YAC_HITS_PER_SAMPLE	3
#define YAC_HITS_SAMPLE(ctx) \
	((((++(ctx)->sample_clock) * 0x9E3779B1u) < (0xFFFFFFFFu / YAC_HITS_PER_SAMPLE)))

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

	/* the CRC chains probe the CPU and mount themselves, including the
	 * interleaved large-value path (see yac_crc32_startup) */
	yac_crc32_startup();

	size = YAC_SG(first_seg).size - ((char *)YAC_SG(slots) - (char *)yac_storage);
	/* rounds down to a power of two and never exceeds its input, so the
	 * slot array always fits within the first segment */
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
 * the 64-bit result is split for probing: low bits pick the home slot
 * (YAC_HASH_HOME), a fold of the upper half gives an odd, non-zero probe
 * stride (YAC_HASH_STRIDE) coprime with the slot count, so the walk can't
 * cycle. the empty key hashes to 0, safe because find() rejects empty
 * slots (val == NULL) before comparing hashes */
static inline uint64_t yac_hash(const char *data, unsigned int len) {
	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	uint64_t h = (uint64_t)len * m;

	while (len >= 8) {
		uint64_t k;

#if SIZEOF_SIZE_T == 8
		/* 64-bit builds: keys are 8-aligned (zend_string val sits at struct
		 * offset 24, yac_object prefix at offset 24), a direct load is safe */
		k = *(uint64_t*)data;
#else
		/* 32-bit builds only guarantee 4-alignment */
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
	/* metadata only: k.val's block can be recycled right after this returns,
	 * so find()'s len/crc guards stay mandatory */
	unsigned int retry = 0;

	for (;;) {
		unsigned int s2, s1 = YAC_SEQ_LOAD(&p->seq);

		if (!(s1 & 1)) {
			unsigned int w;

			/* not *k = *p: that may tear or be reordered around the counter
			 * reads. memcpy because punning k's fields would alias */
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

int yac_storage_find(yac_ctx *ctx, const char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag, int *cas) /* {{{ */ {
	uint64_t h, hash, stride;
	unsigned int i;
	yac_kv_key k;
	YAC_SLOT_V yac_kv_key *p;
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		/* the table is being cleared: report the miss the flush is about
		 * to make true anyway, rather than racing the sweep for an entry
		 * that is on its way out */
		++ctx->miss;
		return 0;
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
			/* empty slot: insert takes the first empty probe slot,
			 * so the key cannot exist beyond this point */
			break;
		}
		/* the next probe slot's address is already known: pull it in
		 * while this slot's load and compare are in flight */
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (k.ttl && k.ttl <= tv) {
				break; /* expired */
			}
			if (YAC_IS_EMBED(k.val)) {
				/* atime is never sampled: it picks the victim, hits only
				 * breaks its ties. being second-granular it claims at most
				 * once a second per slot anyway */
				int stale = k.u2.atime != tv;
				int sampled = YAC_HITS_SAMPLE(ctx);

				if (YAC_IS_EMBED_INLINE(k.val)) {
					/* materialize from the snapshot; the seq check
					 * already certified the bytes, no crc guard */
					unsigned int vlen = YAC_KEY_VLEN(k);
					unsigned int tflag = YAC_EMBED_INLINE_FLAG(k.val);
					char *s = user_alloc(vlen, tflag, 0);

					memcpy(s, k.key + YAC_KEY_KLEN(k), vlen);
					*data = s;
					*size = vlen;
					*flag = tflag;
				} else {
					*data = (char *)k.val; /* tagged word, YAC_IS_EMBED(data) */
					*size = 0; /* the value word carries no metadata */
					*flag = 0;
				}
				/* hits/atime live in the slot here, so touching them means
				 * publishing -- the one read path that still claims. best
				 * effort: a busy slot drops the count rather than wait */
				if ((stale || sampled) && YAC_SLOT_CLAIM(p)) {
					/* a writer may have replaced it, and that entry's hit
					 * count is not ours to bump */
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
				return 1;
			} else {
				/* snapshot the header while live — p->val may turn into a
				 * different block (or an embedded word) behind our back */
				yac_kv_val v = *(k.val);
				char *s = user_alloc(YAC_KEY_VLEN(k), k.u1.flag, 0);

				/* guarders: reject a block recycled behind our back.
				 * yac_crc32_snapshot() copies and checksums in one pass */
				if (k.len == v.len && k.u2.crc == yac_crc32_snapshot(s, (char *)k.val->data, YAC_KEY_VLEN(k))) {
					if (k.val->atime != tv) {
						k.val->atime = tv;
					}
					*data = s;
					*size = YAC_KEY_VLEN(k);
					*flag = k.u1.flag;
					/* sampled at the same rate as the embedded form above:
					 * YAC_KV_HITS compares the two against each other */
					if (YAC_HITS_SAMPLE(ctx)) {
						k.val->hits += YAC_HITS_PER_SAMPLE;
					}
					++ctx->hits;
					return 1;
				}
				user_free(s, k.u1.flag);
				/* guarders rejected the block: recycled or corrupted
				 * behind our back. tombstone it, but only if it is still
				 * our entry — killing a writer's replacement loses a
				 * live value */
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

	return 0;
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
			/* outside the counter on purpose: a lone ttl store cannot tear
			 * a snapshot, and at worst this expires a key another writer
			 * just put here, which costs one entry */
			YAC_STORE(&p->ttl, ttl ? ttl + tv : 1);
			return 1;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	return 0;
}
/* }}} */

static inline unsigned int yac_storage_pick_victim(const yac_kv_key *snaps) /* {{{ */ {
	/* evict the least recently used slot of a fully live probe path; ties
	 * fall to the least hit, then the earliest probe — closer to home
	 * means shorter future lookups.
	 *
	 * snapshots only: re-reading a slot here would leave the sequence
	 * protocol and compare two versions of the same slot */
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

static inline int yac_storage_fill_value(yac_ctx *ctx, yac_kv_key *k, unsigned int len, char *data, unsigned int size, unsigned int flag, uintptr_t word, uint64_t hash) /* {{{ */ {
	unsigned long tv = ctx->tv;
	/* fill k with the new value; the caller picked the form and assembled
	 * word (0 = block path). every field but h/ttl/key/len is set for the
	 * caller to commit, 0 when no value block could be allocated */
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
		/* reuse the old block if big enough and intact, otherwise
		 * allocate a fresh one (grown by YAC_STORAGE_FACTOR); the crc
		 * guards against reusing a block the value pool has already
		 * wrapped around and overwritten */
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
		k->u1.flag = flag;
	}
	return 1;
}
/* }}} */

int yac_storage_update(yac_ctx *ctx, const char *key, unsigned int len, char *data, unsigned int size, unsigned int flag, uintptr_t word, int ttl, int add) /* {{{ */ {
	unsigned int i, w;
	uint64_t h, hash, stride;
	yac_kv_key k, snaps[4];
	YAC_SLOT_V yac_kv_key *p, *paths[4];
	unsigned long tv;

	yac_ctx_refresh_tv(ctx);
	tv = ctx->tv;

	if (YAC_SG(in_flush)) {
		/* the flush would clear this write anyway, and a writer racing the
		 * sweep can leave a half-published slot behind it */
		return 0;
	}

	hash = yac_hash(key, len);
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));

	/* 1. walk the key's probe path (up to 4 slots) looking for the key
	 * itself or an empty slot; both can be taken straight away */
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		paths[i] = p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &snaps[i])) {
			return 0;
		}
		k = snaps[i];
		if (k.val == NULL) {
			goto do_update; /* an insert takes the first empty slot on the path */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (add && (!k.ttl || k.ttl > tv)) {
				return 0; /* add() must not overwrite a live entry */
			}
			goto do_update; /* k holds the entry being updated */
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	/* 2. no empty slot: an expired one is recycled for free — natural
	 * TTL expiry or a delete() tombstone, nothing live is lost */
	for (i = 0; i < 4; i++) {
		k = snaps[i];
		if (k.ttl && k.ttl <= tv) {
			p = paths[i];
			goto do_update;
		}
	}

	/* 3. every slot on the path holds a live entry: displace the victim
	 * chosen by pick_victim — a kick, the only kind of eviction the
	 * counters track */
	i = yac_storage_pick_victim(snaps);
	p = paths[i];
	k = snaps[i];
	++YAC_SG(stats.kicks);

do_update:
	/* 4. fill the new value into k; only blocks can go stale (recycled
	 * or corrupted behind our back), embedded values and empty slots are
	 * always intact */
	if (!yac_storage_fill_value(ctx, &k, len, data, size, flag, word, hash)) {
		return 0;
	}

	/* 5. claim the slot and publish. the slot may have been replaced by a
	 * concurrent writer since step 1, so publish the identity fields
	 * unconditionally; per-field writes (not a whole-slot copy) keep a
	 * step-1 snapshot from clobbering the u1/u2 unions */
	k.h = YAC_HASH_STORE(hash);
	k.ttl = ttl ? tv + ttl : 0;
	memcpy(k.key, key, len);
	YAC_KEY_SET_LEN(k, len, size);
	if (!YAC_SLOT_CLAIM(p)) {
		return 0;
	}
	YAC_STORE(&p->h, k.h);
	YAC_STORE(&p->ttl, k.ttl);
	/* the words the key spans, plus an inline value's; nothing compares
	 * past klen, so a longer predecessor's trailing bytes may stay behind */
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

/* an embedded long keeps the value in the top (word bits - 2) bits; the
 * test mirrors yac_long_embedable() but on the stored word width */
static inline int yac_long_stored(intptr_t v) {
	return (((uintptr_t)v + ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 3)))
			>> (sizeof(uintptr_t) * 8 - 2)) == 0;
}

int yac_storage_incr(yac_ctx *ctx, const char *key, unsigned int len, intptr_t step, intptr_t *newval) /* {{{ */ {
	uint64_t hash, stride, h;
	unsigned int i;
	yac_kv_key k;
	YAC_SLOT_V yac_kv_key *p;

	/* read-modify-write on an embedded long; the slot claim window is the
	 * atomic boundary, same last-writer-wins ceiling as update(). the key
	 * must already hold an embedded long (no 0-seeding, no type coercion),
	 * and a step or result that leaves the embedded range is refused with
	 * the value left untouched */
	if (!yac_long_stored(step)) {
		return 0; /* a step this large can never fit the counter */
	}

	yac_ctx_refresh_tv(ctx);

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		uintptr_t word;
		intptr_t v;
		int ok;

		p = &(YAC_SG(slots)[h]);
		if (!yac_slot_snapshot(p, &k)) {
			return 0;
		}
		if (k.val == NULL) {
			break; /* empty probe slot: the key is not stored */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (!(YAC_HASH_MATCH(k.h, hash) && YAC_KEY_KLEN(k) == len &&
				!memcmp(k.key, key, len) &&
				((uintptr_t)k.val & YAC_EMBED_MASK) == YAC_EMBED_LONG)) {
			h = (h + stride) & YAC_SG(slots_mask);
			continue;
		}
		/* the slot holds an embedded long. re-read the live word under the
		 * claim before deciding, so a writer that swapped a block or another
		 * tag in for the long is not clobbered */
		if (!YAC_SLOT_CLAIM(p)) {
			return 0;
		}
		word = (uintptr_t)YAC_LOAD((const YAC_SLOT_V yac_kv_val **)&p->val);
		ok = 0;
		if ((word & YAC_EMBED_MASK) == YAC_EMBED_LONG) {
			intptr_t cur = (intptr_t)word >> 2;

			v = cur + step; /* both operands fit the range, so this cannot wrap */
			if (yac_long_stored(v)) {
				*newval = v;
				YAC_STORE((YAC_SLOT_V yac_kv_val **)&p->val,
						(yac_kv_val *)(((uintptr_t)v << 2) | YAC_EMBED_LONG));
				YAC_STORE(&p->u2.atime, ctx->tv);
				ok = 1;
			}
		}
		YAC_SLOT_PUBLISH(p);
		if (!ok) {
			return 0; /* no live long under the claim, or the sum overflowed */
		}
		return 1;
	}

	return 0;
}
/* }}} */

void yac_storage_flush(void) /* {{{ */ {
	unsigned int i;

	if (YAC_SG(in_flush)) {
		return;
	}

	/* writers check this and give up, so the sweep below races with fewer
	 * of them; it cannot exclude one already past its own check */
	YAC_ATOMIC_ADD(&YAC_SG(in_flush), 1);

	/* claim the whole table and hold it: no writer can commit anywhere while
	 * every counter is odd, so nothing lands in a slot the memset already
	 * passed. one attempt each -- yac_slot_claim() spins YAC_MAX_SPIN
	 * times already, and a claimer that died never publishes, so waiting on
	 * it would hang the flush */
	for (i = 0; i < YAC_SG(slots_size); i++) {
		YAC_SLOT_V yac_kv_key *p = &(YAC_SG(slots)[i]);
		(void)YAC_SLOT_CLAIM(p);
	}

	/* zeroing seq is even, so this both clears the entries and publishes
	 * every slot claimed above, and it frees one wedged by a claimer that
	 * died mid-publish -- the only thing in yac that does */
	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * YAC_SG(slots_size));

	/* full barrier (__sync_fetch_and_add / InterlockedExchangeAdd): the
	 * memset is plain stores, this publishes them */
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

yac_storage_info * yac_storage_get_info(void) /* {{{ */ {
	yac_storage_info *info;
	unsigned int i, occupied = 0;

	info = user_alloc(sizeof(yac_storage_info), 0, 0);

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

	/* count live slots directly: a slot a writer is holding mid-publish
	 * is skipped, which the old counter could not express */
	for (i = 0; i < YAC_SG(slots_size); i++) {
		yac_kv_key k;

		if (yac_slot_snapshot(&YAC_SG(slots)[i], &k) && k.val != NULL) {
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

yac_item_list * yac_storage_dump(unsigned int limit, unsigned int offset, unsigned int *num, yac_dump_filter_t filter, void *ctx) /* {{{ */ {
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
		item = user_alloc(sizeof(yac_item_list), 0, 1);
		item->index = i;
		item->h = k.h;
		item->ttl = k.ttl;
		item->k_len = YAC_KEY_KLEN(k);
		item->v_len = YAC_KEY_VLEN(k);
		item->embedded = YAC_IS_EMBED(k.val) != 0;
		if (item->embedded) {
			/* no value block: atime/hits live in the slot's unions;
			 * only inline entries carry a flag */
			item->atime = k.u2.atime;
			item->hits = k.u1.hits;
			item->crc = 0;
			item->size = 0;
			item->flag = YAC_IS_EMBED_INLINE(k.val) ? YAC_EMBED_INLINE_FLAG(k.val) : 0;
		} else {
			item->atime = k.val->atime;
			item->hits = k.val->hits;
			item->crc = k.u2.crc;
			item->size = k.u2.size;
			item->flag = k.u1.flag;
		}
		memcpy(item->key, k.key, item->k_len);
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

const char * yac_storage_shared_memory_name(void) /* {{{ */ {
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
