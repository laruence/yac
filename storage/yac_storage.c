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

#include "php.h"
#include "yac_storage.h"
#include "allocator/yac_allocator.h"

yac_storage_globals *yac_storage;

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

	size = YAC_SG(first_seg).size - ((char *)YAC_SG(slots) - (char *)yac_storage);
	real_size = yac_storage_align_size(size / sizeof(yac_kv_key));
	if (!((size / sizeof(yac_kv_key)) & ~(real_size << 1))) {
		real_size <<= 1;
	}

	YAC_SG(slots_size) 	= real_size;
	YAC_SG(slots_mask) 	= real_size - 1;
	YAC_SG(slots_num)  	= 0;
	YAC_SG(fails)      	= 0;
	YAC_SG(hits)  		= 0;
	YAC_SG(miss)    	= 0;
	YAC_SG(kicks)    	= 0;

   	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * real_size);

	return 1;
}
/* }}} */

void yac_storage_shutdown(void) /* {{{ */ {
	yac_allocator_shutdown();
}
/* }}} */

/* {{{ MurmurHash2 (Austin Appleby)
 */
static inline ulong yac_inline_hash_func1(char *data, unsigned int len) {
    unsigned int h, k;

    h = 0 ^ len;

    while (len >= 4) {
        k  = data[0];
        k |= data[1] << 8;
        k |= data[2] << 16;
        k |= data[3] << 24;

        k *= 0x5bd1e995;
        k ^= k >> 24;
        k *= 0x5bd1e995;

        h *= 0x5bd1e995;
        h ^= k;

        data += 4;
        len -= 4;
    }

    switch (len) {
    case 3:
        h ^= data[2] << 16;
    case 2:
        h ^= data[1] << 8;
    case 1:
        h ^= data[0];
        h *= 0x5bd1e995;
    }

    h ^= h >> 13;
    h *= 0x5bd1e995;
    h ^= h >> 15;

    return h;
}
/* }}} */

/* {{{ DJBX33A (Daniel J. Bernstein, Times 33 with Addition)
 *
 * This is Daniel J. Bernstein's popular `times 33' hash function as
 * posted by him years ago on comp->lang.c. It basically uses a function
 * like ``hash(i) = hash(i-1) * 33 + str[i]''. This is one of the best
 * known hash functions for strings. Because it is both computed very
 * fast and distributes very well.
 *
 * The magic of number 33, i.e. why it works better than many other
 * constants, prime or not, has never been adequately explained by
 * anyone. So I try an explanation: if one experimentally tests all
 * multipliers between 1 and 256 (as RSE did now) one detects that even
 * numbers are not useable at all. The remaining 128 odd numbers
 * (except for the number 1) work more or less all equally well. They
 * all distribute in an acceptable way and this way fill a hash table
 * with an average percent of approx. 86%. 
 *
 * If one compares the Chi^2 values of the variants, the number 33 not
 * even has the best value. But the number 33 and a few other equally
 * good numbers like 17, 31, 63, 127 and 129 have nevertheless a great
 * advantage to the remaining numbers in the large set of possible
 * multipliers: their multiply operation can be replaced by a faster
 * operation based on just one shift plus either a single addition
 * or subtraction operation. And because a hash function has to both
 * distribute good _and_ has to be very fast to compute, those few
 * numbers should be preferred and seems to be the reason why Daniel J.
 * Bernstein also preferred it.
 *
 *
 *                  -- Ralf S. Engelschall <rse@engelschall.com>
 */

