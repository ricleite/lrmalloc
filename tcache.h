/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#ifndef __TCACHE_H_
#define __TCACHE_H_

#include "log.h"
#include "lrmalloc.h"
#include "size_classes.h"
#include <cstddef>

struct TCacheBin {
private:
    char* _block = nullptr;
    uint32_t _blockNum = 0;

public:
    // common, fast ops
    void PushBlock(char* block, size_t scIdx);
    // push block list, cache *must* be empty
    void PushList(char* block, uint32_t length);

    char* PopBlock(size_t scIdx); // can return nullptr
    // manually popped list of blocks and now need to update cache
    // `block` is the new head
    void PopList(char* block, uint32_t length);
    char* PeekBlock() const { return _block; }

    uint32_t GetBlockNum() const { return _blockNum; }

    // slow operations like fill/flush handled in cache user
};

inline void TCacheBin::PushBlock(char* block, size_t scIdx)
{
    size_t blockSize = SizeClasses[scIdx].blockSize;
    // block has at least sizeof(char*)
    *(ptrdiff_t*)block = _block - block - blockSize;
    _block = block;
    _blockNum++;
}

inline void TCacheBin::PushList(char* block, uint32_t length)
{
    // caller must ensure there's no available block
    // this op is only used to fill empty cache
    ASSERT(_blockNum == 0);

    _block = block;
    _blockNum = length;
}

inline char* TCacheBin::PopBlock(size_t scIdx)
{
    // caller must ensure there's an available block
    ASSERT(_blockNum > 0);
    size_t blockSize = SizeClasses[scIdx].blockSize;
    char* ret = _block;
    _block += *(ptrdiff_t*)_block + blockSize;
    _blockNum--;
    return ret;
}

inline void TCacheBin::PopList(char* block, uint32_t length)
{
    ASSERT(_blockNum >= length);

    _block = block;
    _blockNum -= length;
}

// use tls init exec model
extern __thread TCacheBin TCache[MAX_SZ_IDX] LFMALLOC_TLS_INIT_EXEC LFMALLOC_CACHE_ALIGNED;

void FillCache(size_t scIdx, TCacheBin* cache);
void FlushCache(size_t scIdx, TCacheBin* cache);

#endif // __TCACHE_H_
