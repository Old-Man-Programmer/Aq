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
/*
 * Support for X Free Desktop Guidelines config/data file locations
 * See xdg.h for details.
 *
 * Placed in the Public Domain April 2023 by Thomas J. Moore.
 */
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "xdg.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static struct xdg_path {
  const char *env1, *def1, *envp, *defp;
  const char *dir, *path;
} xdg_conf[] = {
  { "XDG_CONFIG_HOME", "~/.config", "XDG_CONFIG_DIRS", "/etc/xdg" },
  { "XDG_DATA_HOME", "~/.local/share", "XDG_DATA_DIRS", "/usr/local/share:/usr/share" },
  { "XDG_CACHE_HOME", "~/.cache" },
  { "XDG_STATE_HOME", "~/.local/state" }
};

static const char *homedir = NULL;

const char *find_xdg_file(xdg_type type, const char *prefix, const char *name)
{
  struct xdg_path *xp = &xdg_conf[type];
  static char *buf = NULL;
  static int buflen = 0;
  const char *rd, *path;
  if((unsigned)type >= sizeof(xdg_conf)/sizeof(xdg_conf[0]))
    return NULL;
  if(!xp->dir && !(xp->dir = getenv(xp->env1))) {
    if(!homedir && (!(homedir = getenv("HOME")) || !*homedir)) {
      struct passwd *mypw = getpwuid(getuid());
      if(mypw)
	homedir = strdup(mypw->pw_dir);
      else {
	char p[PATH_MAX];
	homedir = getcwd(p, sizeof(p));
	if(homedir)
	  homedir = strdup(homedir);
      }
    }
    if(!homedir)
      homedir = "";
    char *d = (char *)malloc(strlen(homedir) + strlen(xp->def1) + 1);
    if(!d) {
      perror("homedir");
      exit(1);
    }
    strcpy(d, homedir);
    strcat(d, xp->def1 + 1);
    xp->dir = d;
  }
  if(!xp->path && xp->envp && !(xp->path = getenv(xp->envp)))
    xp->path = xp->defp;
  rd = xp->dir;
  path = xp->path;
  if(!name && !prefix)
    return rd;
  int nlen = name ? strlen(name) : 0;
  int pfxlen = prefix && *prefix ? strlen(prefix) + 1 : 0;
  int plen = path ? strlen(path) : 0;
  int dlen = strlen(rd);
  int needlen = pfxlen + nlen + (plen > dlen ? plen : dlen) + 2;
  if(needlen > buflen) {
    if(!buflen)
      buflen = 80;
    while(needlen > buflen)
      buflen *= 2;
    if(buf)
      buf = (char *)realloc(buf, needlen);
    else
      buf = (char *)malloc(needlen);
    if(!buf) {
      perror("XDG dir");
      exit(1);
    }
  }
  /* See if it's in main dir */
  strcpy(buf, rd);
  char *p = buf + dlen;
  if(dlen && p[-1] != '/')
    *p++ = '/';
  if(prefix) {
    memcpy(p, prefix, pfxlen); // includes 0
    p += pfxlen - 1;
    if(!name)
      return buf;
    if(p[-1] != '/')
      *p++ = '/';
  }
  strcpy(p, name);
  if(!access(buf, R_OK))
    return buf;
  /* Search path */
  if(!path)
    return NULL;
  /* XDG doesn't seem to allow escapes for : */
  while(1) {
    const char *c = strchr(path, ':');
    size_t len = c ? (size_t)(c - path) : strlen(path);
    memcpy(buf, path, len);
    p = buf + len;
    if(len && p[-1] != '/')
      *p++ = '/';
    if(pfxlen) {
      memcpy(p, prefix, pfxlen - 1);
      p += pfxlen - 1;
      if(p[-1] != '/')
	*p++ = '/';
    }
    strcpy(p, name);
    if(!access(buf, R_OK))
      return buf;
    if(!c)
      return NULL;
    path = c + 1;
  }
}
