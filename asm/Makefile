prefix=/usr/local

CC=gcc

VERSION=1.0
AQ_DEST=Aq
IHX2BIN_DEST=ihx2bin
BINDIR=${prefix}/bin
CFLAGS+=$(shell pkg-config sdl2 --cflags)
CLIBS=$(shell pkg-config sdl2 --libs) -lm

SUBDIRS=sndtest exc cam80-12 eb

CFLAGS+=-O3 -Wall -Wextra
#CFLAGS+=-ggdb -Wall -Wextra -Wmissing-declarations -Wmissing-prototypes

#CFLAGS=-O4 -Wall
#LDFLAGS=-s

all:	
	for x in $(SUBDIRS); do $(MAKE) -C $$x all; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean; done

distclean:	clean
	for x in $(SUBDIRS); do $(MAKE) -C $$x distclean; done
