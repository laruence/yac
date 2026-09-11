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

#ifdef USE_MMAP

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
# define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_FAILED
#define MAP_FAILED (void *)-1
#endif

/* segment->reserved keeps the length of the whole mapping, and only for
 * the segment that owns it, so detach_segment() munmaps exactly once */

static int create_segments(unsigned long k_size, unsigned long v_size, yac_shared_segment **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	unsigned long allocate_size, occupied_size =  0;
	unsigned int i, segment_size, segments_num = 1024;
	yac_shared_segment first_segment;

	k_size = YAC_SMM_ALIGNED_SIZE(k_size);
	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	/* don't shift segments_num to 0, v_size / 0 hangs on ARM */
	while (segments_num > 1 && (v_size / segments_num) < YAC_SMM_SEGMENT_MIN_SIZE) {
		segments_num >>= 1;
	}

	segment_size = v_size / segments_num;
	++segments_num;

	allocate_size = k_size + v_size;

	first_segment.p = mmap(0, allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (first_segment.p == MAP_FAILED) {
		*error_in = "mmap";
		return 0;
	}
	first_segment.reserved = allocate_size;
	first_segment.size = k_size;
	first_segment.pos = 0;

	*shared_segments_p = (yac_shared_segment *)calloc(1, segments_num * sizeof(yac_shared_segment));
	if (!*shared_segments_p) {
		munmap(first_segment.p, allocate_size);
		*error_in = "calloc";
		return 0;
	} else {
		(*shared_segments_p)[0] = first_segment;
	}
	*shared_segments_count = segments_num;

	occupied_size = k_size;
	for (i = 1; i < segments_num; i++) {
		/* slices of the first mapping, they own nothing to unmap */
		(*shared_segments_p)[i].reserved = 0;
		(*shared_segments_p)[i].pos = 0;
		(*shared_segments_p)[i].p = (char *)first_segment.p + occupied_size;
		if ((allocate_size - occupied_size) >= YAC_SMM_ALIGNED_SIZE(segment_size)) {
			(*shared_segments_p)[i].size = YAC_SMM_ALIGNED_SIZE(segment_size);
			occupied_size += YAC_SMM_ALIGNED_SIZE(segment_size);
		} else {
			(*shared_segments_p)[i].size = (allocate_size - occupied_size);
			break;
		}
	}

	return 1;
}
/* }}} */

static int detach_segment(yac_shared_segment *shared_segment) /* {{{ */ {
	if (shared_segment->reserved) {
		munmap(shared_segment->p, (size_t)shared_segment->reserved);
	}
	return 0;
}
/* }}} */

yac_shared_memory_handlers yac_alloc_mmap_handlers = /* {{{ */ {
	create_segments,
	detach_segment
};
/* }}} */

#endif /* USE_MMAP */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
