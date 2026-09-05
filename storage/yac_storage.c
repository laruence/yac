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

void yac_storage_start_stats(void) /* {{{ */ {
	memset(&local_stats, 0, sizeof(local_stats));
}
/* }}} */

void yac_storage_flush_stats(void) /* {{{ */ {
	if (local_stats.hits) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.hits), local_stats.hits);
		local_stats.hits = 0;
	}
	if (local_stats.miss) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.miss), local_stats.miss);
		local_stats.miss = 0;
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
	YAC_SG(stats.occupied) = 0;
	YAC_SG(stats.fails) = 0;
	YAC_SG(stats.hits)  = 0;
	YAC_SG(stats.miss)  = 0;
	YAC_SG(stats.kicks) = 0;
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
		 * offset 24, yac_object prefix at offset 0), a direct load is safe */
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

int yac_storage_find(const char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag, int *cas, unsigned long tv) /* {{{ */ {
	uint64_t h, hash, stride;
	uint32_t i;
	yac_kv_key k, *p;

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(slots)[h]);
		if (!WRITEP(p)) {
			break;
		}
		k = *p;
		READP(p);
		if (k.val == NULL) {
			/* empty slot: insert takes the first empty probe slot,
			 * so the key cannot exist beyond this point */
			break;
		}
		/* the next probe slot's address is already known: pull it in
		 * while this slot's CAS, load and compare are in flight */
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			if (k.ttl && k.ttl <= tv) {
				break; /* expired */
			}
			if (YAC_IS_EMBED(k.val)) {
				/* the value lives in the slot itself, no block to go
				 * stale, so the guarders below don't apply */
				if (p->u2.atime != tv) {
					p->u2.atime = tv;
				}
				*data = (char *)k.val; /* tagged word, YAC_IS_EMBED(data) */
				*size = 0; /* the value word carries no metadata */
				*flag = 0;
				++p->u1.hits;
				++local_stats.hits;
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
					++k.val->hits;
					++local_stats.hits;
					return 1;
				}
				user_free(s, k.u1.flag);
				/* guarders rejected the block: recycled or corrupted
				 * behind our back. tombstone it; re-check val under the
				 * lock — a concurrent writer may have replaced the entry */
				if (WRITEP(p)) {
					if (p->val == k.val) {
						p->ttl = 1;
					}
					READP(p);
				}
			}
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	++local_stats.miss;

	return 0;
}
/* }}} */

int yac_storage_delete(const char *key, unsigned int len, int ttl, unsigned long tv) /* {{{ */ {
	uint64_t h, hash, stride;
	uint32_t i;
	yac_kv_key k, *p;

	hash = yac_hash(key, len);
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		p = &(YAC_SG(slots)[h]);
		if (!WRITEP(p)) {
			return 0;
		}
		k = *p;
		READP(p);
		if (k.val == NULL) {
			return 0; /* the key was never stored */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k, hash) && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
			p->ttl = ttl ? ttl + tv : 1;
			return 1;
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	return 0;
}
/* }}} */

static inline uint32_t yac_storage_pick_victim(yac_kv_key **paths) /* {{{ */ {
	/* evict the least recently used slot of a fully live probe path; ties
	 * fall to the least hit, then the earliest probe — closer to home
	 * means shorter future lookups */
	yac_kv_key c;
	unsigned long atime, oldest;
	uint32_t victim, i;

	victim = 0;
	c = *paths[victim];
	oldest = YAC_KV_ATIME(c);
	for (i = 1; i < 4; i++) {
		c = *paths[i];
		atime = YAC_KV_ATIME(c);
		if (atime < oldest) {
			oldest = atime;
			victim = i;
		} else if (atime == oldest && (YAC_KV_HITS(c) < YAC_KV_HITS(*paths[victim]))) {
			victim = i;
		}
	}

	return victim;
}
/* }}} */

