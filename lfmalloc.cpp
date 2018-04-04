
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

// global variables
// descriptor recycle list
std::atomic<DescriptorNode> AvailDesc({ nullptr, 0 });

// utilities
ActiveDescriptor* MakeActive(Descriptor* desc, uint64_t credits)
{
    ASSERT(((uint64_t)desc & CREDITS_MASK) == 0);
    ASSERT(credits < CREDITS_MAX);

    ActiveDescriptor* active = (ActiveDescriptor*)
        ((uint64_t)desc | credits);
    return active;
}

void GetActive(ActiveDescriptor* active, Descriptor** desc, uint64_t* credits)
{
    if (desc)
        *desc = (Descriptor*)((uint64_t)active & ~CREDITS_MASK);

    if (credits)
        *credits = (uint64_t)active & CREDITS_MASK;
}

// (un)register descriptor pages with pagemap
// all pages used by the descriptor will point to desc in
//  the pagemap
void UpdatePageMap(Descriptor* desc, Descriptor* value)
{
    char* superblock = desc->superblock;
    ASSERT(superblock);

    PageInfo info;
    info.desc = value;

    // large allocation, don't need to (un)register every page
    // just first
    if (!desc->heap)
        sPageMap.SetPageInfo(superblock, info);
    else
    {
        // small allocation, (un)register every page
        // could *technically* optimize if blockSize >>> page, 
        //  but let's not worry about that
        size_t sbSize = desc->heap->sizeclass->sbSize;
        // sbSize is a multiple of page
        ASSERT((sbSize & PAGE_MASK) == 0);
        for (size_t idx = 0; idx < sbSize; idx += PAGE)
           sPageMap.SetPageInfo(superblock + idx, info); 
    }
}

void RegisterDesc(Descriptor* desc)
{
    UpdatePageMap(desc, desc);
}

// unregister descriptor before superblock deletion
// can only be done when superblock is about to be free'd to OS
void UnregisterDesc(Descriptor* desc)
{
    UpdatePageMap(desc, nullptr);
}

Descriptor* GetDescriptorForPtr(void* ptr)
{
    PageInfo info = sPageMap.GetPageInfo((char*)ptr);
    return info.desc;
}

void* MallocFromActive(ProcHeap* heap)
{
    // reserve block
    ActiveDescriptor* oldActive = heap->active.load();
    ActiveDescriptor* newActive;
    uint64_t oldCredits;
    do
    {
        if (!oldActive)
            return nullptr;

        Descriptor* oldDesc;
        GetActive(oldActive, &oldDesc, &oldCredits);

        // if credits > 0, subtract by 1
        // otherwise set newActive to nullptr
        newActive = nullptr;
        if (oldCredits > 0)
            newActive = MakeActive(oldDesc, oldCredits - 1);

    } while (!heap->active.compare_exchange_weak(
            oldActive, newActive));

    Descriptor* desc = (Descriptor*)((uint64_t)oldActive & ~CREDITS_MASK);

    LOG_DEBUG("Heap %p, Desc %p", heap, desc);

    // pop block (that we assert exists)
    // underlying superblock *CANNOT* change after
    // block reservation, it'll never be empty until we use it
    char* ptr = nullptr;
    uint64_t credits = 0;

    // anchor state *CANNOT* be empty
    // there is at least one reserved block
    Anchor oldAnchor = desc->anchor.load();
    Anchor newAnchor;
    do
    {
        ASSERT(oldAnchor.avail < desc->maxcount);

        // compute available block
        uint64_t blockSize = desc->blockSize;
        uint64_t avail = oldAnchor.avail;
        uint64_t offset = avail * blockSize;
        ptr = (desc->superblock + offset);

        // @todo: synchronize this access
        uint64_t next = *(uint64_t*)ptr;

        newAnchor = oldAnchor;
        newAnchor.avail = next;
        newAnchor.tag++;
        // last available block
        if (oldCredits == 0)
        {
            // superblock is completely used up
            if (oldAnchor.count == 0)
                newAnchor.state = SB_FULL;
            else
            {
                // otherwise, fill up credits
                credits = std::min<uint64_t>(oldAnchor.count, CREDITS_MAX);
                newAnchor.count -= credits;
            }
        }
    }
    while (!desc->anchor.compare_exchange_weak(
                oldAnchor, newAnchor));

    // can safely read desc fields after CAS, since desc cannot become empty
    //  until after this fn returns block
    ASSERT(newAnchor.avail < desc->maxcount || (oldCredits == 0 && oldAnchor.count == 0));

    // credits change, update
    // while credits == 0, active is nullptr
    // meaning allocations *CANNOT* come from an active block
    if (credits > 0)
        UpdateActive(heap, desc, credits);

    LOG_DEBUG("Heap %p, Desc %p, ptr %p", heap, desc, ptr);

    return (void*)ptr;
}

