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

#if defined(_WIN32)
# include <intrin.h>					/* _mm_pause() / __yield() */
#endif

#if HAVE_BUILTIN_ATOMIC
#define	YAC_CAS(lock, old, set)  __sync_bool_compare_and_swap(lock, old, set)
#elif ( __amd64__ || __amd64 || __x86_64__ || __i386__ || __i386 )
static inline int __yac_cas(unsigned int *lock, unsigned int old, unsigned int set) {
	unsigned char res;

	__asm__ volatile ( "lock;" "cmpxchgl %3, %1;" "sete %0;" :
		"=a" (res) : "m" (*lock), "a" (old), "r" (set) : "memory");

	return res;
}
#define	YAC_CAS(lock, old, set)  __yac_cas(lock, old, set)
#elif ZEND_WIN32
#define	YAC_CAS(lock, old, set)  (InterlockedCompareExchange(lock, set, old) == old)
#else
#error No atomic CAS support: per-slot locking is mandatory for a shared-memory cache
#endif

/* FREE must be 0: yac_slot_unlock() uses __sync_lock_release(), which stores 0. */
#define	YAC_SLOT_FREE    0x0
#define	YAC_SLOT_LOCKED  0x1
#define	YAC_CAS_MAX_SPIN 30

/* backoff hint for the spin loop: cuts power and the pipeline penalty
 * of spinning on x86, and yields the SMT sibling's issue slots;
 * a no-op on architectures without a hint instruction */
static inline void yac_cpu_relax(void) {
#if defined(_WIN32)
# if defined(_M_X64) || defined(_M_IX86)
	_mm_pause();
# elif defined(_M_ARM64)
	__yield();
# endif
#elif defined(__GNUC__) || defined(__clang__)
# if defined(__x86_64__) || defined(__amd64__) || defined(__i386__)
	__asm__ volatile ("pause" ::: "memory");
# elif defined(__aarch64__)
	__asm__ volatile ("yield" ::: "memory");
# endif
#endif
}

static inline void yac_prefetch(const void *p) {
#if defined(__GNUC__) || defined(__clang__)
	/* locality 3 (keep in all levels): the block header is read within a
	 * few instructions, so it must land in L1 to be useful */
	__builtin_prefetch(p, 0, 3);
#elif defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86) || defined(_M_ARM64))
# if defined(_M_ARM64)
	__prefetch(p);
# else
	_mm_prefetch((const char *)p, _MM_HINT_T0);
# endif
#endif
}

static inline int yac_slot_lock(unsigned int *me) {
	int retry = 0;
	while (!YAC_CAS(me, YAC_SLOT_FREE, YAC_SLOT_LOCKED)) {
		if (++retry == YAC_CAS_MAX_SPIN)  {
			return 0;
		}
		yac_cpu_relax();
	}
	return 1;
}

static inline void yac_slot_unlock(unsigned int *me) {
#if HAVE_BUILTIN_ATOMIC
	__sync_lock_release(me);                        /* release store of 0 (== FREE) */
#elif ZEND_WIN32
	InterlockedExchange((LONG volatile *)me, YAC_SLOT_FREE);
#else
	__asm__ volatile ("" ::: "memory");             /* compiler barrier */
	*me = YAC_SLOT_FREE;                            /* x86: plain store is store-ordered */
#endif
}

#define	WRITEP(P)   yac_slot_lock(&(P->mutex))
#define	READP(P)    yac_slot_unlock(&(P->mutex))

#endif /* YAC_ATOMIC_H */
