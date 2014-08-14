#ifndef YAC_LOCK_H
#define YAC_LOCK_H

typedef struct {
    int nelms;
    int elm[1];
} yac_mutexarray_t;

#define	YAC_MUTEXARRAY_SIZE_MAX	(1024*1024)

//yac_mutexarray_t *yac_mutexarray_new(int num);
//void yac_mutexarray_delete(yac_mutexarray_t*);

int yac_mutexarray_init(yac_mutexarray_t*);
void yac_mutexarray_destroy(yac_mutexarray_t*);

int yac_mutex_lock(yac_mutexarray_t*, int sub);
int yac_mutex_locknb(yac_mutexarray_t*, int sub);
int yac_mutex_unlock(yac_mutexarray_t*, int sub);

#endif
