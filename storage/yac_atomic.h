#ifndef YAC_ATOMIC_H
#define YAC_ATOMIC_H

#ifdef __GNUC__
#define	YAC_CAS(PTR, OLD, NEW)  __sync_bool_compare_and_swap(PTR, OLD, NEW)
#else
#error This compiler is NOT supported yet!
#endif

#endif

