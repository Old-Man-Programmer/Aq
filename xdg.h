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
#ifndef _XDG_H
#define _XDG_H 1

/*
 * Support for X Free Desktop Guidelines config/data file locations
 * https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
 * (used v0.8).
 *
 * Placed in the Public Domain April 2023 by Thomas J. Moore.
 *
 * This takes into account the defaults as well as the ennvironment variables.
 * In brief, the default config file location is ~/.config, and the default
 * data location is ~/.local/share.  In addition, there is a search path,
 * which defaults to /etc/xdg and /usr/local/share:/usr/share, respectively.
 * The environment overrides are XDG_CONFIG_HOME, XDG_DATA_HOME,
 * XDG_CONFIG_DIRS and XDG_DATA_DIRS, respectively.  For state and cache
 * directories, there is only a user-specific directory: ~/.local/state and
 * ~/.cache, respectively, with environment overrides XDG_CACHE_HOME and
 * XDG_STATE_HOME, respectively.
 *
 * You should name your config file <app>.config (or something similar) or
 * place all config files in an <app> subdirectory.  All data, cache, and
 * state should be in an <app> subdirectory.
 */
typedef enum {
  /* RUNTIME not supported, since behavior can't be hard-coded */
  /* just use the XDG_RUNTIME_DIR env. var */
  XDG_CONFIG, XDG_DATA, XDG_CACHE, XDG_STATE /*, XDG_RUNTIME */
} xdg_type;

const char *find_xdg_file(xdg_type type, const char *prefix, const char *name);
/* Find file <name> in XDG path <type> under <prefix>, returning full path
 *   type: see xdg_type above.  NULL is returned if out-of-range.
 *   prefix = optional dir prefix (e.g. ~/.local/share/<prefix>)
 *   name = file name to find (access: R_OK; no guarantee it's a file)
 * If name==NULL, just return the default place to store based on prefix.
 *   Use this with a NULL prefix as well to obtain the default directory
 *     xdg_conf = find_xdg_file(XDG_CONFIG, NULL, NULL);
 *   Use this with an app prefix to find the directory to create/use
 *     conf_dir = find_xdg_file(XDG_CONFIG, "my_app", NULL);
 *   Use this with the file name as the prefix to find where to write file
 *     conf_file = find_xdg_file(XDG_CONFIG, "my_app.conf", NULL);
 *     conf_file = find_xdg_file(XDG_CONFIG, "my_app/config", NULL);
 * If you just want to see if a dir exists, set prefix to NULL and name=prefix
 *     data_dir = find_xdg_file(XDG_DATA, NULL, "my_app");
 * (or set name=".").
 *     data_dir = find_xdg_file(XDG_DATA, "my_app", ".");
 *     if(data_dir) { .. use data dir .. }
 */
/* NOTE:  This returns a pointer to a static buffer.  It's not thread safe,
 * and it will likely be clobbered if you call this funciton again.
 * It is definitely not thread safe.  It's meant to be used at program
 * initialization.
 */

#endif
