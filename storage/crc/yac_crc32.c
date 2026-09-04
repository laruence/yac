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

#include <string.h>
#include "yac_crc32.h"

/* {{{ hardware CRC-32C intrinsics: compiled only when the configure probe
 * succeeded; yac_crc32_startup() re-checks the CPU at runtime */
#if HAVE_SSE_CRC32
# include <nmmintrin.h>
# if defined(__x86_64__)
/* the interleaved path needs a 64-bit hardware CRC word */
#  define YAC_HAVE_CRC_WORD 1
#  define YAC_CRC_WORD(crc, word) _mm_crc32_u64((crc), (word))
#  define YAC_CRC_BYTE(crc, byte) _mm_crc32_u8((crc), (byte))
# endif
#endif

#if HAVE_ARM_CRC32
# define YAC_HAVE_CRC_WORD 1
# include <arm_acle.h>
# define YAC_CRC_WORD(crc, word) __crc32cd((crc), (word))
# define YAC_CRC_BYTE(crc, byte) __crc32cb((crc), (byte))
#endif
/* }}} */

/* {{{ runtime CPU probe (x86 only; ARM builds are arch-gated) */
#if HAVE_SSE_CRC32 && (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))

#if defined(_WIN32)
# include <intrin.h>
# define YAC_CPUID(info, leaf) __cpuid((int *)(info), (leaf))
#else
static inline void yac_cpuid(unsigned int info[4], unsigned int leaf) {
	unsigned int a, b, c, d;

#if defined(__i386__) && defined(__PIC__)
	/* PIC 32-bit cannot put ebx in a clobbered output register */
	__asm__ volatile ("xchgl %%ebx, %1\n\tcpuid\n\txchgl %%ebx, %1"
		: "=a" (a), "=r" (b), "=c" (c), "=d" (d) : "a" (leaf), "c" (0));
#else
	__asm__ volatile ("cpuid"
		: "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (leaf), "c" (0));
#endif
	info[0] = a; info[1] = b; info[2] = c; info[3] = d;
}
# define YAC_CPUID(info, leaf) yac_cpuid((info), (leaf))
#endif

static int yac_cpu_has_sse42(void) {
	unsigned int regs[4];

	YAC_CPUID(regs, 1);
	return (regs[2] & (1u << 20)) != 0; /* ECX bit 20: SSE4.2 */
}
#endif /* HAVE_SSE_CRC32 && x86 */
/* }}} */

/* mounted once by yac_crc32_startup(), read-only afterwards; the
 * interleaved pair stays NULL without a 64-bit hardware CRC word */
static uint32_t (*yac_crc_serial)(const char *data, unsigned int size);
static uint32_t (*yac_crc_interleaved)(const char *data, unsigned int size);
static uint32_t (*yac_snapshot_serial)(char *dst, const char *data, unsigned int size);
static uint32_t (*yac_snapshot_interleaved)(char *dst, const char *data, unsigned int size);

/* {{{ hardware crc/snapshot: one core per ISA, dst NULL = checksum only */
#if HAVE_SSE_CRC32
static uint32_t crc32c_sse42_core(char *dst, const char *buf, unsigned int size) {
	uint32_t crc = 0 ^ 0xFFFFFFFF;

	/* peel to 8-alignment so the word loads below stay aligned */
	while (size && ((uintptr_t)buf & 7)) {
		if (dst) {
			*dst++ = *buf;
		}
		crc = _mm_crc32_u8(crc, (unsigned char)*buf++);
		size--;
	}
#if __x86_64__
	while (size >= sizeof(uint64_t)) {
		uint64_t w = *(uint64_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint64_t));
			dst += sizeof(uint64_t);
		}
		crc = _mm_crc32_u64(crc, w);
		buf += sizeof(uint64_t);
		size -= sizeof(uint64_t);
	}
#endif
	while (size >= sizeof(uint32_t)) {
		uint32_t w = *(uint32_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint32_t));
			dst += sizeof(uint32_t);
		}
		crc = _mm_crc32_u32(crc, w);
		buf += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}
	if (size >= sizeof(uint16_t)) {
		uint16_t w = *(uint16_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint16_t));
			dst += sizeof(uint16_t);
		}
		crc = _mm_crc32_u16(crc, w);
		buf += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}
	if (size) {
		if (dst) {
			*dst = *buf;
		}
		crc = _mm_crc32_u8(crc, (unsigned char)*buf);
	}

	return crc ^ 0xFFFFFFFF;
}

static uint32_t crc32c_sse42(const char *buf, unsigned int size) {
	return crc32c_sse42_core(NULL, buf, size);
}

