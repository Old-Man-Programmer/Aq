prefix=/usr/local

CC=gcc

IHX2BIN_DEST=ihx2bin
BINDIR=${prefix}/bin
#CFLAGS+=$(shell pkg-config sdl2 --cflags)
#CLIBS=$(shell pkg-config sdl2 --libs) -lm
CLIBS+=-lm

CFLAGS+=-O3 -Wall -Wextra
#CFLAGS+=-ggdb -Wall -Wextra -Wmissing-declarations -Wmissing-prototypes

all:	ihx2bin

$(IHX2BIN_DEST): ihx2bin.c
	$(CC) -o $(IHX2BIN_DEST) ihx2bin.c

clean:
	rm -f *.o *~

distclean:	clean
	rm -f $(IHX2BIN_DEST)
