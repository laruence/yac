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
  |         John Neo <nhf0424@gmail.com>                                 |
  +----------------------------------------------------------------------+
*/

#ifndef YAC_ATOMIC_H
#define YAC_ATOMIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
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
#else
#undef YAC_CAS
#endif

#ifdef YAC_CAS

#define	MUT_READ      0x0
#define	MUT_WRITE     0x1
#define CAS_MAX_SPIN  100

static inline int yac_mutex_write(unsigned int *me) {
	int retry = 0;
	while (!YAC_CAS(me, MUT_READ, MUT_WRITE)) {
		if (++retry == CAS_MAX_SPIN)  {
			return 0;
		}
	}
	return 1;
}

static inline void yac_mutex_read(unsigned int *me) {
	*me = MUT_READ;
}

#define	WRITEP(P)   yac_mutex_write(&(P->mutex))
#define	READP(P)    yac_mutex_read(&(P->mutex))
#else
#warn  No atomic supports
#undef YAC_CAS
#define WRITEP(P)   (1)
#define READP(P)
#endif

#endif
