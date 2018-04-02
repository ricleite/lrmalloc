
#ifndef __LOG_H
#define __LOG_H

#include <cstdio>

#define LFMALLOC_DEBUG 0

#if LFMALLOC_DEBUG
#define LOG_DEBUG(STR, ...) \
    fprintf(stdout, "%s:%d %s " STR "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);

#else
#define LOG_DEBUG(str, ...)

#endif

#define LOG_ERR(STR, ...) \
    fprintf(stderr, "%s:%d %s " STR "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define ASSERT(x) do { if (!(x)) abort(); } while (0)

#endif // _LOG_H

