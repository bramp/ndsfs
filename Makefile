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
	tar cjf ndsfs-1.1.tar.bz2 Makefile ndsfs.c header.h

clean:
	rm *.o ndsfs

layton1:
	-umount /mnt/layton1
	./ndsfs /home/bramp/nds/Professor\ Layton\ and\ the\ Curious\ Village.nds /mnt/layton1

layton2:
	-umount /mnt/layton2
	./ndsfs /home/bramp/nds/Professor\ Layton\ and\ the\ Diabolical\ Box.nds /mnt/layton2

layton3-jp:
	-umount /mnt/layton3-jp
	./ndsfs /home/bramp/nds/Layton\ Kyouju\ to\ Saigo\ no\ Jikan\ Ryokou.nds /mnt/layton3-jp
