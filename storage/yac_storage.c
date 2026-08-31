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

#if HAVE_SSE_CRC32
#include "Zend/zend_cpuinfo.h"
#include <nmmintrin.h>
static uint32_t crc32c_sse42(const char *dagta, unsigned int size);
#endif

#if HAVE_ARM_CRC32
#include <arm_acle.h>
static uint32_t crc32c_arm(const char *data, unsigned int size);
#endif

#include "yac_atomic.h"
#include "yac_storage.h"
#include "allocator/yac_allocator.h"

yac_storage_globals *yac_storage;

static uint32_t (*yac_crc)(const char *data, unsigned int size);
static uint32_t crc32(const char *dagta, unsigned int size);

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
#if HAVE_SSE_CRC32
	if (zend_cpu_supports_sse42()) {
		yac_crc = crc32c_sse42;
	} else
#endif
#if HAVE_ARM_CRC32
	{
		yac_crc = crc32c_arm;
	}
#else
	{
		yac_crc = crc32;
	}
#endif
	size = YAC_SG(first_seg).size - ((char *)YAC_SG(slots) - (char *)yac_storage);
	/* rounds down to a power of two and never exceeds its input, so the
	 * slot array always fits within the first segment */
	real_size = yac_storage_align_size(size / sizeof(yac_kv_key));

	YAC_SG(slots_size) 	= real_size;
	YAC_SG(slots_mask) 	= real_size - 1;
	YAC_SG(stats.occupied) = 0;
	YAC_SG(stats.fails)     = 0;
	YAC_SG(stats.hits)      = 0;
	YAC_SG(stats.miss)      = 0;
	YAC_SG(stats.kicks)     = 0;
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

		memcpy(&k, data, 8); /* keys may be unaligned */
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

/* {{{  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or
 *  code or tables extracted from it, as desired without restriction.
 *
 *  First, the polynomial itself and its table of feedback terms.  The
 *  polynomial is
 *  X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X^1+X^0
 *
 *  Note that we take it "backwards" and put the highest-order term in
 *  the lowest-order bit.  The X^32 term is "implied"; the LSB is the
 *  X^31 term, etc.  The X^0 term (usually shown as "+1") results in
 *  the MSB being 1
 *
 *  Note that the usual hardware shift register implementation, which
 *  is what we're using (we're merely optimizing it by doing eight-bit
 *  chunks at a time) shifts bits into the lowest-order term.  In our
 *  implementation, that means shifting towards the right.  Why do we
 *  do it this way?  Because the calculated CRC must be transmitted in
 *  order from highest-order term to lowest-order term.  UARTs transmit
 *  characters in order from LSB to MSB.  By storing the CRC this way
 *  we hand it to the UART in the order low-byte to high-byte; the UART
 *  sends each low-bit to high-bit; and the result is transmission bit
 *  by bit from highest- to lowest-order term without requiring any bit
 *  shuffling on our part.  Reception works similarly
 *
 *  The feedback terms table consists of 256, 32-bit entries.  Notes
 *
 *      The table can be generated at runtime if desired; code to do so
 *      is shown later.  It might not be obvious, but the feedback
 *      terms simply represent the results of eight shift/xor opera
 *      tions for all combinations of data and CRC register values
 *
 *      The values must be right-shifted by eight bits by the "updcrc
 *      logic; the shift must be unsigned (bring in zeroes).  On some
 *      hardware you could probably optimize the shift in assembler by
 *      using byte-swap instructions
 *      polynomial $edb88320
 *
 *
 * CRC32 code derived from work by Gary S. Brown.
 */
static unsigned int crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static uint32_t crc32(const char *buf, unsigned int size) {
	const char *p;
	uint32_t crc = 0 ^ 0xFFFFFFFF;

	p = buf;
	while (size--) {
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFF;
}
/* }}} */

#if HAVE_SSE_CRC32
static uint32_t crc32c_sse42(const char *buf, unsigned int size) /* {{{ */ {
	uint32_t crc = 0 ^ 0xFFFFFFFF;
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
		crc = _mm_crc32_u8(crc, *buf);
	}

	return crc ^ 0xFFFFFFFF;
}
/* }}} */
#endif

#if HAVE_ARM_CRC32
static uint32_t crc32c_arm(const char *buf, unsigned int size) /* {{{ */ {
	uint32_t crc = 0 ^ 0xFFFFFFFF;
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
		crc = __crc32cb(crc, *buf);
	}

	return crc ^ 0xFFFFFFFF;
}
/* }}} */
#endif

