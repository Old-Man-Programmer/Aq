/* $Copyright: $
 * Copyright (c) 2022-2025 by Thomas Moore (darktjm@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

extern void tokenize(const char *, char *);

int main(int argc, const char **argv)
{
    char buf[128], tok[128];
    for(int i = 1; i < argc; i++) {
	/* TODO: support renaming using e.g. <fname>:<tapename> */
	FILE *f = fopen(argv[i], "r");
	const char *en = strrchr(argv[i], '.');
	char *t;
	uint16_t lptr = 0x3901;
	int lline = -1;
	if(!f) {
	    perror(argv[i]);
	    exit(1);
	}
	fwrite("\377\377\377\377\377\377\377\377\377\377\377\377\0", 13, 1, stdout);
	if(!en)
	    en = argv[i] + strlen(argv[i]);
	if(en - argv[i] > 6)
	    en = argv[i] + 6;
	printf("%.*s", (int)(en - argv[i]), argv[i]);
	for(int j = en - argv[i]; j < 6; j++)
	    putchar(0);
	fwrite("\377\377\377\377\377\377\377\377\377\377\377\377\0", 13, 1, stdout);
	while(fgets(buf, sizeof(buf), f)) {
	    int lno, tlen = strlen(buf);
	    if(tlen > 80) { /* actually, 77 is the Aq limit */
		fputs("Line too long\n", stderr);
		exit(1);
	    }
	    if(!tlen)
		continue;
	    buf[--tlen] = 0;
	    tokenize(buf, tok);
	    for(t = tok; *t == ' '; t++);
	    if(!isdigit(*t))
		continue; /* ignore interactive statements: REM, PRINT, etc */
	    lno = strtol(t, &t, 10);
	    while(*t == ' ')
		t++;
	    if(!*t)
		continue; /* don't suport deleting lines. */
	    if(lno <= lline) {
		fprintf(stderr, "Line %d out of order\n", lno);
		exit(1);
	    }
	    lline = lno;
	    tlen = strlen(t);
	    lptr += tlen + 5;
	    putchar(lptr&0xff);
	    putchar(lptr/256);
	    putchar(lno&0xff);
	    putchar(lno/256);
	    fputs(t, stdout);
	    putchar(0);
	}
	fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 15, 1, stdout);
    }
}
