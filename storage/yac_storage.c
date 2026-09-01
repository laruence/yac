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

#include "php.h"

#include "yac_atomic.h"
#include "yac_crc32.h"
#include "yac_storage.h"
#include "allocator/yac_allocator.h"

yac_storage_globals *yac_storage;

/* per-process stats accumulators: hits/miss land here on the hot path
 * instead of the shared stats line, and are folded back atomically at
 * request shutdown (or before stats are read). bumping the shared line
 * on every hit made it bounce between cores constantly — and in the
 * compact layout that line also carries the read-only globals — which
 * cost roughly a third of aggregate throughput. the cold counters
 * (kicks/fails/occupied/recycles) keep their direct increments */
static unsigned long yac_stats_hits_acc;
static unsigned long yac_stats_miss_acc;

void yac_storage_stats_flush(void) {
	if (yac_stats_hits_acc) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.hits), (unsigned int)yac_stats_hits_acc);
		yac_stats_hits_acc = 0;
	}
	if (yac_stats_miss_acc) {
		YAC_ATOMIC_ADD(&YAC_SG(stats.miss), (unsigned int)yac_stats_miss_acc);
		yac_stats_miss_acc = 0;
	}
}

void yac_storage_stats_reset(void) {
	yac_stats_hits_acc = 0;
	yac_stats_miss_acc = 0;
}

static uint32_t (*yac_crc)(const char *data, unsigned int size);
static uint32_t (*yac_crc_interleaved)(const char *data, unsigned int size);
static uint32_t crc32(const char *data, unsigned int size);

#if HAVE_SSE_CRC32 /* {{{ */
static uint32_t crc32c_sse42(const char *buf, unsigned int size) {
	uint32_t crc = 0 ^ 0xFFFFFFFF;

	/* byte-wise until 8-aligned, so the word loads below stay aligned */
	while (size && ((uintptr_t)buf & 7)) {
		crc = _mm_crc32_u8(crc, (unsigned char)*buf++);
		size--;
	}
#if __x86_64__
	while (size >= sizeof(uint64_t)) {
		crc = _mm_crc32_u64(crc, *(uint64_t*)buf);
		buf += sizeof(uint64_t);
		size -= sizeof(uint64_t);
	}
#endif
	while (size >= sizeof(uint32_t)) {
		crc = _mm_crc32_u32(crc, *(uint32_t*)buf);
		buf += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}
	if (size >= sizeof(uint16_t)) {
		crc = _mm_crc32_u16(crc, *(uint16_t*)buf);
		buf += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}
	if (size) {
		crc = _mm_crc32_u8(crc, (unsigned char)*buf);
	}

	return crc ^ 0xFFFFFFFF;
}
#endif
/* }}} */

#if HAVE_ARM_CRC32 /* {{{ */
static uint32_t crc32c_arm(const char *buf, unsigned int size) {
	uint32_t crc = 0 ^ 0xFFFFFFFF;

	/* byte-wise until 8-aligned, so the word loads below stay aligned */
	while (size && ((uintptr_t)buf & 7)) {
		crc = __crc32cb(crc, (unsigned char)*buf++);
		size--;
	}
	while (size >= sizeof(uint64_t)) {
		crc = __crc32cd(crc, *(uint64_t*)buf);
		buf += sizeof(uint64_t);
		size -= sizeof(uint64_t);
	}
	while (size >= sizeof(uint32_t)) {
		crc = __crc32cw(crc, *(uint32_t*)buf);
		buf += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}
	if (size >= sizeof(uint16_t)) {
		crc = __crc32ch(crc, *(uint16_t*)buf);
		buf += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}
	if (size) {
		crc = __crc32cb(crc, (unsigned char)*buf);
	}

	return crc ^ 0xFFFFFFFF;
}
#endif
/* }}} */

#if YAC_HAVE_CRC_WORD /* {{{ */
/* CRC-32C of a concatenation reassembled from part CRCs:
 *   crc(A || B) = crc(A) advanced by 8*|B| bits  XOR  crc(B)
 * advancing by 8*|B| bits is multiplication by x^(8*|B|) in GF(2) mod P,
 * i.e. appending |B| zero bytes to the state. the T^(2^i) operators
 * ("append 2^i zero bytes") are tabulated byte-wise, so applying T^m
 * walks the set bits of m at four loads per bit — the reassembly cost is
 * flat and tiny regardless of the block length's popcount. blocks never
 * exceed YAC_STORAGE_MAX_ENTRY_LEN/3 bytes, so twenty levels of doubling
 * cover every possible m */
