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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "storage/yac_storage.h"
#include "storage/yac_atomic.h"
#include "yac_allocator.h"

static inline void yac_alloc_ref_clear(yac_alloc_ref *alloc) {
	alloc->value = NULL;
	alloc->epoch = 0;
	alloc->control = NULL;
}

int yac_allocator_startup(unsigned long k_size, unsigned long size, char **msg) /* {{{ */ {
	char *p;
	yac_shared_segment *segments = NULL;
	int i, segments_num;
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

	if (segments_num < 2 || ((segments_num - 1) & (segments_num - 2))) {
		if (msg) {
			*msg = "Yac requires a power-of-two number of value segments";
		}
		for (i = 0; i < segments_num; i++) {
			he->detach_segment(&segments[i]);
		}
		free(segments);
		return 0;
	}

	segments_num--;
	yac_storage = segments[0].p;
	YAC_SG(first_seg) = segments[0];
	YAC_SG(segments_num) = segments_num;
	YAC_SG(segments_num_mask) = segments_num - 1;
	YAC_SG(max_segment_size) = 0;
	YAC_SG(segments) = (yac_shared_segment **)((char *)yac_storage
			+ YAC_SMM_ALIGNED_SIZE(sizeof(yac_storage_globals)));

	p = (char *)YAC_SMM_CACHE_ALIGNED_SIZE((uintptr_t)YAC_SG(segments) +
			sizeof(void *) * segments_num);
	for (i = 0; i < segments_num; i++) {
		yac_value_segment *value_segment = (yac_value_segment *)p;
		yac_shared_segment *segment = &value_segment->segment;
		yac_segment_control *control = &value_segment->control;

		*segment = segments[i + 1];
		YAC_SEQ_STORE(&control->pos, 0);
		YAC_SEQ_STORE(&control->epoch, 1);
		YAC_SEQ_STORE(&control->gate, 0);
		YAC_SEQ_STORE(&control->writers, 0);
		if (segment->size > YAC_SG(max_segment_size)) {
			YAC_SG(max_segment_size) = segment->size;
		}
		YAC_SG(segments)[i] = segment;
		p += sizeof(*value_segment);
	}
	p = (char *)YAC_SMM_CACHE_ALIGNED_SIZE((uintptr_t)p);
	YAC_SG(slots) = (yac_kv_key *)p;

	if ((unsigned long)(p - (char *)yac_storage) + sizeof(yac_kv_key) > YAC_SG(first_seg).size) {
		if (msg) {
			*msg = "Yac key segment is too small for allocator metadata";
		}
		for (i = 0; i <= segments_num; i++) {
			he->detach_segment(&segments[i]);
		}
		free(segments);
		return 0;
	}

	free(segments);
	return 1;
}
/* }}} */

int yac_allocator_resolve(void *value, unsigned long hash, yac_seg_pos *segpos) /* {{{ */ {
	unsigned int i, first, second, pos, offset;
	unsigned int current = hash & YAC_SG(segments_num_mask);
	unsigned int max = YAC_SG(segments_num) > 4 ? 4 : YAC_SG(segments_num);
	char *p = (char *)value;

	for (i = 0; i < max; i++) {
		yac_shared_segment *segment = YAC_SG(segments)[(current + i) &
			YAC_SG(segments_num_mask)];
		char *payload = yac_allocator_segment_payload(segment);
		yac_segment_control *control;

		if (p < payload || p >= payload + segment->size) {
			continue;
		}
		control = yac_allocator_segment_control(segment);
		offset = (unsigned int)(p - payload);
		first = YAC_SEQ_LOAD(&control->epoch);
		pos = YAC_SEQ_LOAD(&control->pos);
		YAC_READ_BARRIER();
		second = YAC_SEQ_LOAD(&control->epoch);
		if (first != second || first == 0 || pos > segment->size) {
			return 0;
		}
		segpos->epoch = first;
		segpos->pos = pos;
		segpos->offset = offset;
		return 1;
	}
	return 0;
}
/* }}} */

