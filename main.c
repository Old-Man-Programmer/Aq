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

int *_keybuf, _nkeys=0;

FILE *_log, *_ilog;

const char *_gamerom = NULL;
const char *_tape = NULL;
bool _extendedbasic = false, _batchmode = false;
int _argc;
char **_argv;

int main(int argc, char **argv)
{
  char *basicfile = NULL;
  _log = _ilog = NULL;

  _argc = argc;
  _argv = argv;

  setlocale(LC_ALL, "en_US.utf-8");

  int n, i, j;
  bool optf = true;
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
	  if(!*_gamerom)
	    _gamerom = NULL;
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
	case 'E':
	  _extendedbasic = true;
	  break;
	case 'B':
	  _batchmode = true;
	  _turbo = TURBO_BATCH;
	  break;
	 case 'f':
	  _fullscreen = true;
	  break;
	 case 'n':
	  _config_file = NULL;
	  break;
	 case 'c':
	  if (argv[n] == NULL) {
	    fprintf(stderr, "Missing file name to -c option\n");
	    exit(1);
	  }
	  _config_file = argv[n++];
	  break;
	}
      }
    } else {
      // words
    }
  }
  // This has to be done after cmd line parsing for _config_file
  // And setting value must keep any cmd line overrides
  if(_config_file) {
    read_config();
    add_default_type("cartridge", VDT_NULL);
    // retain cmd line override
    _gamerom = _gamerom ? _gamerom : config_str("cartridge");
    add_default_type("tape", VDT_NULL);
    // retain cmd line override
    _tape = _tape ? _tape : config_str("tape");
    // retain boolean cmd line overrides set by init_gui_config()
    bool eb = _extendedbasic, fs = _fullscreen;
    keymode_t km = _keymode;
    init_gui_config();
    if(eb)
      _extendedbasic = true;
    if(fs)
      _fullscreen = true;
    if(km == SCAN)
      _keymode = SCAN;
  }
  load_basic(basicfile);

  if(!_batchmode && (SDL_Init(SDL_INIT_VIDEO)==-1)) { 
    printf("Could not initialize SDL: %s.\n", SDL_GetError());
    return -1;
  }

  reset();
/* NOT REACHED */
  return 0;
}

void load_basic(const char *basicfile)
{
  int fd;
  if (basicfile != NULL && (fd = open_file(basicfile, "bas")) >= 0) {
    struct stat st;
    fstat(fd, &st);
    _keybuf = malloc(sizeof(int) * (st.st_size+1));
    char *buf = malloc(st.st_size+1);

    _nkeys = read(fd, buf, st.st_size);
    for(int i=0; i < _nkeys; i++) _keybuf[i] = findkey(buf[i]);
    _keybuf[st.st_size] = 0;

    close(fd);
    free(buf);
    if(_nkeys > 0) _turbo |= TURBO_BASIC;
  }
}

void quit(void)
{
  if(!_batchmode) {
    aqsnd_reset(); // prevent hang due to underrun
    stop_sdl_events();
    screen_shutdown();
    SDL_Quit();
  }
  exit(0);
}

/* Find a standard file (given by user as f) */
/* subdir is e.g. roms tapes etc. */
/* sys is system dir for rom, tape, etc. */
/* If wd is non-NULL, return dir (could be) found in instead of opening fd */
/* buf must be pointer to NULL pointer; free if non-NULL on return */
/*  (wd might point to this, so use that first) */
/* If fd is non-NULL, just try opening (ignore return value) */
static const char *find_file(const char *f, const char *subdir,
			     char **buf, int *fd)
{
  const char *ls = f ? strrchr(f, '/') : NULL;

  if(fd)
    *fd = -1;
  /* absolute */
  if(f && *f == '/') {
    if(fd) {
      *fd = open(f, O_RDONLY);
      return NULL; // Don't bother computing unused wd
    }
    *buf = strdup(f);
    if(ls == f)
      (*buf)[1] = 0;
    else
      (*buf)[ls - f] = 0;
    return *buf;
  }
  /* current dir */
  if(f && !access(f, R_OK)) {
    if(fd) {
      *fd = open(f, O_RDONLY);
      return NULL; // Don't bother computing unused wd
    }
    if(ls) {
      *buf = strdup(f);
      (*buf)[ls - f] = 0;
      return *buf;
    } else
      return "";
  }
  /* <subdir>/ */
  int slen = strlen(subdir);
  if(f) {
    char *tbuf = (char *)malloc(strlen(f) + slen + 2);
    memcpy(tbuf, subdir, slen);
    tbuf[slen] = '/';
    strcpy(tbuf + slen + 1, f);
    bool ex = !access(tbuf, R_OK);
    if(ex && fd)
      *fd = open(tbuf, O_RDONLY);
    free(tbuf);
    if(ex)
      return subdir;
  }
  /* XDG */
  /* note: if blank, and XDG dir exists, that will be the default */
  char cbuf[4 + slen];
  memcpy(cbuf, "Aq/", 3);
  strcpy(cbuf + 3, subdir);
  const char *fn = find_xdg_file(XDG_DATA, cbuf, f ? f : ".");
  if(fn) {
    if(fd) {
      *fd = open(fn, O_RDONLY);
      return NULL; // Don't bother computiing unused wd
    }
    *buf = strdup(fn);
    char *ls = strrchr(*buf, '/');
    if(ls)
      *ls = 0;
    return *buf;
  }
  /* if blank, and <subdir> dir exists, but not XDG, use that */
  if(!f) {
    char tbuf[slen+3];
    memcpy(tbuf, subdir, slen);
    strcpy(tbuf + slen, "/.");
    int ex = !access(tbuf, R_OK);
    if(ex)
      return subdir;
  }
  /* othrewise, force to system */
  *buf = malloc(sizeof(SYS_DIR) + slen + (fd && fn ? strlen(fn) + 1 : 0));
  memcpy(*buf, SYS_DIR, sizeof(SYS_DIR) - 1);
  memcpy(*buf + sizeof(SYS_DIR) - 1, subdir, slen);
  if(fd && fn) {
    (*buf)[sizeof(SYS_DIR) - 1 + slen] = '/';
    strcpy(*buf + sizeof(SYS_DIR) + slen, fn);
    *fd = open(*buf, O_RDONLY);
  } else
    (*buf)[sizeof(SYS_DIR) - 1 + slen] = 0;
  return *buf;
}

int open_file(const char *f, const char *dir)
{
  char *buf = NULL;
  int ret;
  find_file(f, dir, &buf, &ret);
  if(buf)
    free(buf);
  return ret;
}

const char *file_from(const char *f, const char *dir, char **buf)
{
  return find_file(f, dir, buf, NULL);
}

// FIXME: this should be in a better place
static int mkpathw(char *p)
{
  char *lsl;
  int ret;
  if(!access(p, F_OK))
    return 0;
  lsl = strrchr(p, '/');
  if(!lsl)
    return -1;
  *lsl = 0;
  if((ret = mkpathw(p)))
    return ret;
  *lsl = '/';
  return mkdir(p, 0777);
}

int mkpath(const char *p)
{
  char *todo = strdup(p);
  int ret;
  if(!todo)
    return -1;
  ret = mkpathw(todo);
  free(todo);
  return ret;
}