static uint32_t snapshot_sse42(char *dst, const char *buf, unsigned int size) {
	return crc32c_sse42_core(dst, buf, size);
}
#endif
/* }}} */

/* {{{ hardware crc/snapshot on arm (__crc32cX) */
#if HAVE_ARM_CRC32
static uint32_t crc32c_arm_core(char *dst, const char *buf, unsigned int size) {
	uint32_t crc = 0 ^ 0xFFFFFFFF;

	while (size && ((uintptr_t)buf & 7)) {
		if (dst) {
			*dst++ = *buf;
		}
		crc = __crc32cb(crc, (unsigned char)*buf++);
		size--;
	}
	while (size >= sizeof(uint64_t)) {
		uint64_t w = *(uint64_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint64_t));
			dst += sizeof(uint64_t);
		}
		crc = __crc32cd(crc, w);
		buf += sizeof(uint64_t);
		size -= sizeof(uint64_t);
	}
	while (size >= sizeof(uint32_t)) {
		uint32_t w = *(uint32_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint32_t));
			dst += sizeof(uint32_t);
		}
		crc = __crc32cw(crc, w);
		buf += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}
	if (size >= sizeof(uint16_t)) {
		uint16_t w = *(uint16_t*)buf;
		if (dst) {
			memcpy(dst, &w, sizeof(uint16_t));
			dst += sizeof(uint16_t);
		}
		crc = __crc32ch(crc, w);
		buf += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}
	if (size) {
		if (dst) {
			*dst = *buf;
		}
		crc = __crc32cb(crc, (unsigned char)*buf);
	}

	return crc ^ 0xFFFFFFFF;
}

static uint32_t crc32c_arm(const char *buf, unsigned int size) {
	return crc32c_arm_core(NULL, buf, size);
}

static uint32_t snapshot_arm(char *dst, const char *buf, unsigned int size) {
	return crc32c_arm_core(dst, buf, size);
}
#endif
/* }}} */

/* {{{ three interleaved hardware chains for large values.
 * crc(A || B) = crc(A) advanced by 8*|B| bits XOR crc(B); the advance
 * (appending |B| zero bytes) is tabulated as byte-wise T^(2^i) operators,
 * so reassembly costs four lookups per set bit of |B| regardless of block
 * length. blocks never exceed YAC_STORAGE_MAX_ENTRY_LEN/3, so 20 levels
 * of doubling cover every possible |B| */
#if YAC_HAVE_CRC_WORD
#define YAC_CRC_XPOW_MAXLEV 20
static uint32_t yac_crc_xpow_tab[YAC_CRC_XPOW_MAXLEV][4][256];

/* append one zero byte: eight reflected single-bit steps */
static uint32_t yac_crc_append_byte(uint32_t c) {
	int i;

	for (i = 0; i < 8; i++) {
		c = (c >> 1) ^ (YAC_CRC32_POLY & (uint32_t)(0 - (c & 1)));
	}
	return c;
}

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
	/* T^(2^i) is the previous level applied twice */
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

/* callers only pass size >= YAC_CRC32_INTER_THRESHOLD, so the peel below
 * cannot read past the end; block is a multiple of 8, hence every word
 * load (chains and tail) stays 8-aligned after peeling */
static uint32_t interleaved_core(char *dst, const char *buf, unsigned int size) {
	uint32_t h, c0, c1, c2;
	const char *p0, *p1, *p2;
	char *d0, *d1, *d2;
	unsigned int block, rounds;

	h = 0xFFFFFFFFu;
	while ((uintptr_t)buf & 7) {
		unsigned char b = *buf;

		if (dst) {
			*dst++ = b;
		}
		h = YAC_CRC_BYTE(h, b);
		buf++;
		size--;
	}

	block = (size / 3) & ~7u; /* word-aligned blocks */
	p0 = buf;
	p1 = buf + block;
	p2 = buf + block * 2;
	if (dst) {
		d0 = dst;
		d1 = dst + block;
		d2 = dst + block * 2;
	} else {
		d0 = d1 = d2 = NULL;
	}
	rounds = block >> 3;

	c0 = c1 = c2 = 0xFFFFFFFFu;
	while (rounds--) {
		uint64_t w0 = *(uint64_t*)p0, w1 = *(uint64_t*)p1, w2 = *(uint64_t*)p2;

		if (dst) {
			memcpy(d0, &w0, sizeof(uint64_t));
			memcpy(d1, &w1, sizeof(uint64_t));
			memcpy(d2, &w2, sizeof(uint64_t));
			d0 += sizeof(uint64_t);
			d1 += sizeof(uint64_t);
			d2 += sizeof(uint64_t);
		}
		c0 = YAC_CRC_WORD(c0, w0);
		c1 = YAC_CRC_WORD(c1, w1);
		c2 = YAC_CRC_WORD(c2, w2);
		p0 += sizeof(uint64_t);
		p1 += sizeof(uint64_t);
		p2 += sizeof(uint64_t);
	}

	c0 = yac_crc32_combine(h ^ 0xFFFFFFFFu, c0 ^ 0xFFFFFFFFu, block);
	c0 = yac_crc32_combine(c0, c1 ^ 0xFFFFFFFFu, block);
	c0 = yac_crc32_combine(c0, c2 ^ 0xFFFFFFFFu, block);
	if (size > block * 3) {
		/* up to 23 trailing bytes, still 8-aligned */
		unsigned int tail = size - block * 3;
		uint32_t ct;

#if HAVE_SSE_CRC32
		ct = crc32c_sse42_core(dst ? dst + block * 3 : NULL, buf + block * 3, tail);
#else
		ct = crc32c_arm_core(dst ? dst + block * 3 : NULL, buf + block * 3, tail);
#endif
		c0 = yac_crc32_combine(c0, ct, tail);
	}
	return c0;
}

