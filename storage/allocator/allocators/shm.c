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
   | Authors: Xinchen Hui <laruence@php.net>                              |
   +----------------------------------------------------------------------+
*/

#include "storage/yac_storage.h"
#include "storage/allocator/yac_allocator.h"

#ifdef USE_SHM

#if defined(__FreeBSD__)
# include <machine/param.h>
#endif
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <fcntl.h>

typedef struct  {
    yac_shared_segment common;
    int shm_id;
} yac_shared_segment_shm;

static int create_segments(size_t k_size, size_t v_size, yac_shared_segment_shm **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	struct shmid_ds sds;
	int shm_id, shmget_flags;
	yac_shared_segment_shm *shared_segments, first_segment;
    unsigned int i, j, allocate_size, allocated_num, segments_num, segment_size;

	shmget_flags = IPC_CREAT|SHM_R|SHM_W|IPC_EXCL;
    segments_num = 1024;
    while ((v_size / segments_num) < YAC_SMM_SEGMENT_MIN_SIZE) {
        segments_num >>= 1;
    }
    segment_size = v_size / segments_num;
    allocate_size = YAC_SMM_SEGMENT_MAX_SIZE;

    while ((shm_id = shmget(IPC_PRIVATE, allocate_size, shmget_flags)) < 0) {
        allocate_size >>= 1;
    }

    if (shm_id < 0) {
        /* this should never happen */
        *error_in = "shmget";
        return 0;
    }

    if (allocate_size < YAC_SMM_SEGMENT_MIN_SIZE) {
        /* this should never happen */
        *error_in = "shmget";
        return 0;
    }

    if (k_size <= allocate_size) {
        first_segment.shm_id = shm_id;
        first_segment.common.pos = 0;
        first_segment.common.size = allocate_size;
        first_segment.common.p = shmat(shm_id, NULL, 0);
        shmctl(shm_id, IPC_RMID, &sds);
        if (first_segment.common.p == (void *)-1) {
            *error_in = "shmat";
            return 0;
        }
    } else {
        shmctl(shm_id, IPC_RMID, &sds);
        *error_in = "shmget";
        return 0;
    }

    allocated_num = (v_size % allocate_size)? (v_size / allocate_size) + 1 : (v_size / allocate_size);
    shared_segments = (yac_shared_segment_shm *)calloc(1, (allocated_num) * sizeof(yac_shared_segment_shm));
    if (!shared_segments) {
        *error_in = "calloc";
        return 0;
    }

    for (i = 0; i < allocated_num; i ++) {
        shm_id = shmget(IPC_PRIVATE, allocate_size, shmget_flags);
        if (shm_id == -1) {
            *error_in = "shmget";
            for (j = 0; j < i; j++) {
                shmdt(shared_segments[j].common.p);
            }
            free(shared_segments);
            return 0;
        }
        shared_segments[i].shm_id = shm_id;
        shared_segments[i].common.pos = 0;
        shared_segments[i].common.size = allocate_size;
        shared_segments[i].common.p = shmat(shm_id, NULL, 0);
        shmctl(shm_id, IPC_RMID, &sds);
        if (shared_segments[i].common.p == (void *)-1) {
            *error_in = "shmat";
            for (j = 0; j < i; j++) {
                shmdt(shared_segments[j].common.p);
            }
            free(shared_segments);
            return 0;
        }
    }

    ++segments_num;
    *shared_segments_p = (yac_shared_segment_shm *)calloc(1, segments_num * sizeof(yac_shared_segment_shm));
    if (!*shared_segments_p) {
		free(shared_segments);
        *error_in = "calloc";
        return 0;
    } else {
        *shared_segments_p[0] = first_segment;
    }
    *shared_segments_count = segments_num;

    j = 0;
    for (i = 1; i < segments_num; i++) {
        if (shared_segments[j].common.pos == 0) {
            (*shared_segments_p)[i].shm_id = shared_segments[j].shm_id;
        }

        if ((shared_segments[j].common.size - shared_segments[j].common.pos) >= (2 * YAC_SMM_ALIGNED_SIZE(segment_size))) {
            (*shared_segments_p)[i].common.pos = 0;
            (*shared_segments_p)[i].common.size = YAC_SMM_ALIGNED_SIZE(segment_size);
            (*shared_segments_p)[i].common.p = shared_segments[j].common.p + YAC_SMM_ALIGNED_SIZE(shared_segments[j].common.pos);
            shared_segments[j].common.pos += YAC_SMM_ALIGNED_SIZE(segment_size);
        } else {
            (*shared_segments_p)[i].common.pos = 0;
            (*shared_segments_p)[i].common.size = shared_segments[j].common.size - shared_segments[j].common.pos;
            (*shared_segments_p)[i].common.p = shared_segments[j].common.p + YAC_SMM_ALIGNED_SIZE(shared_segments[j].common.pos);
            j++;
        }
    }

    free(shared_segments);

	return 1;
}
/* }}} */

static int detach_segment(yac_shared_segment_shm *shared_segment) /* {{{ */ {
    if (shared_segment->shm_id) {
        shmdt(shared_segment->common.p);
    }
	return 1;
}
/* }}} */

static size_t segment_type_size(void) /* {{{ */ {
	return sizeof(yac_shared_segment_shm);
}
/* }}} */

yac_shared_memory_handlers yac_alloc_shm_handlers = /* {{{ */ {
	(create_segments_t)create_segments,
	(detach_segment_t)detach_segment,
	segment_type_size
};
/* }}} */

#endif /* USE_SHM */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
