CC = gcc
LINK = ld
CFLAGS = -g
OBJECTS1 = config.o set.o qesa.o connector.o set2.o test-set2.o
OBJECTS2 = config.o set.o qesa.o connector.o set2.o set2hat.o test-hat.o
SKIPLIST_OBJS = skiplist.o test-skiplist.o
CSKIPLIST_OBJS = cskiplist.o test-cskiplist.o
CSKIPLIST_ENH_OBJS = cskiplist.o test-cskiplist-enhanced.o
CSKIPLIST_MILLION_OBJS = cskiplist.o test-cskiplist-million.o
SLIBS = 
PROGRAM = set2

all : set2 hat skiptest cskiptest cskiptest-enh cskiptest-million

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

