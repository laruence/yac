#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <sched.h>
#include <sys/mman.h>

#include "yac_atomic.h"
#include "yac_malloc.h"
#include "yac_lock.h"

#define	MUT_UNLOCKED	0
#define	MUT_LOCKED		1

yac_mutexarray_t *yac_mutexarray_new(int num)
{
	int i;
	yac_mutexarray_t *obj;

	if (num<1 || num>YAC_MUTEXARRAY_SIZE_MAX) {
		return NULL;
	}
//	obj=USER_ALLOC(sizeof(yac_mutexarray_t)+sizeof(int)*(num-1));
	obj=mmap(NULL, sizeof(yac_mutexarray_t)+sizeof(int)*(num-1), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (obj==NULL) {
		return NULL;
	}
	obj->nelms = num;
	for (i=0;i<obj->nelms;++i) {
		obj->elm[i] = MUT_UNLOCKED;
	}
	return obj;
}

void yac_mutexarray_delete(yac_mutexarray_t *l)
{
	//fprintf(stderr, "yac_mutexarray_delete(%p)\n", l);
	if (l!=NULL) {
		//USER_FREE(;);
		munmap(l, sizeof(yac_mutexarray_t)+sizeof(int)*(l->nelms-1));
	}
}

int yac_mutexarray_init(yac_mutexarray_t *me)
{
	int i;

	if (me!=NULL) {
		if (me->nelms<0 || me->nelms>YAC_MUTEXARRAY_SIZE_MAX) {
			return -1;
		}
		for (i=0;i<me->nelms;++i) {
			me->elm[i] = MUT_UNLOCKED;
		}
	}
	return 0;
}

void yac_mutexarray_destroy(yac_mutexarray_t *me)
{
//	fprintf(stderr, "yac_mutexarray_destroy(%p)\n", me);
}

int yac_mutex_lock(yac_mutexarray_t *me, int sub)
{
//	fprintf(stderr, "Lock(%d) of 0x%p\n", sub, me);
	if (me!=NULL && me->nelms>0) {
		while (!YAC_CAS(&me->elm[sub], MUT_UNLOCKED, MUT_LOCKED)) sched_yield();
	}
	return 0;
}

int yac_mutex_locknb(yac_mutexarray_t *me, int sub)
{
	if (me!=NULL && me->nelms>0) {
		return YAC_CAS(&me->elm[sub], MUT_UNLOCKED, MUT_LOCKED)?0:1;
	}
	return 0;
}

int yac_mutex_unlock(yac_mutexarray_t *me, int sub)
{
	if (me!=NULL && me->nelms>0) {
		me->elm[sub] = MUT_UNLOCKED;
	}
	return 0;
}

