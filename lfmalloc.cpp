
#include <cassert>
#include <cstring>
#include <algorithm>
#include <atomic>

// for ENOMEM
#include <errno.h>

#include "lfmalloc.h"
#include "size_classes.h"
#include "pages.h"
#include "pagemap.h"
#include "log.h"
#include "tcache.h"

// global variables
// descriptor recycle list
std::atomic<DescriptorNode> AvailDesc({ nullptr, 0 });
// malloc init state
bool MallocInit = false;
// heaps, one heap per size class
ProcHeap Heaps[MAX_SZ_IDX];

// (un)register descriptor pages with pagemap
// all pages used by the descriptor will point to desc in
//  the pagemap
// for (unaligned) large allocations, only first page points to desc
// aligned large allocations get the corresponding page pointing to desc
void UpdatePageMap(ProcHeap* heap, char* ptr, Descriptor* desc, size_t scIdx)
{
    ASSERT(ptr);

    PageInfo info;
    info.Set(desc, scIdx);

    // large allocation, don't need to (un)register every page
    // just first
    if (!heap)
    {
        sPageMap.SetPageInfo(ptr, info);
        return;
    }

    // only need to worry about alignment for large allocations
    // ASSERT(ptr == superblock);

    // small allocation, (un)register every page
    // could *technically* optimize if blockSize >>> page, 
    //  but let's not worry about that
    size_t sbSize = heap->GetSizeClass()->sbSize;
    // sbSize is a multiple of page
    ASSERT((sbSize & PAGE_MASK) == 0);
    for (size_t idx = 0; idx < sbSize; idx += PAGE)
        sPageMap.SetPageInfo(ptr + idx, info); 
}

void RegisterDesc(Descriptor* desc)
{
    ProcHeap* heap = desc->heap;
    char* ptr = desc->superblock;
    size_t scIdx = 0;
    if (LIKELY(heap != nullptr))
        scIdx = heap->scIdx;

    UpdatePageMap(heap, ptr, desc, scIdx);
}

// unregister descriptor before superblock deletion
// can only be done when superblock is about to be free'd to OS
void UnregisterDesc(ProcHeap* heap, char* superblock)
{
    UpdatePageMap(heap, superblock, nullptr, 0L);
}

PageInfo GetPageInfoForPtr(void* ptr)
{
    return sPageMap.GetPageInfo((char*)ptr);
}

SizeClassData* ProcHeap::GetSizeClass() const
{
    return &SizeClasses[scIdx];
}

Descriptor* ListPopPartial(ProcHeap* heap)
{
    DescriptorNode oldHead = heap->partialList.load();
    DescriptorNode newHead;
    do
    {
        if (!oldHead.desc)
            return nullptr;

        newHead = oldHead.desc->nextPartial.load();
        newHead.counter = oldHead.counter;
    }
    while (!heap->partialList.compare_exchange_weak(
                oldHead, newHead));

    return oldHead.desc;
}

void ListPushPartial(Descriptor* desc)
{
    ProcHeap* heap = desc->heap;

    DescriptorNode oldHead = heap->partialList.load();
    DescriptorNode newHead = { desc, oldHead.counter + 1 };
    do
    {
        ASSERT(oldHead.desc != newHead.desc);
        newHead.desc->nextPartial.store(oldHead); 
    }
    while (!heap->partialList.compare_exchange_weak(
                oldHead, newHead));
}

void ListRemoveEmptyDesc(ProcHeap* heap, Descriptor* desc)
{
    // @todo: try a best-effort search to remove desc?
    // or do an actual search, but need to ensure that ABA can't happen
}

void HeapPushPartial(Descriptor* desc)
{
    ListPushPartial(desc);
}

Descriptor* HeapPopPartial(ProcHeap* heap)
{
    return ListPopPartial(heap);
}

