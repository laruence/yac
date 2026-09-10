/*
  +----------------------------------------------------------------------+
  | Yet Another Cache                                                    |
  +----------------------------------------------------------------------+
  | Copyright (c) 2013-2013 The PHP Group                                |
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web at the following url:           |
  | license@php.net.                                                     |
  +----------------------------------------------------------------------+
  | Authors: Xinchen Hui <laruence@php.net>                              |
  |         John Neo <nhf0424@gmail.com>                                 |
  +----------------------------------------------------------------------+
*/

#ifndef YAC_ATOMIC_H
#define YAC_ATOMIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* two independent axes: which builtins exist (compiler) and which
 * instructions exist (isa). mingw is both _WIN32 and GNU, so the compiler
 * axis must not be spelled _WIN32 -- it would send mingw down the MSVC
 * path, where none of the _M_* isa macros match */
#if defined(__GNUC__) || defined(__clang__)
# define YAC_CC_GNU     1
#else
# define YAC_CC_GNU     0
#endif
#if defined(_MSC_VER)
# define YAC_CC_MSVC    1
#else
# define YAC_CC_MSVC    0
#endif

#if defined(__x86_64__) || defined(__amd64__) || defined(__i386__) || \
		defined(_M_X64) || defined(_M_IX86)
# define YAC_ARCH_X86   1
#else
# define YAC_ARCH_X86   0
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
# define YAC_ARCH_ARM64 1
#else
# define YAC_ARCH_ARM64 0
#endif

/* clang-cl sets both compiler macros; every chain below tries GNU first, so
 * it gets the builtins rather than the volatile fallback */
#if YAC_CC_MSVC
#include <intrin.h>
#endif

/* shared by every retry loop here and in yac_storage.c: a claimer that died
 * never publishes, so no loop may wait forever */
#define YAC_MAX_SPIN 30

/* cpu hints: cuts the power and pipeline cost of spinning on x86 and yields
 * the SMT sibling's issue slots; a no-op where there is no hint instruction */
static inline void yac_cpu_relax(void) {
#if YAC_CC_GNU && YAC_ARCH_X86
	__asm__ volatile ("pause" ::: "memory");
#elif YAC_CC_GNU && YAC_ARCH_ARM64
	__asm__ volatile ("yield" ::: "memory");
#elif YAC_CC_MSVC && YAC_ARCH_X86
	_mm_pause();
#elif YAC_CC_MSVC && YAC_ARCH_ARM64
	__yield();
#endif
}

static inline void yac_prefetch(const void *p) {
#if YAC_CC_GNU
	/* locality 3 (keep in all levels): the block header is read within a
	 * few instructions, so it must land in L1 to be useful */
	__builtin_prefetch(p, 0, 3);
#elif YAC_CC_MSVC && YAC_ARCH_ARM64
	__prefetch(p);
#elif YAC_CC_MSVC && YAC_ARCH_X86
	_mm_prefetch((const char *)p, _MM_HINT_T0);
#endif
}

/* shared-memory access, one chain per compiler. YAC_LOAD/YAC_STORE must be
 * plain: an access that cannot be elided or merged, but takes no cache line
 * ownership -- never use an RMW there. configure rejects a toolchain that
 * cannot provide all of this */
#if YAC_CC_GNU && defined(__ATOMIC_RELAXED)
# define YAC_SLOT_V                    /* type-generic builtins need none */
# define YAC_LOAD(p)           __atomic_load_n((p), __ATOMIC_RELAXED)
# define YAC_STORE(p, v)       __atomic_store_n((p), (v), __ATOMIC_RELAXED)
# define YAC_SEQ_LOAD(p)       __atomic_load_n((p), __ATOMIC_ACQUIRE)
# define YAC_SEQ_STORE(p, v)   __atomic_store_n((p), (v), __ATOMIC_RELEASE)
# define YAC_READ_BARRIER()    __atomic_thread_fence(__ATOMIC_ACQUIRE)
# define YAC_WRITE_BARRIER()   __atomic_thread_fence(__ATOMIC_RELEASE)
# define YAC_CAS(p, old, set)    __sync_bool_compare_and_swap((p), (old), (set))
# define YAC_ATOMIC_ADD(p, val)  __sync_fetch_and_add((p), (val))

#elif YAC_CC_MSVC
/* no generic load/store builtin and no inline asm on x64/ARM64, but a
 * volatile access already emits the plain MOV/LDR and may not be merged.
 * qualifying the slot pointer once lets the fields keep their own widths */
# define YAC_SLOT_V            volatile
# define YAC_LOAD(p)           (*(p))
# define YAC_STORE(p, v)       (*(p) = (v))
# if YAC_ARCH_ARM64
/* /volatile:iso is the default here, so the barriers are real dmb */
#  define YAC_READ_BARRIER()   __dmb(_ARM64_BARRIER_ISHLD)
#  define YAC_WRITE_BARRIER()  __dmb(_ARM64_BARRIER_ISH)
# else
/* TSO keeps load-load and store-store in order, so only the compiler needs
 * holding back; this does not rely on /volatile:ms */
#  define YAC_READ_BARRIER()   _ReadWriteBarrier()
#  define YAC_WRITE_BARRIER()  _ReadWriteBarrier()
# endif
# define YAC_SEQ_LOAD(p)       yac_seq_load(p)
# define YAC_SEQ_STORE(p, v)   do { YAC_WRITE_BARRIER(); YAC_STORE(p, v); } while (0)
# define YAC_CAS(p, old, set) \
	(InterlockedCompareExchange((LONG volatile *)(p), (LONG)(set), (LONG)(old)) == (LONG)(old))
# define YAC_ATOMIC_ADD(p, val)  InterlockedExchangeAdd((LONG volatile *)(p), (LONG)(val))

static inline unsigned int yac_seq_load(const volatile unsigned int *seq) {
	unsigned int s = YAC_LOAD(seq);

	YAC_READ_BARRIER();
	return s;
}

#else
#error No atomic support: yac needs the GNU __atomic/__sync builtins or MSVC
#endif

/* versioned slot: seq even means stable, odd means a writer is publishing.
 * readers never write it, so a hot key costs no cache line ownership. zero
 * is even, hence a zeroed slot array reads as stable and flush() stays one
 * memset */

/* only the claimer advances an odd counter, so publishing needs no RMW.
 * returns 0 after YAC_MAX_SPIN attempts, as the old mutex did */
static inline int yac_slot_claim(YAC_SLOT_V unsigned int *seq) {
	int retry = 0;

	for (;;) {
		unsigned int s = YAC_LOAD(seq);

		/* cast away the qualifier: the CAS is the atomicity, and MSVC's
		 * Interlocked path needs its own parameter type */
		if (!(s & 1) && YAC_CAS((unsigned int *)seq, s, s + 1)) {
			return 1;
		}
		if (++retry == YAC_MAX_SPIN) {
			return 0;
		}
		yac_cpu_relax();
	}
}

static inline void yac_slot_publish(YAC_SLOT_V unsigned int *seq) {
	/* rounds up rather than adds one: flush()'s memset can turn the counter
	 * even underneath the claimer, and +1 would then wedge the slot odd */
	YAC_SEQ_STORE(seq, (YAC_LOAD(seq) | 1) + 1);
}

#define YAC_SLOT_CLAIM(p)     yac_slot_claim(&(p)->seq)
#define YAC_SLOT_PUBLISH(p)   yac_slot_publish(&(p)->seq)

#endif /* YAC_ATOMIC_H */
