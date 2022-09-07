/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#ifndef __LFMALLOC_H
#define __LFMALLOC_H

// a cache line is 64 bytes
#define LG_CACHELINE 6
// a page is 4KB
#define LG_PAGE 12
// a huge page is 2MB
#define LG_HUGEPAGE 21

// a ptr is sizeof(void*) bytes
#define PTR_SZ sizeof(void*)
#define CACHELINE ((size_t)(1U << LG_CACHELINE))
#define PAGE ((size_t)(1U << LG_PAGE))
#define HUGEPAGE ((size_t)(1U << LG_HUGEPAGE))

#define PTR_MASK (PTR_SZ - 1)
#define CACHELINE_MASK (CACHELINE - 1)
#define PAGE_MASK (PAGE - 1)

// minimum alignment requirement all allocations must meet
// "address returned by malloc will be suitably aligned to store any kind of
// variable"
#define MIN_ALIGN sizeof(void*)

// returns smallest value >= value with alignment align
#define ALIGN_VAL(val, align) (__typeof__(val))(((size_t)(val) + (align - 1)) & ((~(align)) + 1))

// returns smallest address >= addr with alignment align
#define ALIGN_ADDR(addr, align) ALIGN_VAL(addr, align)

// return smallest page size multiple that is >= s
#define PAGE_CEILING(s) (((s) + (PAGE - 1)) & ~(PAGE - 1))

// https://stackoverflow.com/questions/109710/how-do-the-likely-and-unlikely-macros-in-the-linux-kernel-work-and-what-is-t
#define LIKELY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)

#define LFMALLOC_ATTR(s) __attribute__((s))
#define LFMALLOC_ALLOC_SIZE(s) LFMALLOC_ATTR(alloc_size(s))
#define LFMALLOC_ALLOC_SIZE2(s1, s2) LFMALLOC_ATTR(alloc_size(s1, s2))
#define LFMALLOC_EXPORT LFMALLOC_ATTR(visibility("default"))
#define LFMALLOC_NOTHROW LFMALLOC_ATTR(nothrow)

#if defined(__GNUC__)
#define LFMALLOC_INLINE          \
    LFMALLOC_ATTR(always_inline) \
    inline static
#elif defined(_MSC_VER)
#define LFMALLOC_INLINE __forceinline inline static
#else
#define LFMALLOC_INLINE
#endif

// use initial exec tls model, faster than regular tls
//  with the downside that the malloc lib can no longer be dlopen'd
// https://www.ibm.com/support/knowledgecenter/en/SSVUN6_1.1.0/com.ibm.xlcpp11.zlinux.doc/language_ref/attr_tls_model.html
#define LFMALLOC_TLS_INIT_EXEC LFMALLOC_ATTR(tls_model("initial-exec"))

#define LFMALLOC_CACHE_ALIGNED LFMALLOC_ATTR(aligned(CACHELINE))

#define LFMALLOC_CACHE_ALIGNED_FN LFMALLOC_ATTR(aligned(CACHELINE))

#define STATIC_ASSERT(x, m) static_assert(x, m)


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
#ifdef __cplusplus
extern "C" {
#endif
// malloc interface
void* lf_malloc(size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
void lf_free(void* ptr) LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_CACHE_ALIGNED_FN;
void* lf_calloc(size_t n, size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE2(1, 2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_realloc(void* ptr, size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
// utilities
size_t lf_malloc_usable_size(void* ptr);
// memory alignment ops
int lf_posix_memalign(void** memptr, size_t alignment, size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ATTR(nonnull(1));
void* lf_aligned_alloc(size_t alignment, size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_valloc(size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
// obsolete alignment oos
void* lf_memalign(size_t alignment, size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(2) LFMALLOC_CACHE_ALIGNED_FN;
void* lf_pvalloc(size_t size) LFMALLOC_EXPORT LFMALLOC_NOTHROW
    LFMALLOC_ALLOC_SIZE(1) LFMALLOC_CACHE_ALIGNED_FN;
#ifdef __cplusplus
}
#endif

#endif // __LFMALLOC_H
