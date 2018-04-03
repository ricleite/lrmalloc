
#ifndef __SIZE_CLASSES_H
#define __SIZE_CLASSES_H

#include <cstddef>

#include "defines.h"

// number of size classes
// idx 0 reserved for large size classes
#define MAX_SZ_IDX 40
#define LG_MAX_SIZE_IDX 6
// size of the largest size class
#define MAX_SZ (2 << 14)

// contains size classes
// computed at compile time
struct SizeClassData
{
public:
    // size of block
    size_t blockSize;
    // superblock size
    // always a multiple of page size
    size_t sbSize;

public:
    size_t GetBlockNum() const { return sbSize / blockSize; }
};

// globals
// initialized at compile time
extern SizeClassData SizeClasses[MAX_SZ_IDX];
// *not* initialized at compile time, needs InitSizeClass() call
extern size_t SizeClassLookup[MAX_SZ];

// must be called before GetSizeClass
void InitSizeClass();

inline size_t GetSizeClass(size_t size)
{
    if (LIKELY(size <= MAX_SZ))
        return SizeClassLookup[size];

    return 0;
}

#endif // __SIZE_CLASSES_H