static inline unsigned int yac_crc32(char *data, unsigned int size) /* {{{ */ {
	/* large values are only crc'ed on a sample */
	if (size < YAC_FULL_CRC_THRESHOLD) {
		return yac_crc(data, size);
	} else {
		int i = 0;
		char crc_contents[YAC_FULL_CRC_THRESHOLD];
		int head = YAC_FULL_CRC_THRESHOLD >> 2;
		int tail = YAC_FULL_CRC_THRESHOLD >> 4;
		int body = YAC_FULL_CRC_THRESHOLD - head - tail;
		char *p = data + head;
		char *q = crc_contents + head;
		int step = (size - tail - head) / body;

		memcpy(crc_contents, data, head);
		for (; i < body; i++, q++, p+= step) {
			*q = *p;
		}
		memcpy(q, p, tail);

		return yac_crc(crc_contents, YAC_FULL_CRC_THRESHOLD);
	}
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
				++YAC_SG(stats.hits);
				return 1;
			} else {
				yac_kv_val v = *(k.val);
				char *s = USER_ALLOC(YAC_KEY_VLEN(k) + 1);

				memcpy(s, (char *)k.val->data, YAC_KEY_VLEN(k));
				/* guarders: reject a block recycled behind our back */
				if (k.len == v.len && k.u2.crc == yac_crc32(s, YAC_KEY_VLEN(k))) {
					s[YAC_KEY_VLEN(k)] = '\0';
					if (k.val->atime != tv) {
						k.val->atime = tv;
					}
					*data = s;
					*size = YAC_KEY_VLEN(k);
					*flag = k.u1.flag;
					++k.val->hits;
					++YAC_SG(stats.hits);
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

	++YAC_SG(stats.miss);

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

static inline uint32_t yac_storage_pick_victim(yac_kv_key **paths, unsigned long tv) /* {{{ */ {
	/* pick the eviction victim from a full 4-slot probe path: prefer an
	 * expired slot, otherwise the least recently used one; ties fall to
	 * the least hit candidate, then the earliest probe — the evicted slot
	 * is inherited by the new key, and the closer it sits to the home slot
	 * the shorter every future lookup of the new key */
	yac_kv_key c;
	unsigned long atime, oldest;
	uint32_t victim, i;

	for (i = 0; i < 4; i++) {
		c = *paths[i];
		/* expired entries are preferred: natural ttl, or ttl = 1 tombstones
		 * left by delete()/find() — recycling such a slot loses nothing */
		if (c.ttl && c.ttl <= tv) {
			return i;
		}
	}

	/* nothing expired: evict the least recently used; ties fall to the
	 * least hit candidate, then the earliest probe */
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

static inline int yac_storage_fill_value(yac_kv_key *k, unsigned int len, char *data, unsigned int size, unsigned int flag, int is_valid, uint64_t hash, unsigned long tv) /* {{{ */ {
	/* make k ready to carry the new value: block values go into a reused or
	 * freshly allocated block, embedded values live in the tagged word
	 * itself; every field but h/ttl/key/len is filled for the caller to
	 * commit. returns 0 when no value block could be allocated */
	if (!YAC_IS_EMBED(data)) {
		/* reuse the old block if intact and big enough, otherwise
		 * allocate a fresh one (grown by YAC_STORAGE_FACTOR) */
		if (!(k->val && !YAC_IS_EMBED(k->val) && is_valid
				&& k->u2.size >= sizeof(yac_kv_val) + size - 1)) {
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
	uint64_t h, hash, stride;
	uint32_t i;
	yac_kv_key k, *p, *paths[4];
	int found = 0, is_valid;

	hash = yac_hash(key, len);
	stride = YAC_HASH_STRIDE(hash, YAC_SG(slots_mask));

	/* 1. walk the key's probe path (up to 4 slots) looking for the key
	 * itself or an empty slot; if all 4 are taken, remember the whole
	 * path and evict from it */
	h = YAC_HASH_HOME(hash, YAC_SG(slots_mask));
	for (i = 0; i < 4; i++) {
		paths[i] = p = &(YAC_SG(slots)[h]);
		if (!WRITEP(p)) {
			return 0;
		}
		k = *p;
		READP(p);
		if (k.val == NULL) {
			break; /* an insert takes the first empty slot on the path */
		}
		if (YAC_HASH_MATCH(k, hash) && YAC_KEY_KLEN(k) == len && !memcmp(k.key, key, len)) {
			found = 1;
			break; /* k holds the entry being updated */
		}
		h = (h + stride) & YAC_SG(slots_mask);
	}

	/* 2. path full: overwrite the victim chosen by pick_victim */
	if (i == 4) {
		p = paths[yac_storage_pick_victim(paths, tv)];
		if (!WRITEP(p)) {
			return 0;
		}
		k = *p;
		READP(p);
		++YAC_SG(stats.kicks);
	}

	/* 3. k is the slot being overwritten. Only blocks can go stale;
	 * embedded and empty slots are always intact */
	is_valid = (!k.val || YAC_IS_EMBED(k.val)) ? 1 :
		(k.u2.crc == yac_crc32(k.val->data, YAC_KEY_VLEN(k)));
	if (add && found && (!k.ttl || k.ttl > tv) && is_valid) {
		return 0; /* add() must not overwrite a live entry */
	}
	if (k.val == NULL) {
		++YAC_SG(stats.occupied); /* this write occupies a new slot */
	}

	/* 4. fill the new value into k */
	if (!yac_storage_fill_value(&k, len, data, size, flag, is_valid, hash, tv)) {
		return 0;
	}

	/* 5. commit under the slot lock. update the fields individually
	 * instead of copying the whole slot: the u1/u2 unions alias
	 * (flag|hits, crc/size|atime) depending on the storage form, and a
	 * whole-slot copy would re-publish the stale union bytes grabbed in
	 * step 1 over lock-free statistics updates or a concurrent
	 * writer's freshly written crc/size */
	k.h = YAC_HASH_STORE(hash);
	k.ttl = ttl ? tv + ttl : 0;
	memcpy(k.key, key, len);
	YAC_KEY_SET_LEN(k, len, size);
	if (!WRITEP(p)) {
		return 0;
	}
	p->h = k.h;
	p->ttl = k.ttl;
	memcpy(p->key, k.key, len);
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
	yac_storage_info *info = USER_ALLOC(sizeof(yac_storage_info));

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
