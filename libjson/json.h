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
#ifndef _JSON_H
#define _JSON_H 1

// JSON value types
typedef enum {
  VDT_STRING, VDT_NUMBER, VDT_OBJECT, VDT_ARRAY, VDT_TRUE, VDT_FALSE, VDT_NULL
} vdt_t;

struct value {
  vdt_t type;
  union {
    void *any;
    char *s;
    double n;
    struct pair *o;	// An object
    struct array *a;
  };
};

struct array {
  long length;
  struct value *a;	// a may be NULL if length == 0.
};

// An object is a list of pairs:
struct pair {
  char *name;
  struct value v;
  struct pair *next;
};

typedef struct array array_t;
typedef struct pair pair_t;
typedef struct value value_t;

// Exported functions:
value_t *json_decode(const char *s);
value_t *json_decode_file(const char *filename);
char *json_encode(const value_t *v);
char *json_pretty(const value_t *v);
int json_line(void);

value_t *json_objval(const value_t *v, const char *name);
value_t *json_arrayval(const value_t *v, int index);
char *json_strval(const value_t *v);
double json_numval(const value_t *v);
long json_length(const value_t *v);

int *json_intarray(const value_t *v, long *length);
float *json_floatarray(const value_t *v, long *length);
char **json_strarray(const value_t *v, long *length);

void json_freeval(value_t *val, int freeval);
void json_copyval(value_t *dst, const value_t *src);
void json_set_objval(value_t *obj, const char *name, const value_t *val);
void json_free_objval(value_t *obj, const char *name);

#endif
