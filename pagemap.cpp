
#include "pagemap.h"
#include "pages.h"
#include "log.h"

PageMap sPageMap;

void PageMap::Init()
{
    ASSERT(!_init);
    _init = true;

    // pages will necessarily be given by the OS
    // so they're already initialized and zero'd
    // PM_SZ is necessarily aligned to page size
    _pagemap = (std::atomic<PageInfo>*)PageAllocOvercommit(PM_SZ);
    ASSERT(_pagemap);
}
