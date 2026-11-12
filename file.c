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
#include <dirent.h>

typedef struct file {
  char *name;
  mode_t mode;
} file_t;

void center(WINDOW *win, int line, int winwidth, char *text)
{
  mvwprintw(win, line, (winwidth/2)-(strlen(text)/2), "%s", text);
}

int dirfilter(const struct dirent *ent)
{
  if (strcmp(ent->d_name, ".") == 0) return false;
  return true;
}

file_t *getdir(char *path, int *nfiles)
{
  char fpath[PATH_MAX];
  struct stat st;
  struct dirent **namelist;
  int n = scandir(path, &namelist, dirfilter, alphasort);

  int nf = 0;
  file_t *files = malloc(sizeof(file_t) * (n+1));
  for(int i=0; i < n; i++) {
    snprintf(fpath, PATH_MAX, "%s/%s", path, namelist[i]->d_name);
    if (stat(fpath, &st) == 0) {
      files[nf].name = strdup(namelist[i]->d_name);
      files[nf].mode = st.st_mode;
      nf++;
    }
    free(namelist[i]);
  }
  files[nf].name = NULL;
  free(namelist);
  *nfiles = nf;
  return files;
}

void freefiles(file_t *files, int nfiles)
{
  for(int i=0; i < nfiles; i++) free(files[i].name);
  free(files);
}

void showfiles(WINDOW *fwin, file_t *files, int off, int curpos, int dirsonly)
{
  int sline = 4;
  for(int i=off; (sline < LINES-6) && files[i].name; i++) {
    if (dirsonly && (files[i].mode&S_IFMT) != S_IFDIR) continue;
    if (sline == curpos) attrset(A_REVERSE);
    mvwprintw(fwin, sline, 3, "%*s", COLS-6, files[i].name);
    if (sline++ == curpos) attrset(A_NORMAL);
  }
}

char *select_file(char *basepath, bool dironly)
{
  WINDOW *fwin = newwin(LINES-4, COLS-4, 2, 2);
  wborder(fwin,0,0,0,0,0,0,0,0);
  center(fwin, 0, COLS-4, "  File selection:  ");

  bool stop = false;
  char *selected, bpath[PATH_MAX];
  strcpy(bpath, basepath);

  nodelay(fwin,FALSE);
  int curpos = 0, fileoff = 0, key;
  do {
    int nfiles;
    file_t *files = getdir(bpath, &nfiles);

    showfiles(fwin, files, fileoff, curpos, dironly);
    switch ((key = getch())) {
      case KEY_UP:
      case KEY_DOWN:
      case KEY_NPAGE:
      case KEY_PPAGE:
      case ' ':
      case '\n':
	selected = files[fileoff+curpos].name;
	break;
    }
  } while (!stop);

  nodelay(fwin,TRUE);

  return strdup(bpath);
}
