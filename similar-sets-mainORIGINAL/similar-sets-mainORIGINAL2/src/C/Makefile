CC = gcc
LINK = ld
CFLAGS = -g
OBJECTS1 = config.o set.o qesa.o connector.o set2.o test-set2.o
OBJECTS2 = config.o set.o qesa.o connector.o set2.o set2hat.o test-hat.o
SLIBS = 
PROGRAM = set2

all : set2 hat

set2 : 	$(OBJECTS1) 
	$(LINK.c) -o $@ $(OBJECTS1) $(SLIBS)

hat : 	$(OBJECTS2) 
	$(LINK.c) -o $@ $(OBJECTS2) $(SLIBS)

config.o:	config.c

set.o:		set.c

qesa.o:		qesa.c

connector.o:	connector.c

set2.o:		set2.c

set2.o:		set2hat.c

test-set2.o:	test-set.c

