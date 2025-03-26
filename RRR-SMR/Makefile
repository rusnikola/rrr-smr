
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
	ModQueueABAHP.hpp\

bench: $(MYDEPS) bench.cpp BenchmarkLists.hpp
	clang++ -mcx16 -O3 -std=c++14 bench.cpp -o bench -lstdc++ -lpthread -lmimalloc

all: bench

clean:
	rm -f bench
