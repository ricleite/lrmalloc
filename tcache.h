
#ifndef __TCACHE_H_
#define __TCACHE_H_

#include "defines.h"
#include "size_classes.h"

struct TCacheBin
{
private:
    char* _block = nullptr;
    size_t _blockNum = 0;

public:
    // common, fast ops
    void PushBlock(char* block);
    char* PopBlock(); // can return nullptr
    size_t GetBlockNum() const { return _blockNum; }

    // slow operations like fill/flush handled in cache user
};

inline void TCacheBin::PushBlock(char* block)
{
    // block has at least sizeof(char*)
    *(char**)block = _block;
    _block = block;
    ++_blockNum;
}

inline char* TCacheBin::PopBlock()
{
    if (UNLIKELY(_block == nullptr))
        return nullptr;

    char* ret = _block;
    _block = *(char**)_block;
    --_blockNum;
    return ret;
}

// uses tsd/tls
extern thread_local TCacheBin TCache[MAX_SZ_IDX];

#endif // __TCACHE_H_

