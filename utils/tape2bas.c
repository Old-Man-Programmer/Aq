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

extern const char *expand_tok(char tok);

static unsigned char mustgetchar(void)
{
    int c = getchar();
    if(c >= 0)
	return c;
    fputs("Unexpected EOF in tape file\n", stderr);
    exit(1);
}

int main(int argc, const char **argv)
{
    const char *fname = argc > 1 ? argv[1] : NULL;
    int c;
    char tname[6];
    if(fname && !strncmp(fname, "######", 6)) {
	fputs("Arrays not supported.\n", stderr);
	exit(1);
    }
    while(1) {
	while(!(c = mustgetchar()));
	while(c == 0xff)
	    c = mustgetchar();
	if(c) {
	    fputs("Invalid tape format\n", stderr);
	    exit(1);
	}
	for(int i = 0; i < 6; i++)
	    tname[i] = mustgetchar();
	if(fname && strncmp(fname, tname, 6)) {
	    int nz = 0;
	    fprintf(stderr, "Skipping %.6s\n", tname);
	    while((c = getchar()) >= 0) {
		if(c == 0xff && nz >= 10)
		    break;
		if(c)
		    nz = 0;
		else
		    ++nz;
	    }
	    if(c < 0) {
		fprintf(stderr, "File %s not found\n", fname);
		exit(1);
	    }
	    continue;
	}
	if((c = mustgetchar()) != 0xff) {
	    fputs("File not preset\n", stderr);
	    exit(1);
	}
	while((c = getchar()) == 0xff);
	if(c) {
	    fputs("Invalid tape format\n", stderr);
	    exit(1);
	}
	/* I'm not going to support link games:
	 *   All lines are just 0-terminated text.
	 *   Link 0 terminates the program.
	 */
	while(1) {
	    c = mustgetchar();
	    c = c + 256 * mustgetchar();
	    if(!c)
		return 0;
	    c = mustgetchar();
	    c = c + 256 * mustgetchar();
	    printf("%d ", c);
	    while((c = mustgetchar())) {
		if(c >= 0x80) {
		    const char *t = expand_tok(c);
		    if(t)
			fputs(t, stdout);
		    else
			putchar(c);
		} else
		    putchar(c);
	    }
	    putchar('\n');
	}
    }
}
