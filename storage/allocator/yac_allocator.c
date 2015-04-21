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

#include <errno.h>
#include <time.h>
#include <sys/types.h>

#include "php.h"
#include "storage/yac_storage.h"
#include "yac_allocator.h"

static const yac_shared_memory_handlers *shared_memory_handler = NULL;
static const char *shared_model;

int yac_allocator_startup(unsigned long k_size, unsigned long size, char **msg) /* {{{ */ {
	char *p;
	yac_shared_segment *segments = NULL;
	int i, segments_num, segments_array_size, segment_size;
	const yac_shared_memory_handlers *he;

	if ((he = &yac_shared_memory_handler)) {
		int ret = he->create_segments(k_size, size, &segments, &segments_num, msg);

		if (!ret) {
			if (segments) {
				int i;
				for (i = 0; i < segments_num; i++) {
					if (segments[i].p && segments[i].p != (void *)-1) {
						he->detach_segment(&segments[i]);
					}
				}
				free(segments);
			}
			return 0;
		}
	} else {
		return 0;
	}

	segment_size = he->segment_type_size();
	segments_array_size = (segments_num - 1) * segment_size;

	yac_storage = segments[0].p;
	memcpy(&YAC_SG(first_seg), (char *)(&segments[0]), segment_size);

	YAC_SG(segments_num) 		= segments_num - 1;
	YAC_SG(segments_num_mask) 	= YAC_SG(segments_num) - 1;
	YAC_SG(segments)     		= (yac_shared_segment **)((char *)yac_storage + YAC_SMM_ALIGNED_SIZE(sizeof(yac_storage_globals) + segment_size - sizeof(yac_shared_segment)));

	p = (char *)YAC_SG(segments) + (sizeof(void *) * YAC_SG(segments_num));
	memcpy(p, (char *)segments + segment_size, segments_array_size);
	for (i = 0; i < YAC_SG(segments_num); i++) {
		YAC_SG(segments)[i] = (yac_shared_segment *)p;
		p += segment_size;
	}
	YAC_SG(slots) = (yac_kv_key *)((char *)YAC_SG(segments)
			+ (YAC_SG(segments_num) * sizeof(void *)) + YAC_SMM_ALIGNED_SIZE(segments_array_size));

	free(segments);

	return 1;
}
/* }}} */

void yac_allocator_shutdown(void) /* {{{ */ {
	yac_shared_segment **segments;
	const yac_shared_memory_handlers *he;

	segments = YAC_SG(segments);
	if (segments) {
		if ((he = &yac_shared_memory_handler)) {
			int i = 0;
			for (i = 0; i < YAC_SG(segments_num); i++) {
				he->detach_segment(segments[i]);
			}
			he->detach_segment(&YAC_SG(first_seg));
		}
	}
}
/* }}} */

static inline void *yac_allocator_alloc_algo2(unsigned long size, int hash) /* {{{ */ {
	yac_shared_segment *segment;
	unsigned int seg_size, retry, pos, current;

	current = hash & YAC_SG(segments_num_mask);
	/* do we really need lock here? it depends the real life exam */
	retry = 3;
do_retry:
	segment = YAC_SG(segments)[current];
	seg_size = segment->size;
	pos = segment->pos;
	if ((seg_size - pos) >= size) {
do_alloc:
		pos += size;
		segment->pos = pos;
		if (segment->pos == pos) {
			return (void *)((char *)segment->p + (pos - size));
		} else if (retry--) {
			goto do_retry;
		}
		return NULL;
    } else { 
		int i, max;
		max = (YAC_SG(segments_num) > 4)? 4 : YAC_SG(segments_num);
		for (i = 1; i < max; i++) {
			segment = YAC_SG(segments)[(current + i) & YAC_SG(segments_num_mask)];
			seg_size = segment->size;
			pos = segment->pos;
			if ((seg_size - pos) >= size) {
				current = (current + i) & YAC_SG(segments_num_mask);
				goto do_alloc;
			}
		}
		segment->pos = 0;
		pos = 0;
		++YAC_SG(recycles);
		goto do_alloc;
	}
}
/* }}} */

#if 0
static inline void *yac_allocator_alloc_algo1(unsigned long size) /* {{{ */ {
    int i, j, picked_seg, atime;
    picked_seg = (YAC_SG(current_seg) + 1) & YAC_SG(segments_num_mask);

    atime = YAC_SG(segments)[picked_seg]->atime;
    for (i = 0; i < 10; i++) {
        j = (picked_seg + 1) & YAC_SG(segments_num_mask);
        if (YAC_SG(segments)[j]->atime < atime) {
            picked_seg = j;
            atime = YAC_SG(segments)[j]->atime;
        }
    }

    YAC_SG(current_seg) = picked_seg;
    YAC_SG(segments)[picked_seg]->pos = 0;
    return yac_allocator_alloc_algo2(size);
}
/* }}} */
#endif

unsigned long yac_allocator_real_size(unsigned long size) /* {{{ */ {
	unsigned long real_size = YAC_SMM_TRUE_SIZE(size);

	if (real_size > YAC_SG(segments)[0]->size) {
		return 0;
	}

	return real_size;
}
/* }}} */

void * yac_allocator_raw_alloc(unsigned long real_size, int hash) /* {{{ */ {

	return yac_allocator_alloc_algo2(real_size, hash);
	/*
    if (YAC_SG(exhausted)) {
        return yac_allocator_alloc_algo1(real_size);
    } else {
        void *p;
        if ((p = yac_allocator_alloc_algo2(real_size))) {
            return p;
        }
        return yac_allocator_alloc_algo1(real_size);
    }
	*/
}
/* }}} */

#if 0
void yac_allocator_touch(void *p, unsigned long atime) /* {{{ */ {
	yac_shared_block_header h = *(yac_shared_block_header *)(p - sizeof(yac_shared_block_header));

	if (h.seg >= YAC_SG(segments_num)) {
		return;
	}
	
	YAC_SG(segments)[h.seg]->atime = atime;
}
/* }}} */
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
