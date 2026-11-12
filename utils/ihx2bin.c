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
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Simple Intel Hex (ihx) format to binary converter, adapted from an
 * older ihx to Aquarius BASIC converter.  You can get the old behavior
 * using the -b command-line option.  Standard input is .ihx file, and
 * standard output is either a raw binary (no start address encoded) or
 * BASIC (start address of code is where program is POKEd into).
 *
 * This does not support gaps, although a future revision might auto-fill
 * them with 0.  To pad to ROM size, give the block size with -p <size>.
 * This will pad to a multiple of size, not to size.
 */

static FILE *inf, *outf;
static uint8_t gethex()
{
    char c[3];
    c[0] = getc(inf); c[1] = getc(inf); c[2] = 0;
    if(!isxdigit(c[0]) || !isxdigit(c[1])) {
	fputs("Bad file format\n", stderr);
	exit(1);
    }
    // yeah, I'm being lazy
    return strtol(c, NULL, 16);
}

int main(int argc, char **argv)
{
    int start_addr = -1, cur_addr = -1;
    int c;
    int lno = 10;
    int basic = 0;
    unsigned blksz = 0;
    const char *tape = NULL;
    while(argc > 1) {
	if(argv[1][0] != '-' || !argv[1][1] || argv[1][2])
	    break;
	argv++;
	argc--;
	if((*argv)[1] == '-')
	    break;
	switch((*argv)[1]) {
	  case 'b':
	    basic = 1;
	    break;
	  case 't':
	    if(!--argc) {
		fputs("Tape requires file name\n", stderr);
		exit(1);
	    }
	    tape = *++argv;
	    break;
	  case 'p':
	    if(!--argc || !(blksz = atoi(*++argv))) {
		fputs("-p requires a size arg\n", stderr);
		exit(1);
	    }
	    if(blksz <= 32) /* probably meant k */
		blksz *= 1024;
	    break;
	}
    }
    if(!(inf = argc > 1 ? fopen(argv[1], "r") : stdin)) {
	perror("input");
	exit(1);
    }
    if(!(outf = argc > 2 ? fopen(argv[2], "w") : stdout)) {
	perror("output");
	exit(1);
    }
    static uint8_t tbuf[65536];
    uint8_t *tptr = tbuf;
    while((c = getc(inf)) >= 0) {
	if(c != ':')
	    continue;
	uint8_t len = gethex(), sum = len;
	uint8_t ah = gethex(), al = gethex(), type = gethex();
	if(type) /* only care about type 0: data */
	    continue;
	sum += al + ah /* + type */;
	int addr = (int)ah * 256 + al;
	if(start_addr < 0) {
	    start_addr = cur_addr = addr;
	    if(basic)
		putc('\n', outf);
#if 0
	    else if(tape) {
		if(addr != 0x39B9) {
		    fprintf(stderr, "Wrong start addr %04X; expected 3948\n", addr);
		    exit(1);
		}
	    }
#endif
	} else if(addr != cur_addr) {
	    fputs("Data out of order\n", stderr);
	    exit(1);
	}
	for(int i = 0; i < len; i++) {
	    if(basic) {
		if(!((cur_addr - start_addr)%16))
		    fprintf(outf, "\n%dDATA", lno++);
		else
		    putc(',', outf);
	    }
	    uint8_t dat = gethex();
	    sum += dat;
	    if(basic)
		fprintf(outf, "%d", (int)dat);
	    else if(tape)
		*tptr++ = dat;
	    else
		putc(dat, outf);
	    cur_addr++;
	}
	sum += gethex();
	if(sum) {
	    fputs("Checksum error\n", stderr);
	    exit(1);
	}
    }
    if(start_addr < 0) {
	fputs("No data\n", stderr);
	exit(1);
    }
    if(tape) {
	for(int i = 0; i < 12; i++)
	    putc(0xff, outf);
	putc(0, outf);
	int nz = 6;
	if(tape) {
	    nz = 6 - strlen(tape);
	    fprintf(outf, "%.6s", tape);
	}
	for(int i = 0; i < nz; i++)
	    putc(0, outf);
	for(int i = 0; i < 12; i++)
	    putc(0xff, outf);
	putc(0, outf);
	putc(0x31, outf); /* link->next line */
	putc(0x39, outf);
	putc(10, outf); /* line# */
	putc(0, outf);
	putc('V', outf);
	putc(0xb0, outf); /* = */
	fputs("0:", outf);
	putc(0x85, outf); /* DIM */
	fprintf(outf,"A(%05d):", (cur_addr - start_addr + 3)/4);
	putc('V', outf);
	putc(0xb0, outf); /* = */
	putc(0xc1, outf); /* PEEK */
	fputs("(14552)", outf);
	putc(0xa8, outf); /* + */
	fputs("256", outf);
	putc(0xaa, outf); /* * */
	putc(0xc1, outf); /* PEEK */
	fputs("(14553)", outf);
	putc(0xa8, outf); /* + */
	fputs("7:", outf);
	putc(0x9a, outf); /* CLOAD */
	putc(0xaa, outf); /* * */
	putc('A', outf);
	putc(0, outf); /* EOL */
	putc(0x56, outf); /* Next line link */
	putc(0x39, outf);
	putc(15, outf); /* line # */
	putc(0, outf);
	putc(0x94, outf); /* poke */
	fputs("14340,V", outf);
	putc(0xad, outf); /* AND */
	fputs("255:", outf);
	putc(0x94, outf); /* poke */
	fputs("14341,V", outf);
	putc(0xab, outf); /* / */
	fputs("256:V", outf);
	putc(0xb0, outf); /* = */
	putc(0xb5, outf); /* USR */
	fputs("(V)", outf);
	putc(0, outf); /* EOL */
	putc(0, outf); /* link to next line -- end of program */
	putc(0, outf);
	for(int i = 0; i < 16; i++)
	    putc(0, outf);
	for(int i = 0; i < 12; i++)
	    putc(0xff, outf);
	putc(0, outf);
	fputs("######", outf);
	fwrite(tbuf, cur_addr - start_addr, 1, outf);
	for(int i = 0; i < 19; i++) /* +3 for padding */
	    putc(0, outf);
    }
    if(blksz && !basic && !tape) {
	unsigned int m = (cur_addr - start_addr) % blksz;
	if(m)
	    while(m++ < blksz)
		putc(0, outf);
    }
    if(!basic)
	return 0;
    fprintf(outf,"\n"
	   "%dFORI=%dTO%d:READB:POKEI,B:NEXTI\n"
	   "%dPOKE14340,%d:POKE14341,%d:X=USR(0)\n"
	   "RUN\n", lno, start_addr, cur_addr - 1, lno + 1,
	   start_addr % 256, start_addr / 256);
    return 0;
}
