
#ifndef __PAGEMAP_H
#define __PAGEMAP_H

#include <atomic>

#include "defines.h"
#include "size_classes.h"

// assuming x86-64, for now
// which uses 48 bits for addressing (e.g high 16 bits ignored)
// can ignore the bottom 12 bits (lg of page)
// insignificant high bits
#define PM_NHS 12
// insignificant low bits
#define PM_NLS LG_PAGE
// significant middle bits
#define PM_SB (64 - PM_NHS - PM_NLS)
// to get the key from a address
// 1. shift to remove insignificant low bits
// 2. apply mask of middle significant bits
#define PM_KEY_SHIFT PM_NLS
#define PM_KEY_MASK ((1ULL << PM_SB) - 1)

struct Descriptor;
// associates metadata to each allocator page
// implemented with a static array, but can also be implemented
//  with a multi-level radix tree

// contains metadata per page
// *has* to be the size of a single page
struct PageInfo
{
    Descriptor* desc;
};

#define PM_SZ ((1ULL << PM_SB) * sizeof(PageInfo))

static_assert(sizeof(PageInfo) == sizeof(uint64_t), "Invalid PageInfo size");

// lock free page map
// lazy-initialized on first call
class PageMap
{
public:
    PageInfo GetPageInfo(char* ptr);
    void SetPageInfo(char* ptr, PageInfo info);

private:
    void Init();
    size_t AddrToKey(char* ptr) const;

private:
    bool _init = false;
    // array based impl
    std::atomic<PageInfo>* _pagemap = { nullptr };
};

inline size_t PageMap::AddrToKey(char* ptr) const
{
    size_t key = ((size_t)ptr >> PM_KEY_SHIFT) & PM_KEY_MASK;
    return key;
}

inline PageInfo PageMap::GetPageInfo(char* ptr)
{
    if (UNLIKELY(!_init))
        Init();

    size_t key = AddrToKey(ptr);
    return _pagemap[key].load();
}

inline void PageMap::SetPageInfo(char* ptr, PageInfo info)
{
    if (UNLIKELY(!_init))
        Init();

    size_t key = AddrToKey(ptr);
    _pagemap[key].store(info);
}

extern PageMap sPageMap;

#endif // __PAGEMAP_H

