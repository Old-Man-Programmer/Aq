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
    if(!basic)
	return 0;
    printf("\n"
	   "%dFORI=%dTO%d:READB:POKEI,B:NEXTI\n"
	   "%dPOKE14340,%d:POKE14341,%d:X=USR(0)\n"
	   "RUN\n", lno, start_addr, cur_addr - 1, lno + 1,
	   start_addr % 256, start_addr / 256);
    return 0;
}
