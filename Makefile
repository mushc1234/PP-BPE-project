CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SRCS      = src/main_seq.cpp src/bpe.cpp
DEPS      = src/bpe.hpp src/unicode_tables.inc

all: bin/bpe_seq

bin/bpe_seq: $(SRCS) $(DEPS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ $(LDFLAGS)

# Same optimization level as the baseline, plus debug info / frame pointers so that
# perf and valgrind can attribute time to functions and source lines. See docs/profiling.md.
profile: bin/bpe_seq_prof

bin/bpe_seq_prof: $(SRCS) $(DEPS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -g -fno-omit-frame-pointer $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -rf bin

.PHONY: all profile clean
