prefix=/usr/local

CC=gcc

VERSION=0.5
AQ_DEST=Aq
IHX2BIN_DEST=ihx2bin
BINDIR=${prefix}/bin
OBJS=main.o z80.o mem.o screen.o io.o aqsnd.o aqjoy.o
CFLAGS+=$(shell pkg-config sdl2 --cflags)
CLIBS=$(shell pkg-config sdl2 --libs) -lm

SUBDIRS=utils asm bas

CFLAGS+=-O3 -Wall -Wextra
#CFLAGS+=-ggdb -Wall -Wextra -Wmissing-declarations -Wmissing-prototypes

#CFLAGS=-O4 -Wall
#LDFLAGS=-s

all:	Aq
	for x in $(SUBDIRS); do $(MAKE) -C $$x all; done

$(AQ_DEST):	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

$(OBJS): %.o:	%.c Aq.h instr.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *~ ilog log .errors
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean; done

distclean:	clean
	rm -f $(AQ_DEST) $(IHX2BIN_DEST)
	for x in $(SUBDIRS); do $(MAKE) -C $$x distclean; done

archive:	clean
	tar zcf ../Aq-$(VERSION).tgz -C .. `cat .files`