void UpdateActive(ProcHeap* heap, Descriptor* desc, uint64_t credits)
{
    ActiveDescriptor* oldActive = heap->active.load();
    ActiveDescriptor* newActive = MakeActive(desc, credits - 1);

    if (heap->active.compare_exchange_strong(oldActive, newActive))
        return; // all good

    // someone installed another active superblock
    // return credits to superblock, make it SB_PARTIAL
    // (because the superblock is no longer active but has available blocks)
    {
        Anchor oldAnchor = desc->anchor.load();
        Anchor newAnchor;
        do
        {
            newAnchor = oldAnchor;
            newAnchor.count += credits;
            newAnchor.state = SB_PARTIAL;
        }
        while (!desc->anchor.compare_exchange_weak(
            oldAnchor, newAnchor));
    }

    HeapPushPartial(desc);
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

void* MallocFromPartial(ProcHeap* heap)
{
    Descriptor* desc = HeapPopPartial(heap);
    if (!desc)
        return nullptr;

    // reserve block
    Anchor oldAnchor = desc->anchor.load();
    Anchor newAnchor;
    uint64_t credits = 0;

    // we have "ownership" of block, but anchor can still change
    // due to free()
    do
    {
        if (oldAnchor.state == SB_EMPTY)
        {
            DescRetire(desc);
            // retry
            return MallocFromPartial(heap);
        }

        // oldAnchor must be SB_PARTIAL
        // can't be SB_FULL because we *own* the block now
        // and it came from HeapPopPartial
        // can't be SB_EMPTY, we already checked
        // obviously can't be SB_ACTIVE
        credits = std::min<uint64_t>(oldAnchor.count - 1, CREDITS_MAX);
        newAnchor = oldAnchor;
        newAnchor.count -= 1; // block we're allocating right now
        newAnchor.count -= credits;
        newAnchor.state = (credits > 0) ?
            SB_ACTIVE : SB_FULL;
    }
    while (!desc->anchor.compare_exchange_weak(
                oldAnchor, newAnchor));

    ASSERT(newAnchor.count < desc->maxcount);

    // pop reserved block
    // because of free(), may need to retry
    char* ptr = nullptr;
    oldAnchor = desc->anchor.load();

    do
    {
        // compute ptr
        uint64_t idx = oldAnchor.avail;
        uint64_t blockSize = desc->heap->sizeclass->blockSize;
        ptr = desc->superblock + idx * blockSize;

        // update avail
        newAnchor = oldAnchor;
        newAnchor.avail = *(uint64_t*)ptr;
        newAnchor.tag++;
    }
    while (!desc->anchor.compare_exchange_weak(
                oldAnchor, newAnchor));

    // can safely read desc fields after CAS, since desc cannot become empty
    //  until after this fn returns block
    ASSERT(newAnchor.avail < desc->maxcount || newAnchor.state == SB_FULL);

    // credits change, update
    if (credits > 0)
        UpdateActive(heap, desc, credits);

    return (void*)ptr;
}

void* MallocFromNewSB(ProcHeap* heap)
{
    SizeClassData const* sc = heap->sizeclass;

    Descriptor* desc = DescAlloc();
    ASSERT(desc);

    desc->heap = heap;
    desc->blockSize = sc->blockSize;
    desc->maxcount = sc->GetBlockNum();
    // allocate superblock, organize blocks in a linked list
    {
        desc->superblock = (char*)PageAlloc(sc->sbSize);

        // ignore "block 0", that's given to the caller
        uint64_t const blockSize = sc->blockSize;
        for (uint64_t idx = 1; idx < desc->maxcount - 1; ++idx)
            *(uint64_t*)(desc->superblock + idx * blockSize) = (idx + 1);
    }

    uint64_t credits = std::min<uint64_t>(desc->maxcount - 1, CREDITS_MAX);
    ActiveDescriptor* newActive = MakeActive(desc, credits - 1);

    Anchor anchor;
    anchor.avail = 1;
    anchor.count = (desc->maxcount - 1) - credits;
    anchor.state = SB_ACTIVE;
    anchor.tag = 0;

    desc->anchor.store(anchor);

    ASSERT(anchor.avail < desc->maxcount);
    ASSERT(anchor.count < desc->maxcount);

    // try to update active superblock
    ActiveDescriptor* oldActive = heap->active.load();
    if (oldActive ||
        !heap->active.compare_exchange_strong(
            oldActive, newActive))
    {
        // there's already an active superblock
        PageFree(desc->superblock, sc->sbSize);
        DescRetire(desc);
        return nullptr;
    }

    // register new descriptor
    RegisterDesc(desc);

    char* ptr = desc->superblock;
    LOG_DEBUG("desc: %p, ptr: %p", desc, ptr);
    return (void*)ptr;
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
                return oldHead.desc;
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

static bool MallocInit = false;
ProcHeap Heaps[MAX_SZ_IDX];

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
        heap.active.store(nullptr);
        heap.partialList.store({nullptr, 0});
        heap.sizeclass = &SizeClasses[idx];
    }
}

