#
# Copyright (C) 2019 Ricardo Leite. All rights reserved.
# Licenced under the MIT licence. See COPYING file in the project root for details.
#

PREFIX?=/usr/local

CCX=g++
DFLAGS=-ggdb -g -fno-omit-frame-pointer
CXXFLAGS=-shared -fPIC -std=gnu++14 -O2 -Wall $(DFLAGS) \
	-fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc \
	-fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign \
	-fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc \
	-fno-builtin -fsized-deallocation -fno-exceptions

LDFLAGS=-ldl -pthread

OBJFILES=lrmalloc.o size_classes.o pages.o pagemap.o tcache.o thread_hooks.o

default: liblrmalloc.so liblrmalloc.a

test: all_tests

%.o : %.cpp
	$(CCX) $(CXXFLAGS) -c -o $@ $< $(LDFLAGS)

liblrmalloc.so: $(OBJFILES)
	$(CCX) $(CXXFLAGS) -o liblrmalloc.so $(OBJFILES) $(LDFLAGS)

liblrmalloc.a: $(OBJFILES)
	ar rcs liblrmalloc.a $(OBJFILES)

all_tests: default basic.test

%.test : test/%.cpp
	$(CCX) $(DFLAGS) -o $@ $< liblrmalloc.a $(LDFLAGS)

clean:
	rm -f *.so *.o *.a *.test

install: default
	install -d $(DESTDIR)$(PREFIX)/lib/
	install -m 644 liblrmalloc.so $(DESTDIR)$(PREFIX)/lib/
	install -m 644 liblrmalloc.a $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/include/
	install -m 644 lrmalloc.h $(DESTDIR)$(PREFIX)/include/
