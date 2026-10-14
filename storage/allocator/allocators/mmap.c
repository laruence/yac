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
	unsigned long allocate_size, occupied_size;
	unsigned int i, segment_size, segments_num;
	void *mapping;
	yac_shared_segment first_segment;

	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	segments_num = yac_allocator_target_segments(v_size);
	k_size = yac_allocator_key_size(k_size, segments_num);
	segment_size = YAC_SMM_SEGMENT_SIZE;
	allocate_size = k_size + (unsigned long)segments_num * segment_size;

	mapping = mmap(0, allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		*error_in = "mmap";
		return 0;
	}
	first_segment.p = mapping;
	first_segment.reserved = allocate_size;
	first_segment.size = k_size;

	*shared_segments_p = (yac_shared_segment *)calloc(1, (segments_num + 1) * sizeof(yac_shared_segment));
	if (!*shared_segments_p) {
		munmap(mapping, allocate_size);
		*error_in = "calloc";
		return 0;
	}
	(*shared_segments_p)[0] = first_segment;
	*shared_segments_count = segments_num + 1;

	occupied_size = k_size;
	for (i = 0; i < segments_num; i++) {
		yac_shared_segment *segment = &(*shared_segments_p)[i + 1];

		/* Slices share the first mapping; only first_segment owns its unmap. */
		segment->reserved = 0;
		segment->p = (char *)mapping + occupied_size;
		segment->size = segment_size;
		occupied_size += segment_size;
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