static inline ulong yac_inline_hash_func2(char *key, uint len) {
	register ulong hash = 5381;

	/* variant with the hash unrolled eight times */
	for (; len >= 8; len -= 8) {
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
		hash = ((hash << 5) + hash) + *key++;
	}
	switch (len) {
		case 7: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 6: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 5: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 4: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 3: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 2: hash = ((hash << 5) + hash) + *key++; /* fallthrough... */
		case 1: hash = ((hash << 5) + hash) + *key++; break;
		case 0: break;
		default: break;
	}
	return hash;
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
 *  sends each low-bit to hight-bit; and the result is transmission bit
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

static inline unsigned int crc32(char *buf, unsigned int size) {
	const char *p;
	register int crc = 0;

	p = buf;
	while (size--) {
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}

	return crc ^ ~0U;
}
/* }}} */

static inline unsigned int yac_crc32(char *data, unsigned int size) /* {{{ */ {
	if (size < YAC_FULL_CRC_THRESHOLD) {
		return crc32(data, size);
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

		return crc32(crc_contents, YAC_FULL_CRC_THRESHOLD);
	}
}
/* }}} */

int yac_storage_find(char *key, unsigned int len, char **data, unsigned int *size, unsigned int *flag, int *cas, unsigned long tv) /* {{{ */ {
	ulong h, hash, seed;
	yac_kv_key k, *p;
	yac_kv_val v;

	hash = h = yac_inline_hash_func1(key, len);
	p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
	k = *p;
	if (k.val) {
		char *s;
		uint i;
		if (k.h == hash && YAC_KEY_KLEN(k) == len) {
			v = *(k.val);
			if (!memcmp(k.key, key, len)) {
				s = USER_ALLOC(YAC_KEY_VLEN(k) + 1);
				memcpy(s, (char *)k.val->data, YAC_KEY_VLEN(k));
do_verify:
				if (k.len != v.len) {
					USER_FREE(s);
					++YAC_SG(miss);
					return 0;
				}

				if (k.ttl) {
					if (k.ttl <= tv) {
						++YAC_SG(miss);
						USER_FREE(s);
						return 0;
					}
				}

				if (k.crc != yac_crc32(s, YAC_KEY_VLEN(k))) {
					USER_FREE(s);
					++YAC_SG(miss);
					return 0;
				}
				s[YAC_KEY_VLEN(k)] = '\0';
				k.val->atime = tv;
				*data = s;
				*size = YAC_KEY_VLEN(k);
				*flag = k.flag;
				++YAC_SG(hits);
				return 1;
			}
		} 

		seed = yac_inline_hash_func2(key, len);
		for (i = 0; i < 3; i++) {
			h += seed & YAC_SG(slots_mask);
			p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
			k = *p;
			if (k.h == hash && YAC_KEY_KLEN(k) == len) {
				v = *(k.val);
				if (!memcmp(k.key, key, len)) {
					s = USER_ALLOC(YAC_KEY_VLEN(k) + 1);
					memcpy(s, (char *)k.val->data, YAC_KEY_VLEN(k));
					goto do_verify;
				}
			}
		}
	}

	++YAC_SG(miss);

	return 0;
}
/* }}} */

void yac_storage_delete(char *key, unsigned int len, int ttl, unsigned long tv) /* {{{ */ {
	ulong hash, h, seed;
	yac_kv_key k, *p;

	hash = h = yac_inline_hash_func1(key, len);
	p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
	k = *p;
	if (k.val) {
		uint i;
		if (k.h == hash && YAC_KEY_KLEN(k) == len) {
			if (!memcmp((char *)k.key, key, len)) {
				if (ttl == 0) {
					p->ttl = 1;
				} else {
					p->ttl = ttl + tv;
				}
				return;
			}
		} 

		seed = yac_inline_hash_func2(key, len);
		for (i = 0; i < 3; i++) {
			h += seed & YAC_SG(slots_mask);
			p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
			k = *p;
			if (k.val == NULL) {
				return;
			} else if (k.h == hash && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
				p->ttl = 1;
				return;
			}
		}
	}
}
/* }}} */

int yac_storage_update(char *key, unsigned int len, char *data, unsigned int size, unsigned int flag, int ttl, int add, unsigned long tv) /* {{{ */ {
	ulong hash, h;
	int idx = 0, is_valid;
	yac_kv_key *p, k, *paths[4];
	yac_kv_val *val, *s;
	unsigned long real_size;

	hash = h = yac_inline_hash_func1(key, len);
	paths[idx++] = p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
	k = *p;
	if (k.val) {
		/* Found the exact match */
		if (k.h == hash && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
do_update:
			is_valid = 0;
			if (k.crc == yac_crc32(k.val->data, YAC_KEY_VLEN(k))) {
				is_valid = 1;
			}
			if (add && (!k.ttl || k.ttl > tv) && is_valid) {
				return 0;
			}
			if (k.size >= size && is_valid) {
				s = USER_ALLOC(sizeof(yac_kv_val) + size - 1);
				memcpy(s->data, data, size);
				if (ttl) {
					k.ttl = (ulong)tv + ttl;
				} else {
					k.ttl = 0;
				}
				s->atime = tv;
				YAC_KEY_SET_LEN(*s, len, size);
				memcpy((char *)k.val, (char *)s, sizeof(yac_kv_val) + size - 1);
				k.crc = yac_crc32(s->data, size);
				k.flag = flag;
				memcpy(k.key, key, len);
				YAC_KEY_SET_LEN(k, len, size);
				*p = k;
				USER_FREE(s);
				return 1;
			} else {
				uint msize;
				real_size = yac_allocator_real_size(sizeof(yac_kv_val) + (size * YAC_STORAGE_FACTOR) - 1);
				if (!real_size) {
					++YAC_SG(fails);
					return 0;
				}
				msize = sizeof(yac_kv_val) + size - 1;
				s = USER_ALLOC(sizeof(yac_kv_val) + size - 1);
				memcpy(s->data, data, size);
				s->atime = tv;
				YAC_KEY_SET_LEN(*s, len, size);
				val = yac_allocator_raw_alloc(real_size, (int)hash);
				if (val) {
					memcpy((char *)val, (char *)s, msize);
					if (ttl) {
						k.ttl = tv + ttl;
					} else {
						k.ttl = 0;
					}
					k.crc = yac_crc32(s->data, size);
					k.val = val;
					k.flag = flag;
					k.size = real_size;
					memcpy(k.key, key, len);
					YAC_KEY_SET_LEN(k, len, size);
					*p = k;
					USER_FREE(s);
					return 1;
				}
				++YAC_SG(fails);
				USER_FREE(s);
				return 0;
			}
		} else {
			uint i;
			ulong seed, max_atime;

			seed = yac_inline_hash_func2(key, len);
			for (i = 0; i < 3; i++) {
				h += seed & YAC_SG(slots_mask);
				paths[idx++] = p = &(YAC_SG(slots)[h & YAC_SG(slots_mask)]);
				k = *p;
				if (k.val == NULL) {
					goto do_add;
				} else if (k.h == hash && YAC_KEY_KLEN(k) == len && !memcmp((char *)k.key, key, len)) {
					/* Found the exact match */
					goto do_update;
				}
			}
			
			--idx;
			max_atime = paths[idx]->val->atime;
			for (i = 0; i < idx; i++) {
				if ((paths[i]->ttl && paths[i]->ttl <= tv) || paths[i]->len != paths[i]->val->len) {
					p = paths[i];
					goto do_add;
				} else if (paths[i]->val->atime < max_atime) {
					max_atime = paths[i]->val->atime;
					p = paths[i];
				}
			}
			++YAC_SG(kicks);
			k = *p;
			k.h = hash;

			goto do_update;
		}
	} else {
do_add:
		real_size = yac_allocator_real_size(sizeof(yac_kv_val) + (size * YAC_STORAGE_FACTOR) - 1);
		if (!real_size) {
			++YAC_SG(fails);
			return 0;
		}
		s = USER_ALLOC(sizeof(yac_kv_val) + size - 1);
		memcpy(s->data, data, size);
		s->atime = tv;
		YAC_KEY_SET_LEN(*s, len, size);
		val = yac_allocator_raw_alloc(real_size, (int)hash);
		if (val) {
			memcpy((char *)val, (char *)s, sizeof(yac_kv_val) + size - 1);
			if (p->val == NULL) {
				++YAC_SG(slots_num);
			}
			k.h = hash;
			k.val = val;
			k.flag = flag;
			k.size = real_size;
			k.crc = yac_crc32(s->data, size);
			memcpy(k.key, key, len);
			YAC_KEY_SET_LEN(k, len, size);
			if (ttl) {
				k.ttl = tv + ttl;
			} else {
				k.ttl = 0;
			}
			*p = k;
			USER_FREE(s);
			return 1;
		}
		++YAC_SG(fails);
		USER_FREE(s);
	}

	return 0;
}
/* }}} */

void yac_storage_flush(void) /* {{{ */ {
	YAC_SG(slots_num) = 0;
	memset((char *)YAC_SG(slots), 0, sizeof(yac_kv_key) * YAC_SG(slots_size));
}
/* }}} */

yac_storage_info * yac_storage_get_info(void) /* {{{ */ {
	yac_storage_info *info = USER_ALLOC(sizeof(yac_storage_info));

	info->k_msize = (unsigned long)YAC_SG(first_seg).size;
	info->v_msize = (unsigned long)YAC_SG(segments)[0]->size * (unsigned long)YAC_SG(segments_num);
	info->segment_size = YAC_SG(segments)[0]->size;
	info->segments_num = YAC_SG(segments_num);
	info->hits = YAC_SG(hits);
	info->miss = YAC_SG(miss);
	info->fails = YAC_SG(fails);
	info->kicks = YAC_SG(kicks);
	info->recycles = YAC_SG(recycles);
	info->slots_size = YAC_SG(slots_size);
	info->slots_num = YAC_SG(slots_num);

	return info;
}
/* }}} */

void yac_storage_free_info(yac_storage_info *info) /* {{{ */ {
	USER_FREE(info);
}
/* }}} */

yac_item_list * yac_storage_dump(unsigned int limit) /* {{{ */ {
	yac_kv_key k;
	yac_item_list *item, *list = NULL;

	if (YAC_SG(slots_num)) {
		unsigned int i = 0, n = 0;
		for (; i<YAC_SG(slots_size) && n < YAC_SG(slots_num) && n < limit; i++) {
			k = YAC_SG(slots)[i];
			if (k.val) {
				item = USER_ALLOC(sizeof(yac_item_list));
				item->index = i;
				item->h = k.h;
				item->crc = k.crc;
				item->ttl = k.ttl;
				item->k_len = YAC_KEY_KLEN(k);
				item->v_len = YAC_KEY_VLEN(k);
				item->flag = k.flag;
				item->size = k.size;
				memcpy(item->key, k.key, YAC_STORAGE_MAX_KEY_LEN);
				item->next = list;
				list = item;
				++n;
			}
		}
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