void MallocFromPartial(size_t scIdx, size_t& blockNum)
{
    ProcHeap* heap = &Heaps[scIdx];
    TCacheBin* cache = &TCache[scIdx];

    Descriptor* desc = HeapPopPartial(heap);
    if (!desc)
        return;

    // reserve block(s)
    Anchor oldAnchor = desc->anchor.load();
    Anchor newAnchor;
    uint64_t blocksTaken = 0;

    // we have "ownership" of block, but anchor can still change
    // due to free()
    do
    {
        if (oldAnchor.state == SB_EMPTY)
        {
            DescRetire(desc);
            // retry
            return MallocFromPartial(scIdx, blockNum);
        }

        // oldAnchor must be SB_PARTIAL
        // can't be SB_FULL because we *own* the block now
        // and it came from HeapPopPartial
        // can't be SB_EMPTY, we already checked
        // obviously can't be SB_ACTIVE
        ASSERT(oldAnchor.state == SB_PARTIAL);

        blocksTaken = std::min<uint64_t>(oldAnchor.count, blockNum);
        newAnchor = oldAnchor;
        newAnchor.count -= blocksTaken;
        if (newAnchor.count == 0)
            newAnchor.state = SB_FULL;
    }
    while (!desc->anchor.compare_exchange_weak(
                oldAnchor, newAnchor));

    ASSERT(newAnchor.count < desc->maxcount);

    // pop reserved block(s)
    // because of free(), may need to retry
    oldAnchor = desc->anchor.load();

    char* superblock = desc->superblock;
    uint64_t maxcount = desc->maxcount;
    uint64_t blockSize = desc->blockSize;

    // while re-reading anchor, state might change if state = SB_FULL
    //  a free() will set state = SB_PARTIAL and push to partial list
    bool addToPartialList = (newAnchor.state == SB_PARTIAL);

    do
    {
        uint64_t nextAvail = oldAnchor.avail;
        for (uint64_t i = 0; i < blocksTaken; ++i)
        {
            char* block = superblock + nextAvail * blockSize;
            nextAvail = *(uint64_t*)block;
            // other threads may have alloc'd block
            // value in block may not make sense (e.g out of bounds)
            // in that case, break cycle early, we already know CAS will fail
            // (and we won't access invalid memory)
            if (nextAvail >= maxcount)
                break;
        }

        // update avail
        newAnchor = oldAnchor;
        newAnchor.avail = nextAvail;
        newAnchor.tag++;
    }
    while (!desc->anchor.compare_exchange_weak(
                oldAnchor, newAnchor));

    // can safely read desc fields after CAS, since desc cannot become empty
    //  until after this fn returns block
    ASSERT(newAnchor.avail < desc->maxcount || newAnchor.state == SB_FULL);

    // from oldAnchor we can now push the reserved blocks to cache
    uint64_t avail = oldAnchor.avail;
    for (uint64_t i = 0; i < blocksTaken; ++i)
    {
        ASSERT(avail < desc->maxcount);
        char* block = superblock + avail * blockSize;
        avail = *(uint64_t*)block;
        cache->PushBlock(block);
    }

    if (addToPartialList)
        HeapPushPartial(desc);

    ASSERT(blockNum >= blocksTaken);
    blockNum -= blocksTaken;
}

void MallocFromNewSB(size_t scIdx, size_t& blockNum)
{
    ProcHeap* heap = &Heaps[scIdx];
    TCacheBin* cache = &TCache[scIdx];
    SizeClassData* sc = &SizeClasses[scIdx];

    Descriptor* desc = DescAlloc();
    ASSERT(desc);

    desc->heap = heap;
    desc->blockSize = sc->blockSize;
    desc->maxcount = sc->GetBlockNum();
    desc->superblock = (char*)PageAlloc(sc->sbSize);

    uint64_t blocksTaken = std::min<uint64_t>(desc->maxcount, blockNum);

    // push blocks to cache
    uint64_t const blockSize = sc->blockSize;
    for (uint64_t idx = 0; idx < blocksTaken; ++idx)
    {
        char* block = desc->superblock + idx * blockSize;
        cache->PushBlock(block);
    }

    // allocate superblock, organize blocks in a linked list
    // this is left for last so we don't do needless work to blocks
    //  that end up in the cache
    for (uint64_t idx = blocksTaken; idx < desc->maxcount - 1; ++idx)
        *(uint64_t*)(desc->superblock + idx * blockSize) = (idx + 1);

    Anchor anchor;
    anchor.avail = blocksTaken;
    anchor.count = desc->maxcount - blocksTaken;
    anchor.state = (anchor.count > 0) ? SB_PARTIAL : SB_FULL;
    anchor.tag = 0;

    desc->anchor.store(anchor);

    ASSERT(anchor.avail < desc->maxcount || anchor.state == SB_FULL);
    ASSERT(anchor.count < desc->maxcount);

    // register new descriptor
    // must be done before setting superblock as active
    // or leaving superblock as available in a partial list
    RegisterDesc(desc);

    if (anchor.state == SB_PARTIAL)
        HeapPushPartial(desc);

    ASSERT(blockNum >= blocksTaken);
    blockNum -= blocksTaken;
}