static int yac_allocator_recycle(yac_shared_segment *segment, unsigned int size, yac_alloc_ref *alloc) /* {{{ */ {
	unsigned int epoch, pos;
	yac_segment_control *control = yac_allocator_segment_control(segment);

	if (segment->size < size || !YAC_SEG_CLAIM(&control->gate)) {
		return 0;
	}
	pos = YAC_SEQ_LOAD(&control->pos);
	if (pos > segment->size || segment->size - pos >= size) {
		YAC_SEG_PUBLISH(&control->gate);
		return 0;
	}
	/* Close this epoch before waiting for its writers. */
	YAC_SEQ_STORE(&control->pos, UINT_MAX);
	while (YAC_SEQ_LOAD(&control->writers)) {
		yac_cpu_relax();
	}
	epoch = YAC_SEQ_LOAD(&control->epoch) + 1;
	if (epoch == 0 || epoch == YAC_EPOCH_SAFE_BIT) {
		epoch = 1;
	}
	/* Reserve the first block before reopening. */
	YAC_SEQ_STORE(&control->writers, 1);
	YAC_SEQ_STORE(&control->epoch, epoch);
	YAC_SEQ_STORE(&control->pos, size);
	alloc->value = yac_allocator_segment_payload(segment);
	alloc->epoch = epoch;
	alloc->control = control;
	YAC_SEG_PUBLISH(&control->gate);
	return 1;
}
/* }}} */

int yac_allocator_alloc(unsigned int size, uint64_t hash, yac_alloc_ref *alloc) /* {{{ */ {
	unsigned int pass, i, current, pos, epoch, max, last = 0;
	yac_shared_segment *segment;

	yac_alloc_ref_clear(alloc);
	current = hash & YAC_SG(segments_num_mask);
	max = YAC_SG(segments_num) > 4 ? 4 : YAC_SG(segments_num);
	for (pass = 0; pass < 3; pass++) {
		for (i = 0; i < max; i++) {
			unsigned int index = (current + i) & YAC_SG(segments_num_mask);
			yac_segment_control *control;

			segment = YAC_SG(segments)[index];
			control = yac_allocator_segment_control(segment);
			last = index;
			if (!YAC_SEG_CLAIM(&control->gate)) {
#ifdef YAC_DEBUG
				YAC_ATOMIC_ADD(&YAC_SG(stats.writer_retries), 1);
#endif
				continue;
			}
			epoch = YAC_SEQ_LOAD(&control->epoch);
			pos = YAC_SEQ_LOAD(&control->pos);
			if (pos <= segment->size && segment->size - pos >= size) {
				YAC_ATOMIC_ADD(&control->writers, 1);
				YAC_SEQ_STORE(&control->pos, pos + size);
				alloc->value = (char *)yac_allocator_segment_payload(segment) + pos;
				alloc->epoch = epoch;
				alloc->control = control;
				YAC_SEG_PUBLISH(&control->gate);
				return 1;
			}
			YAC_SEG_PUBLISH(&control->gate);
		}
		if (yac_allocator_recycle(YAC_SG(segments)[last], size, alloc)) {
			return 1;
		}
	}
	return 0;
}
/* }}} */

int yac_allocator_hold(void *value, uint64_t hash, unsigned int epoch, yac_alloc_ref *alloc) /* {{{ */ {
	unsigned int i, current, max;
	char *p = (char *)value;

	yac_alloc_ref_clear(alloc);
	current = hash & YAC_SG(segments_num_mask);
	max = YAC_SG(segments_num) > 4 ? 4 : YAC_SG(segments_num);
	for (i = 0; i < max; i++) {
		yac_shared_segment *segment = YAC_SG(segments)[(current + i) &
			YAC_SG(segments_num_mask)];
		yac_segment_control *control = yac_allocator_segment_control(segment);
		char *payload = yac_allocator_segment_payload(segment);
		unsigned int pos;

		if (p < payload || p >= payload + segment->size || !YAC_SEG_CLAIM(&control->gate)) {
			continue;
		}
		pos = YAC_SEQ_LOAD(&control->pos);
		if (YAC_SEQ_LOAD(&control->epoch) == epoch && pos <= segment->size &&
				pos > (unsigned int)(p - payload)) {
			YAC_ATOMIC_ADD(&control->writers, 1);
			alloc->epoch = epoch;
			alloc->control = control;
			YAC_SEG_PUBLISH(&control->gate);
			return 1;
		}
		YAC_SEG_PUBLISH(&control->gate);
	}
	return 0;
}
/* }}} */

void yac_allocator_release(yac_alloc_ref *alloc) /* {{{ */ {
	if (alloc->control) {
		YAC_ATOMIC_ADD(&alloc->control->writers, (unsigned int)-1);
		yac_alloc_ref_clear(alloc);
	}
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

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4 fdm=marker
 */
