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
#include <string.h>
#include <ctype.h>

static void prbuf(unsigned char *buf)
{
    for(int i = 0; i < 3 + 6 * 2; i++) {
	if(i >= 5)
	    i++;
//	if(isprint(buf[i]))
	if(isalnum(buf[i]))
	    putchar(buf[i]);
	else
	    printf("%02X", (unsigned)buf[i]); // was %dd, but % isn't necessary
    }
}

int main(int argc, const char **argv)
{
    const unsigned char magic[] = ",3$$7+";

    while(--argc > 0) {
	FILE *f = fopen(*++argv, "rb");
	if(!f) {
	    perror(*argv);
	    continue;
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	if(len <= 0) {
	    perror(*argv);
	    fclose(f);
	    continue;
	}
	if(len > 8192)
	    fseek(f, -8192, SEEK_END);
	else
	    fseek(f, 0, SEEK_SET);
	unsigned char buf[16];
	if(fread(buf, sizeof(buf), 1, f) != 1) {
	    perror(*argv);
	    fclose(f);
	    continue;
	}
	fclose(f);
	unsigned char cbuf[16];
	memcpy(cbuf, buf, sizeof(buf));
	//   0 1 2 3 4 5 6 7 8 9 A B C D E F
	//   H H H a h , h 3 h $ h $ h 7 h +
	// starting @ 4, all characters have offset subtracted and lshift*2
	// sum is (sum(1..12) + sum(adj4..adjE) + a) ^ buf[F]
	unsigned char sum = 78 + buf[3]; // sum(1..12) + a
	int is_magic = 1;
	for(int i = 4; i < 3 + 6 * 2; i++) {
	    unsigned char b = buf[i];
	    sum += b;
	    b = (b << 6) + (b >> 2) + i; // rshift*2 and add offset to revert
	    cbuf[i] = b;
	    if(i >= 5 && (i%1) && b != magic[(i - 6) / 2]) {
//		printf("?%02x %02x?", b, magic[(i - 6) / 2]);
		is_magic = 0;
	    }
	}
	sum ^= 0x70;
	if(buf[15] != (magic[5] - 15) << 2) {
//	    printf("?%02x %02x?", (unsigned)buf[15], ((unsigned)magic[5] - 15) << 2);
	    is_magic = 0;
	}
	if(!is_magic) {
	    printf("%s: no magic\n", *argv);
	    continue;
	}
	printf("%s: \"", *argv);
	prbuf(buf);
	printf("\"\n%s (xlate): \"", *argv);
	prbuf(cbuf);
	printf("\"\n%s (xor): %02X\n", *argv, sum);
    }
}
