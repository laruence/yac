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
   |          Wei Dai     <zxcvdavid@gmail.com>                           |
   +----------------------------------------------------------------------+
*/

#include "storage/yac_storage.h"
#include "storage/allocator/yac_allocator.h"

#ifdef USE_FILE_MAPPING

#define ACCEL_FILEMAP_NAME "Yac.SharedMemoryArea"
#define ACCEL_FILEMAP_BASE "Yac.MemoryBase"

#include <stdio.h>
#include <windows.h>
#include <Lmcons.h>

static HANDLE memfile = NULL;
static void *mapping_base;

#define MAX_MAP_RETRIES 25

typedef struct  {
	yac_shared_segment common;
	unsigned long size;
} yac_shared_segment_create_file;

static char *create_name_with_username(char *name)
{
	static char newname[MAXPATHLEN + UNLEN + 4];
	char uname[UNLEN + 1];
	DWORD unsize = UNLEN;

	GetUserName(uname, &unsize);
	snprintf(newname, sizeof(newname) - 1, "%s@%s", name, uname);
	return newname;
}

static char *get_mmap_base_file(void) {
	static char windir[MAXPATHLEN+UNLEN + 3 + sizeof("\\\\@")];
	char uname[UNLEN + 1];
	DWORD unsize = UNLEN;
	int l;

	GetTempPath(MAXPATHLEN, windir);
	GetUserName(uname, &unsize);
	l = strlen(windir);
	snprintf(windir + l, sizeof(windir) - l - 1, "\\%s@%s", ACCEL_FILEMAP_BASE, uname);
	return windir;
}

static int yac_shared_alloc_reattach(size_t requested_size, char **error_in)
{
	void *wanted_mapping_base;
	char *mmap_base_file = get_mmap_base_file();
	FILE *fp = fopen(mmap_base_file, "r");
	MEMORY_BASIC_INFORMATION info;

	if (!fp) {
		*error_in="fopen";
		return ALLOC_FAILURE;
	}
	if (!fscanf(fp, "%p", &wanted_mapping_base)) {
		*error_in="read mapping base";
		fclose(fp);
		return ALLOC_FAILURE;
	}
	fclose(fp);

	/* Check if the requested address space is free */
	if (VirtualQuery(wanted_mapping_base, &info, sizeof(info)) == 0 ||
	    info.State != MEM_FREE ||
	    info.RegionSize < requested_size) {
		return ALLOC_FAILURE;
   	}

	mapping_base = MapViewOfFileEx(memfile, FILE_MAP_ALL_ACCESS, 0, 0, 0, wanted_mapping_base);

	if (mapping_base == NULL) {
		return ALLOC_FAIL_MAPPING;
	}

	return SUCCESSFULLY_REATTACHED;
}

static int create_segments(unsigned long k_size, unsigned long v_size, yac_shared_segment_create_file **shared_segments_p, int *shared_segments_count, char **error_in) /* {{{ */ {
	int ret;
	yac_shared_segment *shared_segment;
	unsigned long allocate_size, occupied_size =  0;
	unsigned int i, segment_size, segments_num = 1024;
	int map_retries = 0;
	yac_shared_segment_create_file first_segment;
	void *default_mapping_base_set[] = {0, 0};
	void **wanted_mapping_base = default_mapping_base_set;

	k_size = YAC_SMM_ALIGNED_SIZE(k_size);
	v_size = YAC_SMM_ALIGNED_SIZE(v_size);
	while ((v_size / segments_num) < YAC_SMM_SEGMENT_MIN_SIZE) {
		segments_num >>= 1;
	}

	segment_size = v_size / segments_num;
	++segments_num;

	allocate_size = k_size + v_size;

	do {
		memfile = OpenFileMapping(FILE_MAP_WRITE, 0, create_name_with_username(ACCEL_FILEMAP_NAME));
		if (memfile == NULL) {
			break;
		}

		ret =  yac_shared_alloc_reattach(k_size, error_in);
		if (ret == ALLOC_FAIL_MAPPING) {
			/* Mapping failed, wait for mapping object to get freed and retry */
            CloseHandle(memfile);
			memfile = NULL;
			Sleep(1000 * (map_retries + 1));
		} else {
			return ret;
		}
	} while (++map_retries < MAX_MAP_RETRIES);

	if (map_retries == MAX_MAP_RETRIES) {
		*error_in = "OpenFileMapping";
		return 0;
	}

	*shared_segments_count = segments_num;
	*shared_segments_p = (yac_shared_segment_create_file *)calloc(1, segments_num * sizeof(yac_shared_segment_create_file));
	if(!*shared_segments_p) {
		*error_in = "calloc";
		return 0;
	} else {

	}

	memfile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, allocate_size, create_name_with_username(ACCEL_FILEMAP_NAME));
	if (memfile == NULL) {
		*error_in = "CreateFileMapping";
		return 0;	
	}

	do {
		first_segment.common.p = mapping_base = MapViewOfFileEx(memfile, FILE_MAP_ALL_ACCESS, 0, 0, 0, *wanted_mapping_base);
		if (wanted_mapping_base == NULL) {
			break;
		}
		wanted_mapping_base++;
	} while (!mapping_base);

	if(mapping_base == NULL) {
		*error_in = "MapViewOfFileEx";
		return 0;
	} else {
		char *mmap_base_file = get_mmap_base_file();
		FILE *fp = fopen(mmap_base_file, "w");
		if (!fp) {
			*error_in = "get_mmap_base_file";
			return 0;
		}
		fprintf(fp, "%p\n", mapping_base);
		fclose(fp);
	}

	first_segment.size = allocate_size;
	first_segment.common.size = k_size;
	first_segment.common.pos = 0;

	*shared_segments_p[0] = first_segment;

	occupied_size = k_size;
	for (i = 1; i < segments_num; i++) {
		if (i != segments_num - 1) {
			(*shared_segments_p)[i].size = allocate_size;
		} else {
			(*shared_segments_p)[i].size = 0;
		}
		(*shared_segments_p)[i].common.pos = 0;
		(*shared_segments_p)[i].common.p = (void *)((char *)first_segment.common.p + occupied_size);
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
	if (!shared_segment->size && mapping_base) {
		UnmapViewOfFile(mapping_base);
		CloseHandle(memfile);
	}
	return 0;
}
/* }}} */

void close_handle() {
	
}

static unsigned long segment_type_size(void) /* {{{ */ {
	return sizeof(yac_shared_segment_create_file);
}
/* }}} */

yac_shared_memory_handlers yac_alloc_create_file_handlers = /* {{{ */ {
	create_segments,
	detach_segment,
	segment_type_size
};
/* }}} */

#endif /* USE_CREATE_FILE */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
