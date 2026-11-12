/* $Copyright: $
 * Copyright (c) 2022-2025 by Steve Baker (steve.baker.llc@gmail.com)
 *   and Thomas Moore (darktjm@gmail.com)
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
#include "Aq.h"

int *_keybuf, _nkeys=0, _kb[MAXKEYS+1];

FILE *_log, *_ilog;

const char *_gamerom = NULL;
const char *_tape = NULL;

int main(int argc, char **argv)
{
  char *basicfile = NULL;
  _log = _ilog = NULL;

  setlocale(LC_ALL, "en_US.utf-8");

  int n, i, j;
  bool optf = TRUE;
  for(n=i=1;i<argc;i=n) {
    n++;
    if (optf && argv[i][0] == '-' && argv[i][1]) {
      for(j=1;argv[i][j];j++) {
        switch(argv[i][j]) {
        case 'l':
          _log = fopen("log", "w");
	  if (_log == NULL) {
	    fprintf(stderr,"Unable to open ilog.\n");
	    exit(0);
	  } else {
	    fprintf(_log, "log started.\n");
	  }
          break;
	case 'i':
	  _ilog = fopen("ilog", "w");
	  if (_ilog == NULL) {
	    fprintf(stderr,"Unable to open ilog.\n");
	    exit(0);
	  } else {
	    fprintf(_ilog, "Ilog started.\n");
	    fflush(_ilog);
	  }
	  break;
	case 'b':
	  if (argv[n] == NULL) {
	    fprintf(stderr, "Missing filename to -b option\n");
	    exit(1);
	  }
	  basicfile = argv[n++];
	  break;
	case 'g':
	  if (argv[n] == NULL) {
	    fprintf(stderr, "Missing filename to -g option\n");
	    exit(1);
	  }
	  _gamerom = argv[n++];
	  break;
	case 't':
	  if (argv[n] == NULL) {
	    fprintf(stderr, "Missing file/dir name to -t option\n");
	    exit(1);
	  }
	  _tape = argv[n++];
	  break;
	case 'k':
	  _keymode = SCAN;
	  break;
	}
      }
    } else {
      // words
    }
  }

  if (basicfile != NULL) {
    int fd = open(basicfile, O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    _keybuf = malloc(sizeof(int) * (st.st_size+1));
    char *buf = malloc(st.st_size+1);

    _nkeys = read(fd, buf, st.st_size);
    for(int i=0; i < _nkeys; i++) _keybuf[i] = findkey(buf[i]);
    _keybuf[st.st_size] = 0;

    close(fd);
    free(buf);
  } else _keybuf = _kb;

  close(STDERR_FILENO);
  open(".errors", O_WRONLY|O_CREAT|O_TRUNC, 0666);

  initmem();
  io_init();
  reset();
/* NOT REACHED */
  return 0;
}

void quit(void)
{
//   int mem = open("memory", O_WRONLY | O_CREAT | O_TRUNC, 0666);
//   write(mem, _page[0x38]->ram, 0xFF);
//   close(mem);

  nocbreak();
  noraw();
  echo();
  endwin();
  exit(1);
}
