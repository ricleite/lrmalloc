/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#ifndef __LFMALLOC_H
#define __LFMALLOC_H

#include "defines.h"

#define lf_malloc malloc
#define lf_free free
#define lf_calloc calloc
#define lf_realloc realloc
#define lf_malloc_usable_size malloc_usable_size
#define lf_posix_memalign posix_memalign
#define lf_aligned_alloc aligned_alloc
#define lf_valloc valloc
#define lf_memalign memalign
#define lf_pvalloc pvalloc

// exports
extern "C" {
// malloc interface
void* lf_malloc(size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
void lf_free(void* ptr) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_CACHE_ALIGNED_FN;
void* lf_calloc(size_t n, size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE2(1, 2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_realloc(void* ptr, size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
// utilities
size_t lf_malloc_usable_size(void* ptr) noexcept;
// memory alignment ops
int lf_posix_memalign(void** memptr, size_t alignment, size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ATTR(nonnull(1));
void* lf_aligned_alloc(size_t alignment, size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_valloc(size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
// obsolete alignment oos
void* lf_memalign(size_t alignment, size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_pvalloc(size_t size) noexcept LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
}

#endif // __LFMALLOC_H
