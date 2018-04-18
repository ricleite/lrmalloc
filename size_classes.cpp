
#include "defines.h"
#include "size_classes.h"
#include "lfmalloc.h"
#include "log.h"

#define SIZE_CLASS_bin_yes(blockSize, pages) \
    { blockSize, pages * PAGE },
#define SIZE_CLASS_bin_no(blockSize, pages)

#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
    SIZE_CLASS_bin_##bin((1U << lg_grp) + (ndelta << lg_delta), pgs)

SizeClassData SizeClasses[MAX_SZ_IDX] = {
    { 0, 0 },
    SIZE_CLASSES
};

size_t SizeClassLookup[MAX_SZ] = { 0 };

void InitSizeClass()
{
    for (size_t scIdx = 1; scIdx < MAX_SZ_IDX; ++scIdx)
    {
        SizeClassData& sc = SizeClasses[scIdx];
        size_t blockSize = sc.blockSize;
        size_t sbSize = sc.sbSize;
        // size class large enough to store several elements
        if (sbSize > blockSize && (sbSize % blockSize) == 0)
            continue; // skip

        // increase superblock size so it can hold >1 elements
        while (blockSize >= sbSize)
            sbSize += sc.sbSize;

        // increase superblock size to at least 64kB
        while (sbSize < (16 * PAGE))
            sbSize += sc.sbSize;

        sc.sbSize = sbSize;
    }

    // fill blockNum
    for (size_t scIdx = 1; scIdx < MAX_SZ_IDX; ++scIdx)
    {
        SizeClassData& sc = SizeClasses[scIdx];
        sc.blockNum = sc.sbSize / sc.blockSize;
        ASSERT(sc.blockNum > 0);
        ASSERT(sc.blockNum < MAX_BLOCK_NUM);
    }

    // first size class reserved for large allocations
    size_t lookupIdx = 0;
    for (size_t scIdx = 1; scIdx < MAX_SZ_IDX; ++scIdx)
    {
        SizeClassData const& sc = SizeClasses[scIdx];
        size_t blockSize = sc.blockSize;
        while (lookupIdx <= blockSize)
        {
            SizeClassLookup[lookupIdx] = scIdx;
            ++lookupIdx;
        } 
    }
}


