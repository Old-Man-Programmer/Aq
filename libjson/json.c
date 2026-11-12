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
#include "internal.h"

/**
 * JSON grammar: From https://json.org/
 *
 * json	   : value
 * 
 * value   : string | number | object | array | true | false | null
 * 
 * object  : {}
 * 	   | { members } 
 * 
 * members : pair
 * 	   | pair , members
 * 
 * pair    : string : value
 * 
 * array   : []
 * 	   | [ elements ]
 * 
 * elements: value
 *	   | value , elements
 * 
 */

/**
 * The following is a GCC option to make these functions globally visible
 * (i.e. non-hidden) which we've made the default with the -fvisibility=hidden
 * compile option. Ideally a library only exports symbols that are part of the
 * API, and nothing else, to minimize name-space pollution.
 */
#ifdef __GNUC__
#define _visible __attribute__((__visibility__ ("default") ))
#else
#define _visible
#endif
value_t *json_decode(const char *s) _visible;
value_t *json_decode_file(const char *filename) _visible;
char *json_encode(const value_t *v) _visible;
char *json_pretty(const value_t *v) _visible;
int json_line(void) _visible;

value_t *json_objval(const value_t *v, const char *name) _visible;
value_t *json_arrayval(const value_t *v, int index) _visible;
char *json_strval(const value_t *v) _visible;
double json_numval(const value_t *v) _visible;
long int json_length(const value_t *v) _visible;

int *json_intarray(const value_t *v, long *length) _visible;
float *json_floatarray(const value_t *v, long *length) _visible;
char **json_strarray(const value_t *v, long *length) _visible;

void json_freeval(value_t *val, int freeval) _visible;
void json_copyval(value_t *dst, const value_t *src) _visible;
void free_objval(value_t *obj, const char *name) _visible;
void json_set_objval(value_t *obj, const char *name, const value_t *val) _visible;

value_t *json_decode(const char *s)
{
  input = s;
  startlex();
  return parse_value();
}

value_t *json_decode_file(const char *filename)
{
  char *data;
  struct stat st;
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
//     perror("open");
//     fprintf(stderr,"libjson: Failed to open file [%s] for reading.\n", filename);
    return NULL;
  }
  if (fstat(fd, &st) < 0) {
//     perror("stat");
    fprintf(stderr,"libjson: Could not stat file %s\n", filename);
    close(fd);
    return NULL;
  }

  data = malloc(sizeof(char) * (size_t)(st.st_size+1));
  if (data == NULL) {
    close(fd);
    fprintf(stderr,"libjson: Out of memory.\n");
    return NULL;
  }
  if (read(fd, data, (size_t)st.st_size) < st.st_size) {
    close(fd);
    free(data);
    return NULL;
  }
  close(fd);

  data[st.st_size] = '\0';
  value_t *v = json_decode(data);
  free(data);
  return v;
}

char *json_encode(const value_t *v)
{
  output = malloc(sizeof(char) * (osize=1024));
  opos = 0;
  print_value(v);
  return output;
}

char *json_pretty(const value_t *v)
{
  output = malloc(sizeof(char) * (osize=1024));
  opos = 0;
  pretty_print_value(v, 0);
  return output;
}

int json_line(void)
{
  return _line;
}

value_t *json_objval(const value_t *v, const char *name)
{
  if (v == NULL) return NULL;
  if (v->type != VDT_OBJECT) return NULL;
  for(struct pair *p = v->o; p; p = p->next) {
    if (strcasecmp(name, p->name) == 0) return &(p->v);
  }
  return NULL;
}

value_t *json_arrayval(const value_t *v, int index)
{
  if (v->type != VDT_ARRAY) return NULL;
  if (index < 0 || index >= v->a->length) return NULL;
  return &(v->a->a[index]);
}

char *json_strval(const value_t *v)
{
  if (v == NULL) return NULL;
  if (v->type != VDT_STRING) return NULL;
  return v->s;
}

double json_numval(const value_t *v)
{
  if (v == NULL) return 0.0;
  switch(v->type) {
    case VDT_NUMBER:
      return v->n;
    case VDT_TRUE:
      return 1.0;
    case VDT_FALSE:
      return 0.0;
    case VDT_STRING:
      return strtod(v->s,NULL);
    default:
      return 0.0;
  }
  return 0.0;
}

long json_length(const value_t *v)
{
  if (v == NULL) return -1;
  switch(v->type) {
    case VDT_STRING:
      return (long)strlen(v->s);
    case VDT_ARRAY:
      return v->a->length;
    default:
      return -1;
  }
  return -1;
}

