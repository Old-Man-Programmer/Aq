/* $Copyright: $
 * Copyright (c) 2020-2025 by Steve Baker (steve.baker.llc@gmail.com)
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
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include "json.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#define K	1024

typedef enum {
  T_STRING, T_NUMBER, T_OBRAC, T_CBRAC, T_OCBRACE, T_CCBRACE, T_COMMA, T_COLON,
  T_TRUE, T_FALSE, T_NULL,
  T_EOI, T_UNKNOWN
} token_t;

struct token {
  token_t tok;
  char *buf;
  unsigned buflen;
  double val;
};

void startlex(void);
void match(token_t tok);
int accept(token_t tok);

value_t *parse_value(void);
size_t print_value(const value_t *v);
size_t pretty_print_value(const value_t *v, int depth);

extern const char *input;
extern char *output;
extern size_t opos, osize;
extern int _line;
