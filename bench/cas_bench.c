/* cas_bench.c — cost of compare-and-swap (__sync_bool_compare_and_swap).
 *
 * 1) uncontended: one thread CASing a hot cache line it already owns
 * 2) contended:   two threads fighting over the same word
 *
 * build: gcc -O2 -pthread -o cas_bench cas_bench.c
 * run:   taskset -c <core> ./cas_bench <core_a> <core_b>
 *        (the taskset pin matters for part 1; core_a/core_b are the two
 *         physical cores for part 2, default 1 3)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

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

/* ---- part 1: uncontended ---- */

static double cas_uncontended(void) {
	unsigned int lock __attribute__((aligned(64))) = 0;
	uint64_t best = UINT64_MAX;
	int r;
	long i;

	for (r = 0; r < 7; r++) {
		uint64_t s = tick();
		for (i = 0; i < 10000000; i++) {
			__sync_bool_compare_and_swap(&lock, 0, 0);
		}
		uint64_t e = tick();
		if (e - s < best) {
			best = e - s;
		}
	}
	return best / 1e7; /* ticks per CAS */
}

/* ---- part 2: two threads fighting over one word ---- */

static volatile unsigned int fight = 0;
static volatile int start_flag = 0;

struct result {
	int core;
	long attempts, success;
	uint64_t ticks;
};

static void *fighter(void *arg) {
	struct result *res = arg;
	unsigned int expect = 0;
	long i;
	uint64_t s, e;
#ifdef __linux__
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(res->core, &set);
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
	while (!start_flag) {
		sched_yield();
	}
	s = tick();
	for (i = 0; i < 2000000; i++) {
		if (__sync_bool_compare_and_swap(&fight, expect, expect ^ 1)) {
			expect ^= 1;
			res->success++;
		}
		res->attempts++;
	}
	e = tick();
	res->ticks = e - s;
	return NULL;
}

/* unlocked increment of a shared line by two threads: measures the
 * cache-line bouncing (RFO ping-pong), not the instruction */
static volatile unsigned int fight_unlocked __attribute__((aligned(64))) = 0;

static void *bumper(void *arg) {
	struct result *res = arg;
	long i;
	uint64_t s, e;
#ifdef __linux__
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(res->core, &set);
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
	while (!start_flag) {
		sched_yield();
	}
	s = tick();
	for (i = 0; i < 2000000; i++) {
		fight_unlocked++;
	}
	e = tick();
	res->attempts = 2000000;
	res->ticks = e - s;
	return NULL;
}

int main(int argc, char **argv) {
	double hz = tick_hz();
	double t;

#if defined(__x86_64__) || defined(_M_X64)
	fprintf(stderr, "tsc calibrated: %.3f GHz\n", hz / 1e9);
#endif

	t = cas_uncontended();
	printf("uncontended lock cmpxchg: %6.1f cycles (%5.1f ns)\n", t, t * 1e9 / hz);

	{
		static volatile unsigned int ctr __attribute__((aligned(64))) = 0;
		uint64_t best = UINT64_MAX;
		int r;
		long i;

		for (r = 0; r < 7; r++) {
			uint64_t s = tick();
			for (i = 0; i < 100000000; i++) {
				ctr++;
			}
			uint64_t e = tick();
			if (e - s < best) {
				best = e - s;
			}
		}
		printf("unlocked ++ (private line): %4.2f cycles (%4.2f ns)\n",
		       best / 1e8, best / 1e8 * 1e9 / hz);
	}

	{
		int core_a = (argc > 2) ? atoi(argv[1]) : 1;
		int core_b = (argc > 2) ? atoi(argv[2]) : 3;
		pthread_t ta, tb;
		struct result ra, rb;

		memset(&ra, 0, sizeof(ra));
		memset(&rb, 0, sizeof(rb));
		ra.core = core_a;
		rb.core = core_b;
		fight = 0;

		pthread_create(&ta, NULL, fighter, &ra);
		pthread_create(&tb, NULL, fighter, &rb);
		start_flag = 1;
		pthread_join(ta, NULL);
		pthread_join(tb, NULL);

		printf("contended (cpu%d vs cpu%d): %ld attempts, %ld succeeded\n",
		       core_a, core_b, ra.attempts + rb.attempts, ra.success + rb.success);
		printf("  per attempt: cpu%6.1f cycles (%5.1f ns) | cpu%6.1f cycles (%5.1f ns)\n",
		       ra.ticks * 1.0 / ra.attempts, ra.ticks * 1.0 / ra.attempts * 1e9 / hz,
		       rb.ticks * 1.0 / rb.attempts, rb.ticks * 1.0 / rb.attempts * 1e9 / hz);

		/* same fight, but with plain unlocked ++ : line bouncing only */
		start_flag = 0;
		fight_unlocked = 0;
		memset(&ra, 0, sizeof(ra));
		memset(&rb, 0, sizeof(rb));
		ra.core = core_a;
		rb.core = core_b;
		pthread_create(&ta, NULL, bumper, &ra);
		pthread_create(&tb, NULL, bumper, &rb);
		start_flag = 1;
		pthread_join(ta, NULL);
		pthread_join(tb, NULL);
		printf("unlocked ++ on one shared line (cpu%d vs cpu%d): %6.1f cycles (%5.1f ns) per ++\n",
		       core_a, core_b,
		       (ra.ticks + rb.ticks) * 1.0 / (ra.attempts + rb.attempts),
		       (ra.ticks + rb.ticks) * 1.0 / (ra.attempts + rb.attempts) * 1e9 / hz);
	}
	return 0;
}