void RemoveEmptyDesc(ProcHeap* heap, Descriptor* desc)
{
    ListRemoveEmptyDesc(heap, desc);
}

Descriptor* DescAlloc()
{
    DescriptorNode oldHead = AvailDesc.load();
    while (true)
    {
        if (oldHead.desc)
        {
            DescriptorNode newHead = oldHead.desc->nextFree.load();
            newHead.counter = oldHead.counter;
            if (AvailDesc.compare_exchange_weak(oldHead, newHead))
            {
                ASSERT(oldHead.desc->blockSize == 0);
                return oldHead.desc;
            }
        }
        else
        {
            // allocate several pages
            // get first descriptor, this is returned to caller
            char* ptr = (char*)PageAlloc(DESCRIPTOR_BLOCK_SZ);
            Descriptor* ret = (Descriptor*)ptr;
            // organize list with the rest of descriptors
            // and add to available descriptors
            {
                Descriptor* first = nullptr;
                Descriptor* prev = nullptr;

                char* currPtr = ptr + sizeof(Descriptor);
                currPtr = ALIGN_ADDR(currPtr, CACHELINE);
                first = (Descriptor*)currPtr;
                while (currPtr + sizeof(Descriptor) <
                        ptr + DESCRIPTOR_BLOCK_SZ)
                {
                    Descriptor* curr = (Descriptor*)currPtr;
                    if (prev)
                        prev->nextFree.store({curr, 0});

                    prev = curr;
                    currPtr = currPtr + sizeof(Descriptor);
                    currPtr = ALIGN_ADDR(currPtr, CACHELINE);
                }

                prev->nextFree.store({nullptr, 0});

                // add list to available descriptors
                DescriptorNode oldHead = AvailDesc.load();
                DescriptorNode newHead;
                do
                {
                    prev->nextFree.store(oldHead);
                    newHead.desc = first;
                    newHead.counter = oldHead.counter + 1;
                }
                while (!AvailDesc.compare_exchange_weak(oldHead, newHead));
            }

            return ret;
        }
    }
}

void DescRetire(Descriptor* desc)
{
    desc->blockSize = 0;
    DescriptorNode oldHead = AvailDesc.load();
    DescriptorNode newHead;
    do
    {
        desc->nextFree.store(oldHead);
        newHead.desc = desc;
        newHead.counter = oldHead.counter + 1;
    }
    while (!AvailDesc.compare_exchange_weak(oldHead, newHead));
}

void FillCache(size_t scIdx)
{
    SizeClassData* sc = &SizeClasses[scIdx];

    // number of blocks to fill the cache with
    // using the same number of blocks as a superblock can have
    size_t blockNum = sc->blockNum;
    while (blockNum > 0)
    {
        MallocFromPartial(scIdx, blockNum);
        if (blockNum == 0)
            continue;

        MallocFromNewSB(scIdx, blockNum);
    }
}

