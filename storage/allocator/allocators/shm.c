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

/* segment->reserved keeps the owned attachment size. segment->p always
 * addresses usable payload; controls live in the key shared segment. */

static void detach_created_segments(yac_shared_segment *segments, unsigned int count) {
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (segments[i].p && segments[i].p != (void *)-1) {
			shmdt(segments[i].p);
		}
	}
}

static int create_segments(unsigned long k_size, unsigned long v_size, yac_shared_segment **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	struct shmid_ds sds;
	int shm_id, shmget_flags;
	yac_shared_segment first_segment;
	unsigned long attachment_size, key_attachment_size, payload_size;
	unsigned int i, segment_count;

	shmget_flags = IPC_CREAT|SHM_R|SHM_W|IPC_EXCL;
	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	segment_count = yac_allocator_target_segments(v_size);
	payload_size = YAC_SMM_SEGMENT_SIZE;
	attachment_size = YAC_SMM_SEGMENT_SIZE;
	shm_id = shmget(IPC_PRIVATE, attachment_size, shmget_flags);
	if (shm_id < 0) {
		*error_in = "shmget";
		return 0;
	}
	k_size = yac_allocator_key_size(k_size, segment_count);

	key_attachment_size = k_size > attachment_size ? k_size : attachment_size;
	if (key_attachment_size == attachment_size) {
		first_segment.reserved = attachment_size;
		first_segment.p = shmat(shm_id, NULL, 0);
		shmctl(shm_id, IPC_RMID, &sds);
	} else {
		shmctl(shm_id, IPC_RMID, &sds);
		shm_id = shmget(IPC_PRIVATE, key_attachment_size, shmget_flags);
		if (shm_id < 0) {
			*error_in = "shmget";
			return 0;
		}
		first_segment.reserved = key_attachment_size;
		first_segment.p = shmat(shm_id, NULL, 0);
		shmctl(shm_id, IPC_RMID, &sds);
	}
	if (first_segment.p == (void *)-1) {
		*error_in = "shmat";
		return 0;
	}
	first_segment.size = k_size;

	*shared_segments_p = (yac_shared_segment *)calloc(segment_count + 1, sizeof(yac_shared_segment));
	if (!*shared_segments_p) {
		shmdt(first_segment.p);
		*error_in = "calloc";
		return 0;
	}
	(*shared_segments_p)[0] = first_segment;
	for (i = 0; i < segment_count; i++) {
		yac_shared_segment *segment = &(*shared_segments_p)[i + 1];
		void *attachment;

		shm_id = shmget(IPC_PRIVATE, attachment_size, shmget_flags);
		if (shm_id < 0) {
			*error_in = "shmget";
			detach_created_segments(*shared_segments_p, i + 1);
			free(*shared_segments_p);
			return 0;
		}
		attachment = shmat(shm_id, NULL, 0);
		shmctl(shm_id, IPC_RMID, &sds);
		if (attachment == (void *)-1) {
			*error_in = "shmat";
			detach_created_segments(*shared_segments_p, i + 1);
			free(*shared_segments_p);
			return 0;
		}
		segment->reserved = attachment_size;
		segment->p = attachment;
		segment->size = payload_size;
	}
	*shared_segments_count = segment_count + 1;

	return 1;
}
/* }}} */

static int detach_segment(yac_shared_segment *shared_segment) /* {{{ */ {
	if (shared_segment->reserved) {
		shmdt(shared_segment->p);
	}
	return 1;
}
/* }}} */

yac_shared_memory_handlers yac_alloc_shm_handlers = /* {{{ */ {
	create_segments,
	detach_segment
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
