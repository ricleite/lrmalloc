
#include <cassert>
#include <sys/mman.h>

#include "pages.h"

void* PageAlloc(size_t size)
{
    assert((size & PAGE_MASK) == 0);

    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED)
        ptr = nullptr;
 
    return ptr;
}

void PageFree(void* ptr, size_t size)
{
    assert((size & PAGE_MASK) == 0);

    int ret = munmap(ptr, size);
    assert(ret == 0);
}