#define YAC_CRC_XPOW_MAXLEV 20
static uint32_t yac_crc_xpow_tab[YAC_CRC_XPOW_MAXLEV][4][256];

/* append one zero byte to a CRC state: eight reflected single-bit steps */
static uint32_t yac_crc_append_byte(uint32_t c) {
	int i;

	for (i = 0; i < 8; i++) {
		c = (c >> 1) ^ (YAC_CRC_POLY & (uint32_t)(0 - (c & 1)));
	}
	return c;
}

/* apply the T^(2^lev) operator: four independent table lookups, one per
 * state byte, folded together by xor */
static inline uint32_t yac_crc_apply_xpow(uint32_t c, int lev) {
	return yac_crc_xpow_tab[lev][0][c & 0xFF]
	     ^ yac_crc_xpow_tab[lev][1][(c >> 8) & 0xFF]
	     ^ yac_crc_xpow_tab[lev][2][(c >> 16) & 0xFF]
	     ^ yac_crc_xpow_tab[lev][3][c >> 24];
}

static void yac_crc32c_init(void) {
	int lev, j, v;
	uint32_t x;

	for (j = 0; j < 4; j++) {
		for (v = 0; v < 256; v++) {
			yac_crc_xpow_tab[0][j][v] = yac_crc_append_byte((uint32_t)v << (8 * j));
		}
	}
	/* each higher level is the previous one applied twice: T^(2^i) ∘ T^(2^i) */
	for (lev = 1; lev < YAC_CRC_XPOW_MAXLEV; lev++) {
		for (j = 0; j < 4; j++) {
			for (v = 0; v < 256; v++) {
				x = yac_crc_apply_xpow((uint32_t)v << (8 * j), lev - 1);
				yac_crc_xpow_tab[lev][j][v] = yac_crc_apply_xpow(x, lev - 1);
			}
		}
	}
}

static uint32_t yac_crc32_combine(uint32_t crc1, uint32_t crc2, unsigned int len2) {
	int lev = 0;

	while (len2) {
		if (len2 & 1) {
			crc1 = yac_crc_apply_xpow(crc1, lev);
		}
		len2 >>= 1;
		lev++;
	}
	return crc1 ^ crc2;
}

/* split the buffer into three consecutive blocks and advance three
 * independent CRC chains in lockstep: every iteration issues three
 * mutually independent hardware CRC instructions, so the ~3-cycle
 * latency of one chain hides behind the other two.  the caller only
 * routes buffers >= YAC_CRC_INTER_THRESHOLD here, because below
 * that the reassembly costs more than the interleaving saves */
static uint32_t yac_crc32c_interleaved(const char *buf, unsigned int size) {
	uint32_t h, c0, c1, c2;
	const char *p0, *p1, *p2;
	unsigned int block, rounds;

	/* the block pointers all share buf's alignment class: peel an unaligned
	 * head on a serial chain so the word loads below stay aligned. callers
	 * only route buffers >= YAC_CRC_INTER_THRESHOLD here, so the peel cannot
	 * run past the end */
	h = 0xFFFFFFFFu;
	while ((uintptr_t)buf & 7) {
		h = YAC_CRC_BYTE(h, (unsigned char)*buf++);
		size--;
	}

	block = (size / 3) & ~7u; /* word-aligned blocks */
	p0 = buf;
	p1 = buf + block;
	p2 = buf + block * 2;
	rounds = block >> 3;

	c0 = c1 = c2 = 0xFFFFFFFFu;
	while (rounds--) {
		c0 = YAC_CRC_WORD(c0, *(uint64_t*)p0);
		c1 = YAC_CRC_WORD(c1, *(uint64_t*)p1);
		c2 = YAC_CRC_WORD(c2, *(uint64_t*)p2);
		p0 += sizeof(uint64_t);
		p1 += sizeof(uint64_t);
		p2 += sizeof(uint64_t);
	}

	c0 = yac_crc32_combine(h ^ 0xFFFFFFFFu, c0 ^ 0xFFFFFFFFu, block);
	c0 = yac_crc32_combine(c0, c1 ^ 0xFFFFFFFFu, block);
	c0 = yac_crc32_combine(c0, c2 ^ 0xFFFFFFFFu, block);
	if (size > block * 3) {
		/* at most 23 trailing bytes, covered serially */
		c0 = yac_crc32_combine(c0, yac_crc(buf + block * 3, size - block * 3), size - block * 3);
	}
	return c0;
}
#endif
/* }}} */

