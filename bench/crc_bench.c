/* crc_bench.c — CRC-32C paths for Yac: load style x interleave.
 *
 * variants:
 *   serial_mc   single chain, memcpy word loads (alignment-agnostic)
 *   serial_al   single chain, peel to 8-aligned then direct word loads
 *   inter_al    three chains + byte-table reassembly, peel + direct loads
 *
 * correctness is checked for every variant at buffer offsets 0..7.
 *
 * build (x86):  gcc -O2 -msse4.2 -o crc_bench crc_bench.c
 * build (arm):  gcc -O2 -march=armv8-a+crc -o crc_bench crc_bench.c
 * run:          taskset -c <core> ./crc_bench
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#if defined(__x86_64__) || defined(_M_X64)
# include <x86intrin.h>
# define W64(crc, w) _mm_crc32_u64((crc), (w))
# define W32(crc, w) _mm_crc32_u32((crc), (w))
# define W16(crc, w) _mm_crc32_u16((crc), (w))
# define W8(crc, b)  _mm_crc32_u8((crc), (b))
#elif defined(__aarch64__)
# include <arm_acle.h>
# define W64(crc, w) __crc32cd((crc), (w))
# define W32(crc, w) __crc32cw((crc), (w))
# define W16(crc, w) __crc32ch((crc), (w))
# define W8(crc, b)  __crc32cb((crc), (b))
#else
# error need x86_64 SSE4.2 or aarch64 crc instructions
#endif

#define POLY 0x82F63B78u /* reflected CRC-32C */

static volatile uint32_t sink;

/* ---- serial, memcpy loads ---- */

static uint32_t serial_mc(const char *buf, size_t size) {
	uint32_t crc = 0xFFFFFFFFu;

	while (size >= 8) {
		uint64_t w;
		memcpy(&w, buf, 8);
		crc = W64(crc, w);
		buf += 8;
		size -= 8;
	}
	while (size >= 4) {
		uint32_t w;
		memcpy(&w, buf, 4);
		crc = W32(crc, w);
		buf += 4;
		size -= 4;
	}
	if (size >= 2) {
		uint16_t w;
		memcpy(&w, buf, 2);
		crc = W16(crc, w);
		buf += 2;
		size -= 2;
	}
	if (size) {
		crc = W8(crc, (unsigned char)*buf);
	}
	return crc ^ 0xFFFFFFFFu;
}

/* ---- serial, peel to alignment then direct loads ---- */

static uint32_t serial_al(const char *buf, size_t size) {
	uint32_t crc = 0xFFFFFFFFu;

	while (size && ((uintptr_t)buf & 7)) {
		crc = W8(crc, (unsigned char)*buf++);
		size--;
	}
	while (size >= 8) {
		crc = W64(crc, *(uint64_t*)buf);
		buf += 8;
		size -= 8;
	}
	while (size >= 4) {
		crc = W32(crc, *(uint32_t*)buf);
		buf += 4;
		size -= 4;
	}
	if (size >= 2) {
		crc = W16(crc, *(uint16_t*)buf);
		buf += 2;
		size -= 2;
	}
	if (size) {
		crc = W8(crc, (unsigned char)*buf);
	}
	return crc ^ 0xFFFFFFFFu;
}

/* ---- byte tables of the T^(2^lev) "append zero bytes" operators ---- */

#define XPOW_MAXLEV 20
static uint32_t xpow_tab[XPOW_MAXLEV][4][256];

static uint32_t append_byte(uint32_t c) {
	int i;

	for (i = 0; i < 8; i++) {
		c = (c >> 1) ^ (POLY & (uint32_t)(0 - (c & 1)));
	}
	return c;
}

static inline uint32_t apply_xpow(uint32_t c, int lev) {
	return xpow_tab[lev][0][c & 0xFF]
	     ^ xpow_tab[lev][1][(c >> 8) & 0xFF]
	     ^ xpow_tab[lev][2][(c >> 16) & 0xFF]
	     ^ xpow_tab[lev][3][c >> 24];
}

static void xpow_init(void) {
	int lev, j, v;
	uint32_t x;

	for (j = 0; j < 4; j++) {
		for (v = 0; v < 256; v++) {
			xpow_tab[0][j][v] = append_byte((uint32_t)v << (8 * j));
		}
	}
	for (lev = 1; lev < XPOW_MAXLEV; lev++) {
		for (j = 0; j < 4; j++) {
			for (v = 0; v < 256; v++) {
				x = apply_xpow((uint32_t)v << (8 * j), lev - 1);
				xpow_tab[lev][j][v] = apply_xpow(x, lev - 1);
			}
		}
	}
}

static uint32_t combine(uint32_t crc1, uint32_t crc2, unsigned int len2) {
	int lev = 0;

	while (len2) {
		if (len2 & 1) {
			crc1 = apply_xpow(crc1, lev);
		}
		len2 >>= 1;
		lev++;
	}
	return crc1 ^ crc2;
}

/* ---- three-way interleaved, peel + direct loads ---- */

