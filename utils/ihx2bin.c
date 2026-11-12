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
 * them with 0.  This does not pad out the binary output to ROM size,
 * so you may need dd to do that:
 *    ./ihx2bin <my.ihx | dd bs=8k conv=sync iflag=fullblock of=my.bin
 */

static uint8_t gethex()
{
    char c[3];
    c[0] = getchar(); c[1] = getchar(); c[2] = 0;
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
    int basic = argc > 1 && !strcmp(argv[1], "-b");
    int tape = argc > 1 && !strcmp(argv[1], "-t");
    static uint8_t tbuf[65536];
    uint8_t *tptr = tbuf;
    while((c = getchar()) >= 0) {
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
		putchar('\n');
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
		    printf("\n%dDATA", lno++);
		else
		    putchar(',');
	    }
	    uint8_t dat = gethex();
	    sum += dat;
	    if(basic)
		printf("%d", (int)dat);
	    else if(tape)
		*tptr++ = dat;
	    else
		putchar(dat);
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
	    putchar(0xff);
	putchar(0);
	int nz = 6;
	if(argc > 2) {
	    nz = 6 - strlen(argv[2]);
	    printf("%.6s", argv[2]);
	}
	for(int i = 0; i < nz; i++)
	    putchar(0);
	for(int i = 0; i < 12; i++)
	    putchar(0xff);
	putchar(0);
	putchar(0x31); /* link->next line */
	putchar(0x39);
	putchar(10); /* line# */
	putchar(0);
	putchar('V');
	putchar(0xb0); /* = */
	fputs("0:", stdout);
	putchar(0x85); /* DIM */
	printf("A(%05d):", (cur_addr - start_addr + 3)/4);
	putchar('V');
	putchar(0xb0); /* = */
	putchar(0xc1); /* PEEK */
	fputs("(14552)", stdout);
	putchar(0xa8); /* + */
	fputs("256", stdout);
	putchar(0xaa); /* * */
	putchar(0xc1); /* PEEK */
	fputs("(14553)", stdout);
	putchar(0xa8); /* + */
	fputs("7:", stdout);
	putchar(0x9a); /* CLOAD */
	putchar(0xaa); /* * */
	putchar('A');
	putchar(0); /* EOL */
	putchar(0x56); /* Next line link */
	putchar(0x39);
	putchar(15); /* line # */
	putchar(0);
	putchar(0x94); /* poke */
	fputs("14340,V", stdout);
	putchar(0xad); /* AND */
	fputs("255:", stdout);
	putchar(0x94); /* poke */
	fputs("14341,V", stdout);
	putchar(0xab); /* / */
	fputs("256:V", stdout);
	putchar(0xb0); /* = */
	putchar(0xb5); /* USR */
	fputs("(V)", stdout);
	putchar(0); /* EOL */
	putchar(0); /* link to next line -- end of program */
	putchar(0);
	for(int i = 0; i < 16; i++)
	    putchar(0);
	for(int i = 0; i < 12; i++)
	    putchar(0xff);
	putchar(0);
	fputs("######", stdout);
	fwrite(tbuf, cur_addr - start_addr, 1, stdout);
	for(int i = 0; i < 19; i++) /* +3 for padding */
	    putchar(0);
    }
    if(!basic)
	return 0;
    printf("\n"
	   "%dFORI=%dTO%d:READB:POKEI,B:NEXTI\n"
	   "%dPOKE14340,%d:POKE14341,%d:X=USR(0)\n"
	   "RUN\n", lno, start_addr, cur_addr - 1, lno + 1,
	   start_addr % 256, start_addr / 256);
    return 0;
}
