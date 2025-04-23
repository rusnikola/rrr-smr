MYDEPS = \
	NatarajanMittalTreeEBR.hpp \
	DWNatarajanMittalTreeEBR.hpp\
	HarrisMichaelLinkedListEBR.hpp \
	DWHarrisMichaelLinkedListEBR.hpp\
	HarrisMichaelLinkedListHP.hpp \
	DWHarrisMichaelLinkedListHP.hpp\
	MSQueueEBR.hpp \
	MSQueueABAEBR.hpp\
	ModQueueABAEBR.hpp \
	MSQueueHP.hpp\
	MSQueueABAHP.hpp \
	ModQueueABAHP.hpp

CFLAGS = -O3 -std=c++14 bench.cpp

ARCH = $(shell uname -m)

ifeq ($(ARCH),x86_64) # x84-64: enable cmpxchg16b
	CFLAGS += -mcx16
else # ARM64: use v8.1-a for casp/cas
	CFLAGS += -march=armv8.1-a
endif

bench: $(MYDEPS) bench.cpp BenchmarkLists.hpp
	clang++ $(CFLAGS) -o bench -lstdc++ -lpthread -lmimalloc

all: bench

clean:
	rm -f bench
