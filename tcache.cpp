
#include "tcache.h"

// thread cache, uses tsd/tls
// one cache per thread
__thread TCacheBin TCache[MAX_SZ_IDX];

