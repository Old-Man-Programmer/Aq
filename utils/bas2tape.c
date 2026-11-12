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
