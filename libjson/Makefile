# $Copyright: $
# Copyright (c) 2020-2025 by Steve Baker (steve.baker.llc@gmail.com)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
CC=gcc
LD=ld
SOCFLAGS=-fPIC -shared -fvisibility=hidden -ggdb
CFLAGS=-fPIC -static -fvisibility=hidden -ggdb -Wall -Wpedantic -Wextra -Wstrict-prototypes -Wshadow -Wconversion -Wdiscarded-qualifiers
OBJS=json.o lex.o parse.o print.o

all: libjson static

libjson: $(OBJS)
	$(CC) $(SOCFLAGS) -o libjson.so $(OBJS);
	strip --strip-all --discard-all libjson.so

static: $(OBJS)
	ar rcs libjson.a $(OBJS);

lex.o: lex.c internal.h json.h
	$(CC) $(CFLAGS) -c lex.c

json.o: json.c internal.h json.h
	$(CC) $(CFLAGS) -c json.c

parse.o: parse.c internal.h json.h
	$(CC) $(CFLAGS) -c parse.c

print.o: print.c internal.h json.h
	$(CC) $(CFLAGS) -c print.c

objclean:
	rm -f *.o

clean:	objclean
	rm -f libjson.so libjson.a
