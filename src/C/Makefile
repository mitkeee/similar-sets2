CC = gcc
LINK = ld
CFLAGS = -g -O3 -msse2
OBJECTS1 = config.o set.o qesa.o connector.o set2.o test-set2.o
OBJECTS2 = config.o set.o qesa.o connector.o set2.o set2hat.o test-hat.o
SKIPLIST_OBJS = skiplist.o test-skiplist.o
CSKIPLIST_OBJS = cskiplist.o test-cskiplist.o
CSKIPLIST_ENH_OBJS = cskiplist.o test-cskiplist-enhanced.o
CSKIPLIST_MILLION_OBJS = cskiplist.o test-cskiplist-million.o
SKIPLIST_BENCH_OBJS = cskiplist.o test-skiplist-benchmark.o
ASKIPLIST_OBJS = cskiplist.o askiplist.o test-askiplist.o
CACHE_BENCH_OBJS = cskiplist.o test-cache-benchmark.o
SIMD_BENCH_OBJS = cskiplist.o test-simd-benchmark.o
EYT_TEST_OBJS = cskiplist.o test-eytzinger.o
TEST_PROC_OBJS = config.o set.o qesa.o connector_csl.o cskiplist.o set2.o test-procedure.o
TEST_PROC_BASE_OBJS = config.o set.o qesa.o connector.o set2.o test-procedure.o
SLIBS = 
PROGRAM = set2

all : set2 hat skiptest cskiptest cskiptest-enh cskiptest-million skipbench askiptest cachebench simdbench eyttest branchless testproc

set2 : 	$(OBJECTS1) 
	$(LINK.c) -o $@ $(OBJECTS1) $(SLIBS)

hat : 	$(OBJECTS2) 
	$(LINK.c) -o $@ $(OBJECTS2) $(SLIBS)

skiptest : $(SKIPLIST_OBJS)
	$(LINK.c) -o $@ $(SKIPLIST_OBJS) $(SLIBS)

cskiptest : $(CSKIPLIST_OBJS)
	$(LINK.c) -o $@ $(CSKIPLIST_OBJS) $(SLIBS)

cskiptest-enh : $(CSKIPLIST_ENH_OBJS)
	$(LINK.c) -o $@ $(CSKIPLIST_ENH_OBJS) $(SLIBS)

cskiptest-million : $(CSKIPLIST_MILLION_OBJS)
	$(LINK.c) -o $@ $(CSKIPLIST_MILLION_OBJS) $(SLIBS)

skipbench : $(SKIPLIST_BENCH_OBJS)
	$(LINK.c) -o $@ $(SKIPLIST_BENCH_OBJS) $(SLIBS)

askiptest : $(ASKIPLIST_OBJS)
	$(LINK.c) -o $@ $(ASKIPLIST_OBJS) $(SLIBS)

cachebench : $(CACHE_BENCH_OBJS)
	$(LINK.c) -o $@ $(CACHE_BENCH_OBJS) $(SLIBS) -lpsapi

simdbench : $(SIMD_BENCH_OBJS)
	$(LINK.c) -o $@ $(SIMD_BENCH_OBJS) $(SLIBS) -lpsapi

eyttest : $(EYT_TEST_OBJS)
	$(LINK.c) -o $@ $(EYT_TEST_OBJS) $(SLIBS) -lpsapi

branchless : test-branchless.o
	$(LINK.c) -o $@ test-branchless.o $(SLIBS) -lpsapi

testproc : $(TEST_PROC_OBJS)
	$(LINK.c) -o $@ $(TEST_PROC_OBJS) $(SLIBS) -lpsapi

testproc-base : $(TEST_PROC_BASE_OBJS)
	$(LINK.c) -o $@ $(TEST_PROC_BASE_OBJS) $(SLIBS) -lpsapi

config.o:	config.c

set.o:		set.c

qesa.o:		qesa.c

connector.o:	connector.c

set2.o:		set2.c

set2.o:		set2hat.c

test-set2.o:	test-set.c

skiplist.o: skiplist.c skiplist.h
test-skiplist.o: test-skiplist.c skiplist.h
cskiplist.o: cskiplist.c cskiplist.h
test-cskiplist.o: test-cskiplist.c cskiplist.h
test-cskiplist-enhanced.o: test-cskiplist-enhanced.c cskiplist.h
test-cskiplist-million.o: test-cskiplist-million.c cskiplist.h
test-skiplist-benchmark.o: test-skiplist-benchmark.c cskiplist.h
askiplist.o: askiplist.c askiplist.h cskiplist.h
test-askiplist.o: test-askiplist.c askiplist.h
test-cache-benchmark.o: test-cache-benchmark.c cskiplist.h
test-simd-benchmark.o: test-simd-benchmark.c cskiplist.h
test-eytzinger.o: test-eytzinger.c cskiplist.h
test-branchless.o: test-branchless.c
connector_csl.o: connector_csl.c connector.h cskiplist.h
test-procedure.o: test-procedure.c config.h set.h qesa.h connector.h set2.h cskiplist.h

