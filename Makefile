
CCX=g++
DFLAGS=-ggdb -g -fno-omit-frame-pointer
CXXFLAGS=-shared -fPIC -std=gnu++14 -O2 -Wall $(DFLAGS)

LDFLAGS=-ldl -pthread

OBJFILES=lfmalloc.o size_classes.o pages.o pagemap.o tcache.o thread_hooks.o

default: lfmalloc.so lfmalloc.a

%.o : %.cpp
	$(CCX) $(CXXFLAGS) -c -o $@ $< $(LDFLAGS)

lfmalloc.so: $(OBJFILES)
	$(CCX) $(CXXFLAGS) -o lfmalloc.so $(OBJFILES) $(LDFLAGS)

lfmalloc.a: $(OBJFILES)
	ar rcs lfmalloc.a $(OBJFILES)

clean:
	rm -f *.so *.o *.a
