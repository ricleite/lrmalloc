#
# Copyright (C) 2019 Ricardo Leite. All rights reserved.
# Licenced under the MIT licence. See COPYING file in the project root for details.
#

CCX=g++
DFLAGS=-ggdb -g -fno-omit-frame-pointer
CXXFLAGS=-shared -fPIC -std=gnu++14 -O2 -Wall $(DFLAGS) \
	-fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc \
	-fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign \
	-fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc \
	-fno-builtin -fsized-deallocation -fno-exceptions

LDFLAGS=-ldl -pthread

OBJFILES=lrmalloc.o size_classes.o pages.o pagemap.o tcache.o thread_hooks.o

default: lrmalloc.so lrmalloc.a

test: all_tests

%.o : %.cpp
	$(CCX) $(CXXFLAGS) -c -o $@ $< $(LDFLAGS)

lrmalloc.so: $(OBJFILES)
	$(CCX) $(CXXFLAGS) -o lrmalloc.so $(OBJFILES) $(LDFLAGS)

lrmalloc.a: $(OBJFILES)
	ar rcs lrmalloc.a $(OBJFILES)

all_tests: default basic.test

%.test : test/%.cpp
	$(CCX) $(DFLAGS) -o $@ $< lrmalloc.a $(LDFLAGS)

clean:
	rm -f *.so *.o *.a *.test
