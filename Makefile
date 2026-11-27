# bench-, data- and test-<version> for every v*/ folder, same flags.
#   make | make test | make clean      (Windows: mingw32-make)

CC      = gcc
CFLAGS ?= -O3 -march=native -Wall -Wextra

ifeq ($(OS),Windows_NT)
EXE = .exe
endif

VERSIONS := $(patsubst %/,%,$(dir $(wildcard v*/skiplist.c)))
BENCH    := $(VERSIONS:%=bench-%$(EXE))
DATA     := $(VERSIONS:%=data-%$(EXE))
TESTS    := $(VERSIONS:%=test-%$(EXE))

all: $(BENCH) $(DATA) $(TESTS)

bench-%$(EXE): bench/bench.c %/skiplist.c %/skiplist.h
	$(CC) $(CFLAGS) -I$* -o $@ bench/bench.c $*/skiplist.c

data-%$(EXE): bench/dataset.c %/skiplist.c %/skiplist.h
	$(CC) $(CFLAGS) -I$* -o $@ bench/dataset.c $*/skiplist.c

test-%$(EXE): tests/test.c %/skiplist.c %/skiplist.h
	$(CC) $(CFLAGS) -I$* -o $@ tests/test.c $*/skiplist.c

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f bench-v* data-v* test-v*

.PHONY: all test clean
