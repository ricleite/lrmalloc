#include <cassert>

#include "../size_classes.h"

#define SIZE_CLASS_bin_yes(blockSize, pages) { blockSize, pages * PAGE },
#define SIZE_CLASS_bin_no(blockSize, pages)

#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
    SIZE_CLASS_bin_##bin((1U << lg_grp) + (ndelta << lg_delta), pgs)


int main()
{
    SizeClassData SizeClasses[MAX_SZ_IDX] = { { 0, 0 }, SIZE_CLASSES };
    // each superblock has to contain several blocks
    // and it has to contain blocks *perfectly*
    //  e.g no space left after last block
    for (size_t scIdx = 1; scIdx < MAX_SZ_IDX; ++scIdx) {
        SizeClassData& sc = SizeClasses[scIdx];
        // size class large enough to store several elements
        assert(sc.sbSize >= (sc.blockSize * 2));
        assert((sc.sbSize % sc.blockSize) == 0);
        assert(sc.sbSize < (PAGE * PAGE));
    }
}