static uint32_t inter_al(const char *buf, size_t size) {
	uint32_t h, c0, c1, c2;
	const char *p0, *p1, *p2;
	size_t block, rounds;

	h = 0xFFFFFFFFu;
	while (size && ((uintptr_t)buf & 7)) {
		h = W8(h, (unsigned char)*buf++);
		size--;
	}

	block = (size / 3) & ~(size_t)7;
	p0 = buf;
	p1 = buf + block;
	p2 = buf + block * 2;
	rounds = block >> 3;

	c0 = c1 = c2 = 0xFFFFFFFFu;
	while (rounds--) {
		c0 = W64(c0, *(uint64_t*)p0);
		c1 = W64(c1, *(uint64_t*)p1);
		c2 = W64(c2, *(uint64_t*)p2);
		p0 += 8;
		p1 += 8;
		p2 += 8;
	}

	c0 = combine(h ^ 0xFFFFFFFFu, c0 ^ 0xFFFFFFFFu, (unsigned int)block);
	c0 = combine(c0, c1 ^ 0xFFFFFFFFu, (unsigned int)block);
	c0 = combine(c0, c2 ^ 0xFFFFFFFFu, (unsigned int)block);
	if (size > block * 3) {
		c0 = combine(c0, serial_al(buf + block * 3, size - block * 3),
		             (unsigned int)(size - block * 3));
	}
	return c0;
}

/* ---- timing ---- */

#if defined(__x86_64__) || defined(_M_X64)
static inline uint64_t tick(void) {
	unsigned lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}
static double tick_hz(void) {
	struct timespec t0, t1, req = {0, 100 * 1000 * 1000};
	uint64_t c0, c1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	c0 = tick();
	nanosleep(&req, NULL);
	c1 = tick();
	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (double)(c1 - c0) / ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9);
}
#else
static inline uint64_t tick(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; /* ns */
}
static double tick_hz(void) { return 1e9; }
#endif

typedef uint32_t (*fn_t)(const char *, size_t);

static double bench(fn_t f, const char *buf, size_t size, long it) {
	uint64_t best = UINT64_MAX;
	uint32_t acc = 0;
	int r;
	long i;

	for (r = 0; r < 7; r++) {
		uint64_t c0 = tick();
		for (i = 0; i < it; i++) {
			acc ^= f(buf, size);
		}
		uint64_t c1 = tick();
		if (c1 - c0 < best) {
			best = c1 - c0;
		}
	}
	sink ^= acc;
	return (double)best / it;
}

int main(void) {
	static const size_t sizes[] = {
		100, 256, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 16384,
		32768, 65536, 131072, 262144, 524288, 1048576
	};
	size_t max_size = 1048576 + 8, s, i;
	int off;
	char *buf = malloc(max_size);
	uint64_t rs = 0x9E3779B97F4A7C15ull;
	double hz;

	if (!buf) {
		return 1;
	}
	xpow_init();

	for (i = 0; i < max_size; i++) {
		rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
		buf[i] = (char)rs;
	}

	/* correctness at every misalignment: all variants must agree */
	for (off = 0; off < 8; off++) {
		for (s = 1; s <= 8192; s += (s < 64 ? 1 : 17)) {
			if (serial_al(buf + off, s) != serial_mc(buf + off, s) ||
			    inter_al(buf + off, s) != serial_mc(buf + off, s)) {
				fprintf(stderr, "MISMATCH off=%d size=%zu\n", off, s);
				return 1;
			}
		}
		for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			if (serial_al(buf + off, sizes[i]) != serial_mc(buf + off, sizes[i]) ||
			    inter_al(buf + off, sizes[i]) != serial_mc(buf + off, sizes[i])) {
				fprintf(stderr, "MISMATCH off=%d size=%zu\n", off, sizes[i]);
				return 1;
			}
		}
	}
	fprintf(stderr, "correctness: all variants agree at offsets 0..7\n");

	hz = tick_hz();
#if defined(__x86_64__) || defined(_M_X64)
	fprintf(stderr, "tsc calibrated: %.3f GHz\n", hz / 1e9);
#endif

	/* alignment sensitivity of the serial chain: aligned vs +1 misaligned */
	printf("--- serial chain: memcpy loads vs peel+direct (ns/op) ---\n");
	printf("%8s | %10s %10s | %10s %10s\n",
	       "size", "mc@0", "al@0", "mc@+1", "al@+1");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		size_t sz = sizes[i];
		long it = (long)(2e7 / (sz > 16 ? sz : 16));

		if (it < 30) it = 30;
		if (it > 2000000) it = 2000000;

		printf("%8zu | %10.1f %10.1f | %10.1f %10.1f\n", sz,
		       bench(serial_mc, buf, sz, it) * 1e9 / hz,
		       bench(serial_al, buf, sz, it) * 1e9 / hz,
		       bench(serial_mc, buf + 1, sz, it) * 1e9 / hz,
		       bench(serial_al, buf + 1, sz, it) * 1e9 / hz);
	}

	printf("--- aligned input: serial vs interleaved (ns/op) ---\n");
	printf("%8s | %10s %10s | %8s\n", "size", "serial", "inter", "speedup");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		size_t sz = sizes[i];
		long it = (long)(2e7 / (sz > 16 ? sz : 16));
		double t_s, t_i;

		if (it < 30) it = 30;
		if (it > 2000000) it = 2000000;

		t_s = bench(serial_al, buf, sz, it);
		t_i = bench(inter_al, buf, sz, it);
		printf("%8zu | %10.1f %10.1f | %7.2fx\n", sz,
		       t_s * 1e9 / hz, t_i * 1e9 / hz, t_s / t_i);
	}
	printf("(best of 7; buffer cache-hot)\n");

	free(buf);
	return 0;
}
