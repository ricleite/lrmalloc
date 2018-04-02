
CC=gcc
CCX=g++
DFLAGS=-ggdb -g -fno-omit-frame-pointer
CFLAGS=-shared -fPIC -std=gnu11 -O2 -Wall $(DFLAGS)

# -mcx16 allows the compiler to assume the cpu supports
#  cmpxchg16b (e.g double cas) during execution
# assuming this support shouldn't be a problem, see:
# https://superuser.com/questions/187254/how-prevalent-are-old-x64-processors-lacking-the-cmpxchg16b-instruction
# @todo: -O2
CXXFLAGS=-shared -fPIC -mcx16 -std=gnu++14 -O0 -Wall $(DFLAGS) \
		 -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc \
		 -fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign \
		 -fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc \
		 -fno-builtin

LDFLAGS=-ldl -pthread $(DFLAGS)

default: lfmalloc.so lfmalloc.a

lfmalloc.so: lfmalloc.cpp size_classes.cpp pages.cpp
	$(CCX) $(LDFLAGS) $(CXXFLAGS) -o lfmalloc.so lfmalloc.cpp size_classes.cpp pages.cpp

lfmalloc.a: lfmalloc.so
	ar rcs lfmalloc.a lfmalloc.so

clean:
	rm -f *.so *.o *.a
