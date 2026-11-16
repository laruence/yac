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
   |          Dmitry Stogov <dmitry@zend.com>                             |
   |          Wei Dai <demon@php.net>                                     |
   +----------------------------------------------------------------------+
*/

#include "php.h"

#include "php_yac.h"
#include "storage/yac_storage.h"
#include "storage/allocator/yac_allocator.h"

#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <Lmcons.h>

#define ACCEL_FILEMAP_NAME "Yac.SharedMemoryArea"
#define ACCEL_FILEMAP_MUTEX "Yac.SharedMemoryArea.Init"
#define YAC_FILE_MAPPING_MAGIC 0x59414346u
#define YAC_FILE_MAPPING_LAYOUT_VERSION 1
#define YAC_FILE_MAPPING_HEADER_SIZE 64
#define YAC_FILE_MAPPING_INITIALIZING 1
#define YAC_FILE_MAPPING_READY 2

#if defined(YAC_DEBUG)
# define YAC_FILE_MAPPING_DEBUG 1
#else
# define YAC_FILE_MAPPING_DEBUG 0
#endif

typedef struct {
	uint32_t magic;
	uint32_t layout_version;
	uint32_t pointer_bits;
	uint32_t debug;
	uint32_t keys_memory_size;
	uint32_t values_memory_size;
	uint64_t mapping_base;
	volatile LONG state;
} yac_file_mapping_header;

typedef char yac_file_mapping_header_fits[
		(sizeof(yac_file_mapping_header) <= YAC_FILE_MAPPING_HEADER_SIZE) ? 1 : -1];

static HANDLE memfile = NULL;
static HANDLE init_mutex = NULL;
static void *mapping_base;
static int mapping_attached;
static int init_mutex_owned;

#ifdef USE_FILE_MAPPING
static char *create_name_with_username(const char *name) /* {{{ */ {
	static char newname[MAXPATHLEN + UNLEN + 4];
	char uname[UNLEN + 1];
	DWORD unsize = UNLEN + 1;

	if (!GetUserName(uname, &unsize)) {
		return NULL;
	}
	snprintf(newname, sizeof(newname), "%s@%s", name, uname);
	return newname;
}
/* }}} */

static void release_init_mutex(void) /* {{{ */ {
	if (init_mutex_owned) {
		ReleaseMutex(init_mutex);
		init_mutex_owned = 0;
	}
	if (init_mutex) {
		CloseHandle(init_mutex);
		init_mutex = NULL;
	}
}
/* }}} */

static int acquire_init_mutex(char **error_in) /* {{{ */ {
	DWORD wait_result;
	char *name = create_name_with_username(ACCEL_FILEMAP_MUTEX);

	if (!name) {
		*error_in = "GetUserName";
		return 0;
	}
	init_mutex = CreateMutex(NULL, FALSE, name);
	if (!init_mutex) {
		*error_in = "CreateMutex";
		return 0;
	}
	wait_result = WaitForSingleObject(init_mutex, INFINITE);
	if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
		CloseHandle(init_mutex);
		init_mutex = NULL;
		*error_in = "WaitForSingleObject";
		return 0;
	}
	init_mutex_owned = 1;
	return 1;
}
/* }}} */

static void reset_mapping_state(void) /* {{{ */ {
	mapping_base = NULL;
	mapping_attached = 0;
}
/* }}} */

static void close_mapping(void) /* {{{ */ {
	if (mapping_base) {
		UnmapViewOfFile(mapping_base);
	}
	if (memfile) {
		CloseHandle(memfile);
		memfile = NULL;
	}
	reset_mapping_state();
}
/* }}} */

static void *mapping_data_base(void) /* {{{ */ {
	return (char *)mapping_base + YAC_FILE_MAPPING_HEADER_SIZE;
}
/* }}} */

static int read_mapping_header(yac_file_mapping_header *header, char **error_in) /* {{{ */ {
	void *view;

	view = MapViewOfFile(memfile, FILE_MAP_READ, 0, 0,
			YAC_FILE_MAPPING_HEADER_SIZE);
	if (!view) {
		*error_in = "MapViewOfFile header";
		return 0;
	}
	memcpy(header, view, sizeof(*header));
	UnmapViewOfFile(view);
	MemoryBarrier();
	return 1;
}
/* }}} */

static int header_matches(const yac_file_mapping_header *header,
		unsigned long keys_memory_size, unsigned long values_memory_size) /* {{{ */ {
	return header->magic == YAC_FILE_MAPPING_MAGIC &&
		header->layout_version == YAC_FILE_MAPPING_LAYOUT_VERSION &&
		header->pointer_bits == sizeof(void *) * 8 &&
		header->debug == YAC_FILE_MAPPING_DEBUG &&
		header->keys_memory_size == (uint32_t)keys_memory_size &&
		header->values_memory_size == (uint32_t)values_memory_size;
}
/* }}} */

static void initialize_mapping_header(unsigned long keys_memory_size,
		unsigned long values_memory_size) /* {{{ */ {
	yac_file_mapping_header *header = mapping_base;

	memset(header, 0, YAC_FILE_MAPPING_HEADER_SIZE);
	header->magic = YAC_FILE_MAPPING_MAGIC;
	header->layout_version = YAC_FILE_MAPPING_LAYOUT_VERSION;
	header->pointer_bits = sizeof(void *) * 8;
	header->debug = YAC_FILE_MAPPING_DEBUG;
	header->keys_memory_size = (uint32_t)keys_memory_size;
	header->values_memory_size = (uint32_t)values_memory_size;
	header->mapping_base = (uint64_t)(uintptr_t)mapping_base;
	MemoryBarrier();
	header->state = YAC_FILE_MAPPING_INITIALIZING;
}
/* }}} */