void FlushCache(size_t scIdx)
{
    TCacheBin* cache = &TCache[scIdx];
    ProcHeap* heap = &Heaps[scIdx];

    // @todo: optimize
    // in the normal case, we should be able to return several
    //  blocks with a single CAS
    while (char* ptr = cache->PopBlock())
    {
        PageInfo info = GetPageInfoForPtr(ptr);
        Descriptor* desc = info.GetDesc();
        char* superblock = desc->superblock;
        uint64_t blockSize = desc->blockSize;
        uint64_t idx = (ptr - superblock) / blockSize;
        // after CAS, desc might become empty and
        //  concurrently reused, so store maxcount
        uint64_t maxcount = desc->maxcount;
        (void)maxcount; // suppress unused warning

        Anchor oldAnchor = desc->anchor.load();
        Anchor newAnchor;
        do
        {
            // update anchor.avail
            *(uint64_t*)ptr = oldAnchor.avail;

            newAnchor = oldAnchor;
            newAnchor.avail = idx;
            // state updates
            // don't set SB_PARTIAL if state == SB_ACTIVE
            if (oldAnchor.state == SB_FULL)
                newAnchor.state = SB_PARTIAL;
            // this can't happen with SB_ACTIVE
            // because of reserved blocks
            ASSERT(oldAnchor.count < desc->maxcount);
            if (oldAnchor.count == desc->maxcount - 1)
                newAnchor.state = SB_EMPTY; // can free superblock
            else
                ++newAnchor.count;
        }
        while (!desc->anchor.compare_exchange_weak(
                    oldAnchor, newAnchor));

        // after last CAS, can't reliably read any desc fields
        // as desc might have become empty and been concurrently reused
        ASSERT(oldAnchor.avail < maxcount || oldAnchor.state == SB_FULL);
        ASSERT(newAnchor.avail < maxcount);
        ASSERT(newAnchor.count < maxcount);

        // CAS success, can free block
        if (newAnchor.state == SB_EMPTY)
        {
            // unregister descriptor
            UnregisterDesc(heap, superblock);

            // free superblock
            PageFree(superblock, heap->GetSizeClass()->sbSize);
            RemoveEmptyDesc(heap, desc);
        }
        else if (oldAnchor.state == SB_FULL)
            HeapPushPartial(desc);
    }
}

void InitMalloc()
{
    LOG_DEBUG();

    // hard assumption that this can't be called concurrently
    MallocInit = true;

    // init size classes
    InitSizeClass();

    // init heaps
    for (size_t idx = 0; idx < MAX_SZ_IDX; ++idx)
    {
        ProcHeap& heap = Heaps[idx];
        heap.partialList.store({nullptr, 0});
        heap.scIdx = idx;
    }
}

size_t GetOrInitSizeClass(size_t size)
{
    if (UNLIKELY(!MallocInit))
        InitMalloc();

    return GetSizeClass(size);
}

void lf_malloc_initialize() { }

void lf_malloc_finalize() { }

void lf_malloc_thread_initialize() { }

void lf_malloc_thread_finalize()
{
    // flush caches
    for (size_t scIdx = 1; scIdx < MAX_SZ_IDX; ++scIdx)
        FlushCache(scIdx);
}

extern "C"
void* lf_malloc(size_t size) noexcept
{
    LOG_DEBUG("size: %lu", size);

    // size class calculation
    size_t scIdx = GetOrInitSizeClass(size);
    // large block allocation
    if (UNLIKELY(!scIdx))
    {
        size_t pages = PAGE_CEILING(size);
        Descriptor* desc = DescAlloc();
        ASSERT(desc);

        desc->heap = nullptr;
        desc->blockSize = pages;
        desc->maxcount = 1;
        desc->superblock = (char*)PageAlloc(pages);

        Anchor anchor;
        anchor.avail = 0;
        anchor.count = 0;
        anchor.state = SB_FULL;
        anchor.tag = 0;

        desc->anchor.store(anchor);

        RegisterDesc(desc);

        char* ptr = desc->superblock;
        LOG_DEBUG("large, ptr: %p", ptr);
        return (void*)ptr;
    }

    TCacheBin* cache = &TCache[scIdx];
    // fill cache if needed
    if (UNLIKELY(cache->GetBlockNum() == 0))
        FillCache(scIdx);

    return cache->PopBlock();
}

extern "C"
void* lf_calloc(size_t n, size_t size) noexcept
{
    LOG_DEBUG();
    size_t allocSize = n * size;
    // overflow check
    // @todo: expensive, need to optimize
    if (UNLIKELY(n == 0 || allocSize / n != size))
        return nullptr;

    void* ptr = lf_malloc(allocSize);

    // calloc returns zero-filled memory
    // @todo: optimize, memory may be already zero-filled 
    //  if coming directly from OS
    if (LIKELY(ptr != nullptr))
        memset(ptr, 0x0, allocSize);

    return ptr;
}

extern "C"
void* lf_realloc(void* ptr, size_t size) noexcept
{
    LOG_DEBUG();
    void* newPtr = lf_malloc(size);
    if (LIKELY(ptr && newPtr))
    {
        PageInfo info = GetPageInfoForPtr(ptr);
        Descriptor* desc = info.GetDesc();
        ASSERT(desc);

        uint64_t blockSize = desc->blockSize;
        // prevent invalid memory access if size < blockSize
        blockSize = std::min(size, blockSize);
        memcpy(newPtr, ptr, blockSize);
    }

    lf_free(ptr);
    return newPtr;
}

