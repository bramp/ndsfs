CC      = gcc
LD      = gcc
#CFLAGS =  -Wall -Wcast-align -g
CFLAGS  =  -Wall -g

COMPILE  = $(CC) $(CFLAGS) $(INCLUDES)

FUSEFLAGS = `pkg-config fuse --cflags --libs`

all: ndsfs

ndsfs: ndsfs.o
	$(COMPILE) $(FUSEFLAGS) -o ndsfs ndsfs.o

ndsfs.o: ndsfs.c header.h
	$(COMPILE) -c ndsfs.c

release:
	tar cjf ndsfs-`git rev-parse --short HEAD`.tar.bz2 Makefile ndsfs.c header.h

clean:
	rm *.o ndsfs