int yac_file_mapping_is_attached(void) /* {{{ */ {
	return mapping_attached;
}
/* }}} */

void yac_file_mapping_finish_startup(void) /* {{{ */ {
	yac_file_mapping_header *header = mapping_base;

	MemoryBarrier();
	InterlockedExchange(&header->state, YAC_FILE_MAPPING_READY);
	release_init_mutex();
}
/* }}} */

static int create_segments(unsigned long k_size, unsigned long v_size, yac_shared_segment **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	unsigned long keys_memory_size = k_size, values_memory_size = v_size;
	unsigned long allocate_size, mapping_size, occupied_size;
	unsigned int i, segment_size, segments_num;
	yac_shared_segment first_segment;
	char *mapping_name;
#if defined(_WIN64)
	void *mapping_bases[] = {
		(void *)0x0000100000000000, (void *)0x0000200000000000,
		(void *)0x0000300000000000, (void *)0x0000700000000000, NULL
	};
#else
	void *mapping_bases[] = {
		(void *)0x20000000, (void *)0x21000000, (void *)0x30000000,
		(void *)0x31000000, (void *)0x50000000, NULL
	};
#endif
	void **mapping_base_candidate;

	*shared_segments_p = NULL;
	*shared_segments_count = 0;
	mapping_attached = 0;

	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	segments_num = yac_allocator_target_segments(v_size);
	k_size = yac_allocator_key_size(k_size, segments_num);
	segment_size = YAC_SMM_SEGMENT_SIZE;
	allocate_size = k_size + (unsigned long)segments_num * segment_size;
	mapping_size = YAC_FILE_MAPPING_HEADER_SIZE + allocate_size;

	if (!acquire_init_mutex(error_in)) {
		return 0;
	}

	mapping_name = create_name_with_username(ACCEL_FILEMAP_NAME);
	if (!mapping_name) {
		*error_in = "GetUserName";
		release_init_mutex();
		return 0;
	}
	memfile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
			0, mapping_size, mapping_name);
	if (!memfile) {
		*error_in = "CreateFileMapping";
		release_init_mutex();
		return 0;
	}

	mapping_attached = GetLastError() == ERROR_ALREADY_EXISTS;
	if (mapping_attached) {
		yac_file_mapping_header header;
		void *wanted_mapping_base;

		if (!read_mapping_header(&header, error_in)) {
			close_mapping();
			release_init_mutex();
			return 0;
		}
		if (!header_matches(&header, keys_memory_size, values_memory_size) ||
				header.state != YAC_FILE_MAPPING_READY ||
				header.mapping_base > UINTPTR_MAX) {
			*error_in = "Yac shared memory layout does not match this PHP process";
			close_mapping();
			release_init_mutex();
			return 0;
		}
		wanted_mapping_base = (void *)(uintptr_t)header.mapping_base;
		mapping_base = MapViewOfFileEx(memfile, FILE_MAP_ALL_ACCESS, 0, 0, 0,
				wanted_mapping_base);
		if (mapping_base != wanted_mapping_base) {
			*error_in = "MapViewOfFileEx reattach";
			close_mapping();
			release_init_mutex();
			return 0;
		}
	} else {
		for (mapping_base_candidate = mapping_bases;
				*mapping_base_candidate && !mapping_base;
				mapping_base_candidate++) {
			mapping_base = MapViewOfFileEx(memfile, FILE_MAP_ALL_ACCESS, 0, 0, 0,
					*mapping_base_candidate);
		}
		if (!mapping_base) {
			*error_in = "MapViewOfFileEx";
			close_mapping();
			release_init_mutex();
			return 0;
		}
		initialize_mapping_header(keys_memory_size, values_memory_size);
	}

	first_segment.p = mapping_data_base();
	first_segment.reserved = allocate_size;
	first_segment.size = k_size;

	*shared_segments_p = (yac_shared_segment *)calloc(1,
			(segments_num + 1) * sizeof(yac_shared_segment));
	if (!*shared_segments_p) {
		*error_in = "calloc";
		close_mapping();
		release_init_mutex();
		return 0;
	}
	*shared_segments_count = segments_num + 1;
	(*shared_segments_p)[0] = first_segment;

	occupied_size = k_size;
	for (i = 0; i < segments_num; i++) {
		yac_shared_segment *segment = &(*shared_segments_p)[i + 1];

		segment->p = (char *)mapping_data_base() + occupied_size;
		segment->size = segment_size;
		occupied_size += segment_size;
	}

	if (mapping_attached) {
		release_init_mutex();
	}
	return 1;
}
/* }}} */

static int detach_segment(yac_shared_segment *shared_segment) /* {{{ */ {
	if (mapping_base && shared_segment->reserved &&
			shared_segment->p == mapping_data_base()) {
		close_mapping();
		release_init_mutex();
	}
	return 0;
}
/* }}} */

yac_shared_memory_handlers yac_alloc_create_file_handlers = /* {{{ */ {
	create_segments,
	detach_segment
};
/* }}} */
#endif /* USE_FILE_MAPPING */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4 fdm=marker
 */
