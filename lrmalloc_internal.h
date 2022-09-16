/*
 * Copyright (C) 2022 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#ifndef __LFMALLOC_INTERNAL_H
#define __LFMALLOC_INTERNAL_H

#include <atomic>

#include "lrmalloc.h"
#include "log.h"

// superblock states
// used in Anchor::state
enum SuperblockState : uint8_t {
    // all blocks allocated or reserved
    SB_FULL = 0,
    // has unreserved available blocks
    SB_PARTIAL = 1,
    // all blocks are free
    SB_EMPTY = 2,
};

struct Anchor;
struct DescriptorNode;
struct Descriptor;
struct ProcHeap;
struct SizeClassData;
struct TCacheBin;

#define LG_MAX_BLOCK_NUM 31
#define MAX_BLOCK_NUM (1ul << LG_MAX_BLOCK_NUM)

struct Anchor {
    SuperblockState state : 2;
    uint32_t avail : LG_MAX_BLOCK_NUM;
    uint32_t count : LG_MAX_BLOCK_NUM;
} LFMALLOC_ATTR(packed);

STATIC_ASSERT(sizeof(Anchor) == sizeof(uint64_t), "Invalid anchor size");

struct DescriptorNode {
public:
    // ptr
    Descriptor* _desc;
    // aba counter
    // uint64_t _counter;

public:
    void Set(Descriptor* desc, uint64_t counter)
    {
        // desc must be cacheline aligned
        ASSERT(((uint64_t)desc & CACHELINE_MASK) == 0);
        // counter may be incremented but will always be stored in
        //  LG_CACHELINE bits
        _desc = (Descriptor*)((uint64_t)desc | (counter & CACHELINE_MASK));
    }

    Descriptor* GetDesc() const
    {
        return (Descriptor*)((uint64_t)_desc & ~CACHELINE_MASK);
    }

    uint64_t GetCounter() const
    {
        return (uint64_t)((uint64_t)_desc & CACHELINE_MASK);
    }

} LFMALLOC_ATTR(packed);

STATIC_ASSERT(sizeof(DescriptorNode) == sizeof(uint64_t), "Invalid descriptor node size");

// Superblock descriptor
// needs to be cache-line aligned
// descriptors are allocated and *never* freed
struct Descriptor {
    // list node pointers
    // used in free descriptor list
    std::atomic<DescriptorNode> nextFree;
    // used in partial descriptor list
    std::atomic<DescriptorNode> nextPartial;
    // anchor
    std::atomic<Anchor> anchor;

    char* superblock;
    ProcHeap* heap;
    uint32_t blockSize; // block size
    uint32_t maxcount;
} LFMALLOC_CACHE_ALIGNED;

// at least one ProcHeap instance exists for each sizeclass
struct ProcHeap {
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

#endif // __LFMALLOC_INTERNAL_H
