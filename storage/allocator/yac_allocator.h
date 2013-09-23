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

#ifndef YAC_ALLOCATOR_H
#define YAC_ALLOCATOR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define YAC_SMM_ALIGNMENT           8
#define YAC_SMM_ALIGNMENT_LOG2      3
#define YAC_SMM_ALIGNMENT_MASK      ~(YAC_SMM_ALIGNMENT - 1)
#define YAC_SMM_BLOCK_HEADER_SIZE   YAC_SMM_ALIGNED_SIZE(sizeof(yac_shared_block_header))

#define YAC_SMM_MAIN_SEG_SIZE       (4*1024*1024)
#define YAC_SMM_SEGMENT_MAX_SIZE    (32*1024*1024)
#define YAC_SMM_SEGMENT_MIN_SIZE    (4*1024*1024)
#define YAC_SMM_MIN_BLOCK_SIZE      128
#define YAC_SMM_ALIGNED_SIZE(x)     (((x) + YAC_SMM_ALIGNMENT - 1) & YAC_SMM_ALIGNMENT_MASK)
#define YAC_SMM_TRUE_SIZE(x)        ((x < YAC_SMM_MIN_BLOCK_SIZE)? (YAC_SMM_MIN_BLOCK_SIZE) : (YAC_SMM_ALIGNED_SIZE(x)))

#ifdef PHP_WIN32
#  define USE_FILE_MAPPING	1
#  define inline __inline
#elif defined(HAVE_SHM_MMAP_ANON)
#  define USE_MMAP      1
#elif defined(HAVE_SHM_IPC)
#  define USE_SHM       1
#else 
#error(no builtin shared memory supported)
#endif

#define ALLOC_FAILURE           0
#define ALLOC_SUCCESS           1
#define FAILED_REATTACHED       2
#define SUCCESSFULLY_REATTACHED 4
#define ALLOC_FAIL_MAPPING      8

typedef int (*create_segments_t)(unsigned long k_size, unsigned long v_size, yac_shared_segment **shared_segments, int *shared_segment_count, char **error_in);
typedef int (*detach_segment_t)(yac_shared_segment *shared_segment);

typedef struct {
	create_segments_t create_segments;
	detach_segment_t detach_segment;
	unsigned long (*segment_type_size)(void);
} yac_shared_memory_handlers;

typedef struct {
	const char *name;
	yac_shared_memory_handlers *handler;
} yac_shared_memory_handler_entry;

int yac_allocator_startup(unsigned long first_seg_size, unsigned long size, char **err);
void yac_allocator_shutdown(void);
unsigned long yac_allocator_real_size(unsigned long size);
void *yac_allocator_raw_alloc(unsigned long real_size, int seg);
int  yac_allocator_free(void *p);

static inline void * yac_allocator_alloc(unsigned long size, int seg) {
    unsigned long real_size = yac_allocator_real_size(size);
    if (!real_size) {
        return (void *)0;
    }
    return yac_allocator_raw_alloc(real_size, seg);
}

#if defined(USE_MMAP)
extern yac_shared_memory_handlers yac_alloc_mmap_handlers;
#define yac_shared_memory_handler yac_alloc_mmap_handlers
#define YAC_SHARED_MEMORY_HANDLER_NAME "mmap"
#elif defined(USE_SHM)
extern yac_shared_memory_handlers yac_alloc_shm_handlers;
#define yac_shared_memory_handler yac_alloc_shm_handlers
#define YAC_SHARED_MEMORY_HANDLER_NAME "shm"
#elif defined(USE_FILE_MAPPING)
extern yac_shared_memory_handlers yac_alloc_create_file_handlers;
#define yac_shared_memory_handler yac_alloc_create_file_handlers
#define YAC_SHARED_MEMORY_HANDLER_NAME "file_mapping"
#endif

#endif /* YAC_ALLOCATOR_H */
