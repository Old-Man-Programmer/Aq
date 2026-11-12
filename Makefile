prefix=/usr/local

CC=gcc

VERSION=0.2
AQ_DEST=Aq
BINDIR=${prefix}/bin
OBJS=main.o z80.o mem.o screen.o io.o aqsnd.o aqjoy.o
CFLAGS+=$(shell pkg-config portaudio-2.0 --cflags)
CFLAGS+=$(shell sdl-config --libs --cflags)
CLIBS=$(shell pkg-config ncursesw portaudio-2.0 --libs)

#CFLAGS+=-O2 -Wall -Wextra
CFLAGS+=-ggdb -Wall -Wextra -Wmissing-declarations -Wmissing-prototypes

#CFLAGS=-O4 -Wall
#LDFLAGS=-s

all:	Aq

$(AQ_DEST):	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

$(OBJS): %.o:	%.c Aq.h instr.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(AQ_DEST) *.o *~

archive:	clean
	tar zcf ../Aq-$(VERSION).tgz -C .. `cat .files`
