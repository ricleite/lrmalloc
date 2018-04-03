
#ifndef __LFMALLOC_H
#define __LFMALLOC_H

#include <atomic>

#include "defines.h"

#define LFMALLOC_ATTR(s) __attribute__((s))
#define LFMALLOC_ALLOC_SIZE(s) LFMALLOC_ATTR(alloc_size(s))
#define LFMALLOC_ALLOC_SIZE2(s1, s2) LFMALLOC_ATTR(alloc_size(s1, s2))
#define LFMALLOC_EXPORT LFMALLOC_ATTR(visibility("default"))
#define LFMALLOC_NOTHROW LFMALLOC_ATTR(nothrow)

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

// called on process init/exit
void lf_malloc_initialize();
void lf_malloc_finalize();
// called on thread enter/exit
void lf_malloc_thread_initialize();
void lf_malloc_thread_finalize();

// exports
extern "C"
{
    // malloc interface
    void* lf_malloc(size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(1);
    void lf_free(void* ptr) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW;
    void* lf_calloc(size_t n, size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE2(1, 2);
    void* lf_realloc(void* ptr, size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(2);
    // utilities
    size_t lf_malloc_usable_size(void* ptr) noexcept;
    // memory alignment ops
    int lf_posix_memalign(void** memptr, size_t alignment, size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ATTR(nonnull(1))
        LFMALLOC_ALLOC_SIZE(3);
    void* lf_aligned_alloc(size_t alignment, size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(2);
    void* lf_valloc(size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(1);
    // obsolete alignment oos
    void* lf_memalign(size_t alignment, size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(2);
    void* lf_pvalloc(size_t size) noexcept
        LFMALLOC_EXPORT LFMALLOC_NOTHROW LFMALLOC_ALLOC_SIZE(1);
}

// superblock states
// used in Anchor::state
enum SuperblockState
{
    // superblock used in ProcHeap
    SB_ACTIVE   = 0,
    // all blocks allocated or reserved
    SB_FULL     = 1,
    // not ACTIVE but has unreserved available blocks
    SB_PARTIAL  = 2,
    // not ACTIVE and all blocks are free
    SB_EMPTY    = 3,
};

struct Anchor;
struct DescriptorNode;
struct Descriptor;
struct ProcHeap;
struct SizeClassData;

// helper struct to fill descriptor_t::anchor
// used as atomic_uint64_t
struct Anchor
{
    uint64_t state : 2;
    uint64_t avail : 15;
    uint64_t count : 15;
    uint64_t tag : 32;
} LFMALLOC_ATTR(packed);

STATIC_ASSERT(sizeof(Anchor) == sizeof(uint64_t), "Invalid anchor size");

// used with double-cas for atomic ops
struct DescriptorNode
{
    // ptr
    Descriptor* desc;
    // aba counter
    uint64_t counter;
} LFMALLOC_ATTR(packed);

// Superblock descriptor
// needs to be cache-line aligned
// descriptors are allocated and *never* freed
struct Descriptor
{
    // list node pointers
    // preferably 16-byte aligned
    // used in free descriptor list
    std::atomic<DescriptorNode> nextFree;
    // used in partial descriptor list
    std::atomic<DescriptorNode> nextPartial;
    // anchor
    std::atomic<Anchor> anchor;

    char* superblock;
    ProcHeap* heap;
    uint64_t blockSize; // block size
    uint64_t maxcount;
} LFMALLOC_ATTR(aligned(CACHELINE));

/*
// can't actually use a bitfield for this, unfortunately
struct ActiveDescriptor
{
    Descriptor* ptr : 58;
    uint64_t credits : 6;
};
*/

typedef Descriptor ActiveDescriptor;

// depends on the number of bits being used in ActiveDescriptor
#define CREDITS_MAX (1ULL << 6)
#define CREDITS_MASK ((1ULL << 6) - 1)

// at least one ProcHeap instance exists for each sizeclass
struct ProcHeap
{
    // active superblock descriptor
    // aligned to 64 bytes, last 6 bits used for credits
    // see ActiveDescriptor
    std::atomic<ActiveDescriptor*> active;
    // ptr to descriptor, head of partial descriptor list
    std::atomic<DescriptorNode> partialList;

    SizeClassData* sizeclass;
} LFMALLOC_ATTR(aligned(CACHELINE));

// size of allocated block when allocating descriptors
// block is split into multiple descriptors
// 64k byte blocks
#define DESCRIPTOR_BLOCK_SZ (16 * PAGE)

// global variables
// descriptor recycle list
extern std::atomic<DescriptorNode> AvailDesc;

// helper fns
void* MallocFromActive(ProcHeap* heap);
void UpdateActive(ProcHeap* heap, Descriptor* desc, uint64_t credits);
void HeapPushPartial(Descriptor* desc);
Descriptor* HeapPopPartial(ProcHeap* heap);
void* MallocFromPartial(ProcHeap* heap);
void* MallocFromNewSB(ProcHeap* heap);
void RemoveEmptyDesc(ProcHeap* heap, Descriptor* desc);
Descriptor* DescAlloc();
void DescRetire(Descriptor* desc);

ProcHeap* GetProcHeap(size_t size);

#endif // __LFMALLOC_H