static uint32_t yac_crc32c_interleaved(const char *buf, unsigned int size) {
	return interleaved_core(NULL, buf, size);
}

static uint32_t snapshot_interleaved(char *dst, const char *buf, unsigned int size) {
	return interleaved_core(dst, buf, size);
}
#endif
/* }}} */

/* {{{ slicing-by-8 software fallback for CPUs without hardware CRC-32C:
 * the same polynomial the hardware instructions compute, so results agree
 * bit for bit; tables in yac_crc32_tab.h, verified against RFC 3720 B.4
 * ("123456789" -> 0xe3069283) */
#include "yac_crc32_tab.h"

static uint32_t slice8_core(char *dst, const char *data, unsigned int size) {
	const unsigned char *p = (const unsigned char *)data;
	uint32_t crc = 0xFFFFFFFFu;

#define YAC_SLICE_STORE(d, s) do { if (d) { *(d)++ = (s); } } while (0)

	while (size && ((uintptr_t)p & 7)) {
		unsigned char b = *p;

		YAC_SLICE_STORE(dst, b);
		crc = yac_crc32c_tab[0][(crc ^ b) & 0xFF] ^ (crc >> 8);
		p++;
		size--;
	}
	while (size >= 8) {
		uint64_t w;

		memcpy(&w, p, sizeof(uint64_t));
		if (dst) {
			memcpy(dst, &w, sizeof(uint64_t));
			dst += sizeof(uint64_t);
		}
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
		unsigned char b = *p;

		YAC_SLICE_STORE(dst, b);
		crc = yac_crc32c_tab[0][(crc ^ b) & 0xFF] ^ (crc >> 8);
		p++;
	}
#undef YAC_SLICE_STORE
	return crc ^ 0xFFFFFFFF;
}

static uint32_t crc32c_sw(const char *buf, unsigned int size) {
	return slice8_core(NULL, buf, size);
}

static uint32_t snapshot_slice8(char *dst, const char *data, unsigned int size) {
	return slice8_core(dst, data, size);
}
/* }}} */

void yac_crc32_startup(void) /* {{{ */ {
#if HAVE_SSE_CRC32
	if (yac_cpu_has_sse42()) {
		yac_crc_serial = crc32c_sse42;
		yac_snapshot_serial = snapshot_sse42;
# if YAC_HAVE_CRC_WORD
		yac_crc32c_init();
		yac_crc_interleaved = yac_crc32c_interleaved;
		yac_snapshot_interleaved = snapshot_interleaved;
# endif
	} else
#endif
	{
#if HAVE_ARM_CRC32
		yac_crc_serial = crc32c_arm;
		yac_snapshot_serial = snapshot_arm;
		yac_crc32c_init();
		yac_crc_interleaved = yac_crc32c_interleaved;
		yac_snapshot_interleaved = snapshot_interleaved;
#else
		yac_crc_serial = crc32c_sw;
		yac_snapshot_serial = snapshot_slice8;
#endif
	}
}
/* }}} */

uint32_t yac_crc32(const char *data, unsigned int size) /* {{{ */ {
	if (yac_crc_interleaved == NULL || size < YAC_CRC32_INTER_THRESHOLD) {
		return yac_crc_serial(data, size);
	}
	return yac_crc_interleaved(data, size);
}
/* }}} */

uint32_t yac_crc32_snapshot(char *dst, const char *data, unsigned int size) /* {{{ */ {
	if (yac_snapshot_interleaved == NULL || size < YAC_CRC32_INTER_THRESHOLD) {
		return yac_snapshot_serial(dst, data, size);
	}
	return yac_snapshot_interleaved(dst, data, size);
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