int *json_intarray(const value_t *v, long *length)
{
  if (v == NULL || v->type != VDT_ARRAY) return NULL;
  *length = v->a->length;
  int *a = calloc((size_t)(*length)+1, sizeof(int));
  for(int i = 0; i < *length; i++) {
    a[i] = (int)json_numval(&v->a->a[i]);
  }
  return a;
}

float *json_floatarray(const value_t *v, long *length)
{
  if (v == NULL || v->type != VDT_ARRAY) return NULL;
  *length = v->a->length;
  float *a = calloc((size_t)(*length)+1, sizeof(float));
  for(int i = 0; i < *length; i++) {
    a[i] = (float)json_numval(&v->a->a[i]);
  }
  return a;
}

char **json_strarray(const value_t *v, long *length)
{
  char buf[K];

  if (v == NULL || v->type != VDT_ARRAY) return NULL;
  *length = v->a->length;
  char **a = calloc((size_t)(*length)+1, sizeof(char *));

  for(int i = 0; i < *length; i++) {
    value_t *vx = &v->a->a[i];
    if (vx == NULL) {
      a[i] = strdup("NULL");
    } else {
      switch(v->type) {
	case VDT_NUMBER:
	  sprintf(buf, "%f", v->n);
	  a[i] = strdup(buf);
	  break;
	case VDT_TRUE:
	  a[i] = strdup("true");
	  break;
	case VDT_FALSE:
	  a[i] = strdup("false");
	  break;
	case VDT_STRING:
	  a[i] = strdup(vx->s);
	  break;
	default:
	  a[i] = strdup("{object}");
      }
    }
  }
  return a;
}

void json_freeval(value_t *val, int freeval)
{
  if(!val)
    return;
  if(val->type == VDT_STRING && val->s) {
    free(val->s);
    val->s = NULL;
  } else if(val->type == VDT_OBJECT) {
    for(pair_t *cur = val->o, *next = NULL; cur; cur = next) {
      next = cur->next;
      json_freeval(&cur->v, 0);
      free(cur->name);
    }
    val->o = NULL;
  } else if(val->type == VDT_ARRAY && val->a) {
    for(long i = 0; i < val->a->length; i++)
      json_freeval(&val->a->a[i], 0);
    free(val->a);
    val->a = NULL;
  }
  if(freeval)
    free(val);
}

void json_copyval(value_t *dst, const value_t *src)
{
  *dst = *src;
  if(src->type == VDT_STRING && src->s)
    dst->s = strdup(src->s);
  else if(src->type == VDT_OBJECT) {
    pair_t **last = &dst->o;
    for(const pair_t *cur = src->o; cur; cur = cur->next, last = &(*last)->next) {
      *last = malloc(sizeof(pair_t));
      (*last)->name = strdup(cur->name);
      json_copyval(&(*last)->v, &cur->v);
    }
    *last = NULL;
  } else if(src->type == VDT_ARRAY && src->a) {
    dst->a = malloc(sizeof(*dst->a));
    dst->a->length = src->a->length;
    dst->a->a = malloc((size_t)src->a->length * sizeof(*dst->a->a));
    for(long i = 0; i < src->a->length; i++)
      json_copyval(&dst->a->a[i], &src->a->a[i]);
  }
}

// json_objval() returns &val, but I need &&pair for deletion
static pair_t **json_objval_ptr(value_t *v, const char *name)
{
  if (v == NULL) return NULL;
  if (v->type != VDT_OBJECT) return NULL;
  for(struct pair **p = &v->o; *p; p = &(*p)->next) {
    if (strcasecmp(name, (*p)->name) == 0) return p;
  }
  return NULL;
}

void json_free_objval(value_t *obj, const char *name)
{
  pair_t **old = json_objval_ptr(obj, name);
  if(old) {
    pair_t *n = (*old)->next;
    json_freeval(&(*old)->v, 0);
    free(*old);
    *old = n;
  }
}

void json_set_objval(value_t *obj, const char *name, const value_t *val)
{
  value_t *v = json_objval(obj, name);
  if(!v) {
    pair_t **p;
    for(p = &obj->o; *p; p = &(*p)->next);
    *p = malloc(sizeof(**p));
    (*p)->next = NULL;
    (*p)->name = strdup(name);
    v = &(*p)->v;
  } else
    json_freeval(v, 0);
  json_copyval(v, val);
}
