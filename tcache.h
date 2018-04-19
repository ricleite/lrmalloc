
#ifndef __TCACHE_H_
#define __TCACHE_H_

#include "defines.h"
#include "size_classes.h"
#include "log.h"

struct TCacheBin
{
private:
    char* _block = nullptr;
    uint32_t _blockNum = 0;

public:
    // common, fast ops
    void PushBlock(char* block);
    char* PopBlock(); // can return nullptr
    char* PeekBlock() const { return _block; }
    uint32_t GetBlockNum() const { return _blockNum; }

    // slow operations like fill/flush handled in cache user
};

inline void TCacheBin::PushBlock(char* block)
{
    // block has at least sizeof(char*)
    *(char**)block = _block;
    _block = block;
    _blockNum++;
}

inline char* TCacheBin::PopBlock()
{
    // caller must ensure there's an available block
    ASSERT(_blockNum > 0);

    char* ret = _block;
    _block = *(char**)_block;
    _blockNum--;
    return ret;
}

// use tls init exec model
extern __thread TCacheBin TCache[MAX_SZ_IDX]
    LFMALLOC_TLS_INIT_EXEC LFMALLOC_ATTR_CACHE_ALIGNED;

#endif // __TCACHE_H_