extern "C"
size_t lf_malloc_usable_size(void* ptr) noexcept
{
    LOG_DEBUG();
    if (UNLIKELY(ptr == nullptr))
        return 0;

    // @todo: could optimize by trying to use scIdx
    // albeit that does require an extra branch
    PageInfo info = GetPageInfoForPtr(ptr);
    Descriptor* desc = info.GetDesc();
    return desc->blockSize;
}

extern "C"
int lf_posix_memalign(void** memptr, size_t alignment, size_t size) noexcept
{
    LOG_DEBUG();
    // @todo: this is so very inefficient
    size = std::max(alignment, size) * 2;
    char* ptr = (char*)malloc(size);
    if (!ptr)
        return ENOMEM;

    // because of alignment, might need to update pagemap
    // aligned large allocations need to correctly point to desc
    //  wherever the block starts (might not be start due to alignment)
    PageInfo info = GetPageInfoForPtr(ptr);
    Descriptor* desc = info.GetDesc();
    ASSERT(desc);

    LOG_DEBUG("original ptr: %p", ptr);
    ptr = ALIGN_ADDR(ptr, alignment);

    // need to update page so that descriptors can be found
    //  for large allocations aligned to "middle" of superblocks
    if (UNLIKELY(!desc->heap))
        UpdatePageMap(nullptr, ptr, desc, 0L);

    LOG_DEBUG("provided ptr: %p", ptr);
    *memptr = ptr;
    return 0;
}

extern "C"
void* lf_aligned_alloc(size_t alignment, size_t size) noexcept
{
    LOG_DEBUG();
    void* ptr = nullptr;
    int ret = lf_posix_memalign(&ptr, alignment, size);
    if (ret)
        return nullptr;

    return ptr;
}

extern "C"
void* lf_valloc(size_t size) noexcept
{
    LOG_DEBUG();
    return lf_aligned_alloc(PAGE, size);
}

extern "C"
void* lf_memalign(size_t alignment, size_t size) noexcept
{
    LOG_DEBUG();
    return lf_aligned_alloc(alignment, size);
}

extern "C"
void* lf_pvalloc(size_t size) noexcept
{
    LOG_DEBUG();
    size = ALIGN_ADDR(size, PAGE);
    return lf_aligned_alloc(PAGE, size);
}

extern "C"
void lf_free(void* ptr) noexcept
{
    LOG_DEBUG("ptr: %p", ptr);
    if (UNLIKELY(!ptr))
        return;

    PageInfo info = GetPageInfoForPtr(ptr);
    Descriptor* desc = info.GetDesc();
    if (UNLIKELY(!desc))
    {
        // @todo: this can happen with dynamic loading
        // need to print correct message
        ASSERT(desc);
        return;
    }

    size_t scIdx = info.GetScIdx();
    
    LOG_DEBUG("Heap %p, Desc %p, ptr %p", heap, desc, ptr);

    // large allocation case
    if (UNLIKELY(!scIdx))
    {
        char* superblock = desc->superblock;

        // unregister descriptor
        UnregisterDesc(nullptr, superblock);
        // aligned large allocation case
        if (UNLIKELY((char*)ptr != superblock))
            UnregisterDesc(nullptr, (char*)ptr);

        // free superblock
        PageFree(superblock, desc->blockSize);
        RemoveEmptyDesc(nullptr, desc);

        // desc cannot be in any partial list, so it can be
        //  immediately reused
        DescRetire(desc);
        return;
    }

    // ptr may be aligned, need to recompute to be sure
    // @todo: optimize, integer div is expensive
    char* superblock = desc->superblock;
    uint64_t blockSize = desc->blockSize;
    uint64_t idx = ((char*)ptr - superblock) / blockSize;
    // recompute
    ptr = (char*)(superblock + idx * blockSize);

    TCacheBin* cache = &TCache[scIdx];
    SizeClassData* sc = &SizeClasses[scIdx];

    // flush cache if need
    if (UNLIKELY(cache->GetBlockNum() >= sc->GetBlockNum()))
        FlushCache(scIdx);

    cache->PushBlock((char*)ptr);
}