static inline unsigned int yac_storage_align_size(unsigned int size) /* {{{ */ {
	int bits = 0;
	while ((size = size >> 1)) {
		++bits;
	}
	return (1 << bits);
}
/* }}} */

int yac_storage_startup(unsigned long fsize, unsigned long size, char **msg) /* {{{ */ {
	unsigned long real_size;

	if (!yac_allocator_startup(fsize, size, msg)) {
		return 0;
	}
	/* pick the CRC chain: hardware when this CPU actually has the
	 * instructions (a binary built where the compiler supports them can
	 * run on older CPUs that do not), software otherwise. the
	 * interleaved large-value path uses the same hardware word ops, so
	 * it must only be mounted when the hardware chain won — routing it
	 * from compile-time macros would crash with SIGILL on such CPUs */
#if HAVE_SSE_CRC32
	if (zend_cpu_supports_sse42()) {
		yac_crc = crc32c_sse42;
# if YAC_HAVE_CRC_WORD
		yac_crc32c_init();
		yac_crc_interleaved = yac_crc32c_interleaved;
# endif
	} else
#endif
	{
#if HAVE_ARM_CRC32
		yac_crc = crc32c_arm;
		yac_crc32c_init();
		yac_crc_interleaved = yac_crc32c_interleaved;
#else
		yac_crc = crc32;
#endif
	}
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
 * one pass over the key; the 64-bit result is split for the probe
 * scheme: the low bits pick the home slot (YAC_HASH_HOME), a fold of the
 * upper half gives the probe stride (YAC_HASH_STRIDE) — odd, so coprime
 * with the power-of-two slot count and never zero, hence the probe walk
 * always advances and cannot cycle within a single slot. keys are bounded
 * by YAC_STORAGE_MAX_KEY_LEN (48 bytes): at most six 8-byte rounds. the
 * empty key hashes to 0, which is safe because find() rejects empty
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

/* {{{ Software fallback CRC-32C (Castagnoli, reflected, poly 0x82F63B78 —
 * the same polynomial the hardware _mm_crc32/__crc32c instructions compute,
 * so a software fallback on a given machine matches its hardware CRC bit for
 * bit, and entries stay valid across the dispatch choice).
 *
 * Slicing-by-8: one loop iteration consumes 8 bytes — xor the next 4 input
 * bytes into the running crc to clear it, then 8 table lookups fold all 8
 * bytes in at once. Roughly 4-5x the throughput of a single-table byte loop
 * for a 8 KiB table footprint. Only reached on CPUs without hardware CRC-32C
 * (see yac_storage_startup). Tables generated offline and cross-checked
 * against the hardware instructions over all lengths and alignments;
 * "123456789" -> 0xe3069283 (RFC 3720 B.4).
 */
static uint32_t crc32(const char *buf, unsigned int size) {
	const unsigned char *p = (const unsigned char *)buf;
	uint32_t crc = 0xFFFFFFFFu;

	/* byte-wise until 8-aligned so the fast loop can read full words */
	while (size && ((uintptr_t)p & 7)) {
		crc = yac_crc32c_tab[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
		size--;
	}
	while (size >= 8) {
		uint64_t w;

		memcpy(&w, p, sizeof(uint64_t));
		crc ^= (uint32_t)w;
		crc = yac_crc32c_tab[7][crc & 0xFF]
			^ yac_crc32c_tab[6][(crc >> 8) & 0xFF]
			^ yac_crc32c_tab[5][(crc >> 16) & 0xFF]
			^ yac_crc32c_tab[4][(crc >> 24) & 0xFF]
			^ yac_crc32c_tab[3][(w >> 32) & 0xFF]
			^ yac_crc32c_tab[2][(w >> 40) & 0xFF]
			^ yac_crc32c_tab[1][(w >> 48) & 0xFF]
			^ yac_crc32c_tab[0][(w >> 56) & 0xFF];
		p += 8;
		size -= 8;
	}
	while (size--) {
		crc = yac_crc32c_tab[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFF;
}
/* }}} */

static inline unsigned int yac_crc32(char *data, unsigned int size) /* {{{ */ {
	/* CRC-32C over the whole value: the serial chain for small ones, and
	 * three interleaved hardware chains for large ones (NULL when the build
	 * has no hardware CRC at all) */
	if (yac_crc_interleaved == NULL || size < YAC_CRC_INTER_THRESHOLD) {
		return yac_crc(data, size);
	}
	return yac_crc_interleaved(data, size);
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
				++yac_stats_hits_acc;
				return 1;
			} else {
				yac_kv_val v = *(k.val);
				char *s = USER_ALLOC(YAC_KEY_VLEN(k));

				memcpy(s, (char *)k.val->data, YAC_KEY_VLEN(k));
				/* guarders: reject a block recycled behind our back */
				if (k.len == v.len && k.u2.crc == yac_crc32(s, YAC_KEY_VLEN(k))) {
					if (k.val->atime != tv) {
						k.val->atime = tv;
					}
					*data = s;
					*size = YAC_KEY_VLEN(k);
					*flag = k.u1.flag;
					++k.val->hits;
					++yac_stats_hits_acc;
					return 1;
				}
				USER_FREE(s);
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

	++yac_stats_miss_acc;

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
	/* pick the eviction victim from a probe path whose 4 slots are all
	 * live: the least recently used one; ties fall to the least hit
	 * candidate, then the earliest probe — the evicted slot is inherited
	 * by the new key, and the closer it sits to the home slot the shorter
	 * every future lookup of the new key */
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
	/* make k ready to carry the new value: block values go into a reused or
	 * freshly allocated block, embedded values live in the tagged word
	 * itself; every field but h/ttl/key/len is filled for the caller to
	 * commit. returns 0 when no value block could be allocated */
	if (!YAC_IS_EMBED(data)) {
		/* reuse the old block if intact and big enough, otherwise
		 * allocate a fresh one (grown by YAC_STORAGE_FACTOR). the crc
		 * check guards against a block the value pool has already
		 * wrapped around and overwritten — reusing such a block would
		 * clobber its new owner's data */
		int intact = k->val && !YAC_IS_EMBED(k->val) &&
			k->u2.crc == yac_crc32(k->val->data, YAC_KEY_VLEN(*k));
		if (!(intact && k->u2.size >= sizeof(yac_kv_val) + size - 1)) {
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

	/* 5. commit under the slot lock. the slot may no longer be ours — a
	 * concurrent writer can have evicted and replaced it between the
	 * probe and this point, so the identity fields are published
	 * unconditionally. update the fields individually instead of copying
	 * the whole slot: the u1/u2 unions alias (flag|hits, crc/size|atime)
	 * depending on the storage form, and a whole-slot copy would
	 * re-publish the stale union bytes grabbed in step 1 over lock-free
	 * statistics updates or a concurrent writer's freshly written
	 * crc/size */
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

	/* include this process's pending counts before reading */
	yac_storage_stats_flush();
	info = USER_ALLOC(sizeof(yac_storage_info));

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
	USER_FREE(info);
}
/* }}} */

yac_item_list * yac_storage_dump(unsigned int limit, unsigned int offset) /* {{{ */ {
	yac_kv_key k;
	yac_item_list *item, *list = NULL;
	unsigned int i = 0, n = 0, skipped = 0;

	if (YAC_SG(stats.occupied) == 0) {
		return NULL;
	}
	for (; i < YAC_SG(slots_size) && n < YAC_SG(stats.occupied) && n < limit; i++) {
		k = YAC_SG(slots)[i];
		if (k.val == NULL) {
			continue;
		}
		if (skipped < offset) {
			++skipped; /* the first offset occupied slots are not reported */
			continue;
		}
		item = USER_ALLOC(sizeof(yac_item_list));
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

	return list;
}
/* }}} */

void yac_storage_free_list(yac_item_list *list) /* {{{ */ {
	yac_item_list *l;
	while (list) {
		l = list;
		list = list->next;
		USER_FREE(l);
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
