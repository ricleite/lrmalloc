
#ifndef __LFMALLOC_H
#define __LFMALLOC_H

#include <atomic>

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
    // all blocks allocated or reserved
    SB_FULL     = 0,
    // has unreserved available blocks
    SB_PARTIAL  = 1,
    // all blocks are free
    SB_EMPTY    = 2,
};

struct Anchor;
struct DescriptorNode;
struct Descriptor;
struct ProcHeap;
struct SizeClassData;
struct TCacheBin;

#define LG_MAX_BLOCK_NUM    22
// LG_TAG must be (64 - 2 - LG_MAX_BLOCK_NUM * 2)
// see struct Anchor
#define LG_TAG              18
#define MAX_BLOCK_NUM       (2 << LG_MAX_BLOCK_NUM)

// helper struct to fill descriptor_t::anchor
// used as atomic_uint64_t
struct Anchor
{
    uint64_t state : 2;
    uint64_t avail : LG_MAX_BLOCK_NUM;
    uint64_t count : LG_MAX_BLOCK_NUM;
    uint64_t tag : LG_TAG;
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
} LFMALLOC_ATTR_CACHE_ALIGNED;

// at least one ProcHeap instance exists for each sizeclass
struct ProcHeap
{
public:
    // ptr to descriptor, head of partial descriptor list
    std::atomic<DescriptorNode> partialList;
    // size class index
    size_t scIdx;

public:
    size_t GetScIdx() const { return scIdx; }
    SizeClassData* GetSizeClass() const;

} LFMALLOC_ATTR(aligned(CACHELINE));

// size of allocated block when allocating descriptors
// block is split into multiple descriptors
// 64k byte blocks
#define DESCRIPTOR_BLOCK_SZ (16 * PAGE)

// global variables
// descriptor recycle list
extern std::atomic<DescriptorNode> AvailDesc;

// helper fns
void HeapPushPartial(Descriptor* desc);
Descriptor* HeapPopPartial(ProcHeap* heap);
void MallocFromPartial(size_t scIdx, TCacheBin* cache, size_t& blockNum);
void MallocFromNewSB(size_t scIdx, TCacheBin* cache, size_t& blockNum);
Descriptor* DescAlloc();
void DescRetire(Descriptor* desc);

void FillCache(size_t scIdx, TCacheBin* cache);
void FlushCache(size_t scIdx, TCacheBin* cache);

#endif // __LFMALLOC_H

