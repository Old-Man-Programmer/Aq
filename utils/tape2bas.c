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