ProcHeap* GetProcHeap(size_t size)
{
    // compute size class
    if (UNLIKELY(!MallocInit))
        InitMalloc();

    size_t scIdx = GetSizeClass(size);
    if (LIKELY(scIdx))
        return &Heaps[scIdx];

    return nullptr;
}

extern "C"
void* lf_malloc(size_t size) noexcept
{
    LOG_DEBUG("size: %lu", size);

    // size class calculation
    ProcHeap* heap = GetProcHeap(size);
    // large block allocation
    if (UNLIKELY(!heap))
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

    while (1)
    {
        if (void* ptr = MallocFromActive(heap))
        {
            LOG_DEBUG("MallocFromActive, ptr: %p", ptr);
            return ptr;
        }

        if (void* ptr = MallocFromPartial(heap))
        {
            LOG_DEBUG("MallocFromPartial, ptr: %p", ptr);
            return ptr;
        }

        if (void* ptr = MallocFromNewSB(heap))
        {
            LOG_DEBUG("MallocFromNewSB, ptr: %p", ptr);
            return ptr;
        }
    }
}

extern "C"
void* lf_calloc(size_t n, size_t size) noexcept
{
    LOG_DEBUG();
    // @todo: check overflow
    return malloc(n * size);
}

extern "C"
void* lf_realloc(void* ptr, size_t size) noexcept
{
    LOG_DEBUG();
    void* newPtr = lf_malloc(size);
    if (LIKELY(ptr && newPtr))
    {
        Descriptor* desc = GetDescriptorForPtr(ptr);
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

    Descriptor* desc = GetDescriptorForPtr(ptr);
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

    LOG_DEBUG("original ptr: %p", ptr);
    ptr = ALIGN_ADDR(ptr, alignment);
    // this works fine for a couple reasons
    // 1) when freeing, we end up following Descriptor* and don't really use ptr
    // 2) twice the size of (alignment + size) is always enough to align
    LOG_DEBUG("provided ptr: %p", ptr);
    *memptr = ptr;
    return 0;
}

extern "C"
void* lf_aligned_alloc(size_t alignment, size_t size) noexcept
{
    LOG_DEBUG();
    void* ptr = nullptr;
    lf_posix_memalign(&ptr, alignment, size);
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

    Descriptor* desc = GetDescriptorForPtr(ptr);
    if (UNLIKELY(!desc))
    {
        // @todo: this can happen with dynamic loading
        // need to print correct message
        ASSERT(desc);
        return;
    }

    ProcHeap* heap = desc->heap;

    char* superblock = desc->superblock;
    
    LOG_DEBUG("Heap %p, Desc %p, ptr %p", heap, desc, ptr);

    // large allocation case
    if (UNLIKELY(!heap))
    {
        // free block
        PageFree(superblock, desc->blockSize);
        RemoveEmptyDesc(heap, desc);
        return;
    }

    // normal case
    {
        // after CAS, desc might become empty and
        //  concurrently reused, so store maxcount
        uint64_t maxcount = desc->maxcount;

        Anchor oldAnchor = desc->anchor.load();
        Anchor newAnchor;
        do
        {
            // compute index of ptr
            uint64_t blockSize = desc->blockSize;
            uint64_t idx = ((char*)ptr - superblock) / blockSize;

            // recompute ptr, 
            // @todo: remove when descriptor ptrs are no longer stored in "user" memory
            ptr = (char*)(desc->superblock + idx * blockSize);

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
            UnregisterDesc(desc);

            // free superblock
            PageFree(superblock, heap->sizeclass->sbSize);
            RemoveEmptyDesc(heap, desc);
        }
        else if (oldAnchor.state == SB_FULL)
            HeapPushPartial(desc);
    }
}



