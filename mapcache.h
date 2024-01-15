/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#ifndef __MAPCACHE_H_
#define __MAPCACHE_H_

#include "log.h"
#include "pages.h"
#include "size_classes.h"
#include <sys/mman.h>

#define MAPCACHE_SIZE 64

struct MapCacheBin {
private:
    char* _block = nullptr;
    uint32_t _blockNum = 0;

public:
    // Map MAPCACHE_SIZE superblocks in one go and then consume 1 by 1
    char* Alloc();
    // Unmap superblocks immediately in SB_SIZE chunks
    void Free(char*);
    // Used for thread termination to unmap what remains
    void Flush();
};

inline char* MapCacheBin::Alloc()
{
    if (_blockNum == 0) {
        _block = (char*)PageAlloc(SB_SIZE * MAPCACHE_SIZE);
        if (_block == nullptr) {
            return nullptr;
        }
        _blockNum = MAPCACHE_SIZE;
    }
    char* ret = _block;
    _block += SB_SIZE;
    _blockNum--;
    return ret;
}

inline void MapCacheBin::Free(char* block)
{
    PageFree(block, SB_SIZE);
}

inline void MapCacheBin::Flush()
{
    if (_blockNum > 0) {
        PageFree(_block, SB_SIZE * _blockNum);
    }
}

// use tls init exec model
extern __thread MapCacheBin sMapCache LFMALLOC_TLS_INIT_EXEC LFMALLOC_CACHE_ALIGNED;

#endif // __MAPCACHE_H_