static inline int yac_storage_fill_value(yac_kv_key *k, unsigned int len, char *data, unsigned int size, unsigned int flag, uint64_t hash, unsigned long tv) /* {{{ */ {
	/* fill k with the new value (a block or an embedded word); every
	 * field but h/ttl/key/len is set for the caller to commit, 0 when
	 * no value block could be allocated */
	if (!YAC_IS_EMBED(data)) {
		/* reuse the old block if big enough and intact, otherwise
		 * allocate a fresh one (grown by YAC_STORAGE_FACTOR); the crc
		 * guards against reusing a block the value pool has already
		 * wrapped around and overwritten */
		int has_block = k->val && !YAC_IS_EMBED(k->val);
		if (!(has_block && k->u2.size >= sizeof(yac_kv_val) + size - 1 &&
				k->u2.crc == yac_crc32(k->val->data, YAC_KEY_VLEN(*k)))) {
			unsigned long real_size = yac_allocator_real_size(sizeof(yac_kv_val) + (size * YAC_STORAGE_FACTOR) - 1);
			yac_kv_val *val;

			if (!real_size) {
				++YAC_SG(stats.fails);
				return 0;
			}
			val = yac_allocator_raw_alloc(real_size, (int)hash);
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
		memcpy(k->val->data, data, size);
		k->u2.crc = yac_crc32(data, size);
		k->u1.flag = flag;
	} else {
		/* small scalars live in the tagged word itself, no block */
		k->val = (yac_kv_val *)data;
		k->u2.atime = tv;
		k->u1.hits = 0;
	}
	return 1;
}
/* }}} */

int yac_storage_update(const char *key, unsigned int len, char *data, unsigned int size, unsigned int flag, int ttl, int add, unsigned long tv) /* {{{ */ {
	uint32_t i;
	uint64_t h, hash, stride;
	yac_kv_key k, *p, *paths[4];

	hash = yac_hash(key, len);
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));


	/* 1. walk the key's probe path (up to 4 slots) looking for the key
	 * itself or an empty slot; both can be taken straight away */
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		paths[i] = p = &(YAC_SG(slots)[h]);
		if (!WRITEP(p)) {
			return 0;
		}
		k = *p;
		READP(p);
		if (k.val == NULL) {
			++YAC_SG(stats.occupied); /* this write occupies a new slot */
			goto do_update; /* an insert takes the first empty slot on the path */
		}
		yac_prefetch(&YAC_SG(slots)[(h + stride) & YAC_SG(slots_mask)]);
		if (YAC_HASH_MATCH(k, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
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
		k = *paths[i];
		if (k.ttl && k.ttl <= tv) {
			p = paths[i];
			goto do_update;
		}
	}

	/* 3. every slot on the path holds a live entry: displace the victim
	 * chosen by pick_victim — a kick, the only kind of eviction the
	 * counters track */
	p = paths[yac_storage_pick_victim(paths)];
	if (!WRITEP(p)) {
		return 0;
	}
	k = *p;
	READP(p);
	++YAC_SG(stats.kicks);

do_update:
	/* 4. fill the new value into k; only blocks can go stale (recycled
	 * or corrupted behind our back), embedded values and empty slots are
	 * always intact */
	if (!yac_storage_fill_value(&k, len, data, size, flag, hash, tv)) {
		return 0;
	}

	/* 5. commit under the slot lock. the slot may have been replaced by a
	 * concurrent writer since step 1, so publish the identity fields
	 * unconditionally; per-field writes (not a whole-slot copy) keep a
	 * step-1 snapshot from clobbering the u1/u2 unions */
	k.h = YAC_HASH_STORE(hash);
	k.ttl = ttl ? tv + ttl : 0;
	memcpy(k.key, key, len);
	YAC_KEY_SET_LEN(k, len, size);
	if (!WRITEP(p)) {
		return 0;
	}
	p->h = k.h;
	p->ttl = k.ttl;
	memcpy(p->key, key, len);
	p->len = k.len;
	p->u1 = k.u1;
	p->u2 = k.u2;
	p->val = k.val;
	READP(p);

	return 1;
}
/* }}} */

void yac_storage_flush(void) /* {{{ */ {
	YAC_SG(stats.occupied) = 0;

	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * YAC_SG(slots_size));
}
/* }}} */

yac_storage_info * yac_storage_get_info(void) /* {{{ */ {
	yac_storage_info *info;

	/* fold this process's pending counts so the shared numbers are
	 * accurate; the request keeps accumulating afterwards */
	yac_storage_flush_stats();
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
	info->occupied = YAC_SG(stats.occupied);

	return info;
}
/* }}} */

void yac_storage_free_info(yac_storage_info *info) /* {{{ */ {
	user_free(info, 0);
}
/* }}} */

yac_item_list * yac_storage_dump(unsigned int limit, unsigned int offset, unsigned int *num) /* {{{ */ {
	yac_kv_key k;
	yac_item_list *item, *list = NULL;
	unsigned int size = YAC_SG(slots_size), occupied = YAC_SG(stats.occupied);
	unsigned int i = 0, n = 0, skipped = 0, max = MIN(occupied, limit);

	for (; i < size && n < max; i++) {
		k = YAC_SG(slots)[i];
		if (k.val == NULL) {
			continue;
		}
		if (skipped < offset) {
			++skipped; /* the first offset occupied slots are not reported */
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
			/* no value block: atime and the hit count live in the
			 * slot's u2/u1 unions, crc/size/flag have no meaning */
			item->atime = k.u2.atime;
			item->hits = k.u1.hits;
			item->crc = 0;
			item->size = 0;
			item->flag = 0;
		} else {
			item->atime = k.val->atime;
			item->hits = k.val->hits;
			item->crc = k.u2.crc;
			item->size = k.u2.size;
			item->flag = k.u1.flag;
		}
		memcpy(item->key, k.key, YAC_STORAGE_MAX_KEY_LEN);
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
