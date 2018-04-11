
#include "tcache.h"

// thread cache, uses tsd/tls
// one cache per thread
thread_local TCacheBin TCache[MAX_SZ_IDX];


