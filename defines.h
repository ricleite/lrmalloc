
#ifndef __DEFINES_H__
#define __DEFINES_H__

// a cache line is 64 bytes
#define LG_CACHELINE    6
// a page is 4KB
#define LG_PAGE         12
// a huge page is 2MB
#define LG_HUGEPAGE     21

#define CACHELINE   ((size_t)(1U << LG_CACHELINE))
#define PAGE        ((size_t)(1U << LG_PAGE))
#define HUGEPAGE    ((size_t)(1U << LG_HUGEPAGE))

#define PAGE_MASK   (PAGE - 1)

// minimum alignment requirement all allocations must meet
// "address returned by malloc will be suitably aligned to store any kind of variable"
#define MIN_ALIGN sizeof(void*)

// returns smallest address >= addr with alignment align
#define ALIGN_ADDR(addr, align) \
    ( __typeof__ (addr))(((size_t)(addr) + (align - 1)) & ((~(align)) + 1))


// return smallest page size multiple that is >= s
#define PAGE_CEILING(s) \
    (((s) + (PAGE - 1)) & ~(PAGE - 1))

// https://stackoverflow.com/questions/109710/how-do-the-likely-and-unlikely-macros-in-the-linux-kernel-work-and-what-is-t
#define LIKELY(x)       __builtin_expect((x), 1)
#define UNLIKELY(x)     __builtin_expect((x), 0)

#endif // __DEFINES_H__
