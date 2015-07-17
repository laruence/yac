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

typedef struct  {
	yac_shared_segment common;
	unsigned long size;
} yac_shared_segment_mmap;

static int create_segments(unsigned long k_size, unsigned long v_size, yac_shared_segment_mmap **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	unsigned long allocate_size, occupied_size =  0;
	unsigned int i, segment_size, segments_num = 1024;
	yac_shared_segment_mmap first_segment;

	k_size = YAC_SMM_ALIGNED_SIZE(k_size);
	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	while ((v_size / segments_num) < YAC_SMM_SEGMENT_MIN_SIZE) {
		segments_num >>= 1;
	}

	segment_size = v_size / segments_num;
	++segments_num;

	allocate_size = k_size + v_size;

	first_segment.common.p = mmap(0, allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (first_segment.common.p == MAP_FAILED) {
		*error_in = "mmap";
		return 0;
	}
	first_segment.size = allocate_size;
	first_segment.common.size = k_size;
	first_segment.common.pos = 0;

	*shared_segments_p = (yac_shared_segment_mmap *)calloc(1, segments_num * sizeof(yac_shared_segment_mmap));
	if (!*shared_segments_p) {
		munmap(first_segment.common.p, first_segment.size);
		*error_in = "calloc";
		return 0;
	} else {
		*shared_segments_p[0] = first_segment;
	}
	*shared_segments_count = segments_num;

	occupied_size = k_size;
	for (i = 1; i < segments_num; i++) {
		(*shared_segments_p)[i].size = 0;
		(*shared_segments_p)[i].common.pos = 0;
		(*shared_segments_p)[i].common.p = first_segment.common.p + occupied_size;
		if ((allocate_size - occupied_size) >= YAC_SMM_ALIGNED_SIZE(segment_size)) {
			(*shared_segments_p)[i].common.size = YAC_SMM_ALIGNED_SIZE(segment_size);
			occupied_size += YAC_SMM_ALIGNED_SIZE(segment_size);
		} else {
			(*shared_segments_p)[i].common.size = (allocate_size - occupied_size);
			break;
		}
	}

	return 1;
}
/* }}} */

static int detach_segment(yac_shared_segment *shared_segment) /* {{{ */ {
	if (shared_segment->size) {
		munmap(shared_segment->p, shared_segment->size);
	}
	return 0;
}
/* }}} */

static unsigned long segment_type_size(void) /* {{{ */ {
	return sizeof(yac_shared_segment_mmap);
}
/* }}} */

yac_shared_memory_handlers yac_alloc_mmap_handlers = /* {{{ */ {
	(create_segments_t)create_segments,
	detach_segment,
	segment_type_size
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
