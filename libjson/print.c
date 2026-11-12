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

char *output = NULL;
size_t opos, osize;
bool pre_indent = true;

static size_t oprint(char *fmt, ...)
{
  size_t size;
  ssize_t printed;
  va_list ap;

  va_start(ap, fmt);
  size = osize-opos;
  printed = vsnprintf(output+opos, size, fmt, ap);
  va_end(ap);
  if (printed < 0) return opos;
  if (printed < (ssize_t)size) return (opos += (size_t)printed);
  // if printed >= size, then not enough space was available in the output buffer
  // grow it and do it again.  Grow it in 1K increments until it is definitely
  // enough to contain what we want to print:
  do {
    printed -= 1024;
    osize += 1024;
  } while (printed >= 0);
  output = realloc(output, sizeof(char) * osize);

  va_start(ap, fmt);
  size = osize-opos;
  printed = vsnprintf(output+opos, size, fmt, ap);
  va_end(ap);

  return opos += (size_t)printed;
}

/**
 * char	  : any-Unicode-character-except-"-or-\-or-control-character
 *	  | \" | \\ | \/ | \b | \f | \n | \r | \t| \u four-hex-digits 
 */
static size_t print_string(char *s)
{
  oprint("\"");
  for(int i=0; s[i]; i++) {
    switch(s[i]) {
      case '"':
      case '\\':
	oprint("\\%c",s[i]);
	break;
      case '\b': oprint("\\b"); break;
      case '\f': oprint("\\f"); break;
      case '\n': oprint("\\n"); break;
      case '\r': oprint("\\r"); break;
      case '\t': oprint("\\t"); break;
      default:
	if (iscntrl(s[i])) oprint("\\u%04o", s[i]);
	else oprint("%c", s[i]);
    }
  }
  return oprint("\"");
}

static size_t print_array(array_t *a)
{
  oprint("[");
  for(int i=0; i < a->length; i++) {
    print_value(&(a->a[i]));
    if (i+1 < a->length) oprint(",");
  }
  return oprint("]");
}

static void print_pair(pair_t *p)
{
  print_string(p->name);
  oprint(":");
  print_value(&(p->v));
}

static size_t print_object(pair_t *o)
{
  oprint("{");
  while(o) {
    print_pair(o);
    if (o->next) oprint(",");
    o = o->next;
  }
  return oprint("}");
}

size_t print_value(const value_t *v)
{
  switch(v->type) {
    case VDT_NUMBER:	return oprint("%lg", v->n);
    case VDT_TRUE:	return oprint("true");
    case VDT_FALSE:	return oprint("false");
    case VDT_NULL:	return oprint("null");
    case VDT_STRING:	return print_string(v->s);
    case VDT_ARRAY:	return print_array(v->a);
    case VDT_OBJECT:	return print_object(v->o);
    default:		return 0;
  }
}

static size_t pprint(char *s, int depth)
{
  if (pre_indent) {
    pre_indent = false;
    return oprint("%*s%s", depth*2, "", s);
  }
  return oprint("%s", s);
}

static size_t pretty_print_array(array_t *a, int depth)
{
  oprint("[");
  for(int i=0; i < a->length; i++) {
    pretty_print_value(&(a->a[i]),depth);
    if (i+1 < a->length) oprint(", ");
  }
  return oprint("]");
}

static void pretty_print_pair(const pair_t *p, int depth)
{
  oprint("%*s", depth*2, "");
  print_string(p->name);
  oprint(":");
  pre_indent = false;
  pretty_print_value(&(p->v),depth);
}

static size_t pretty_print_object(pair_t *o, int depth)
{
  pprint("{\n",depth);
  pre_indent = true;
  while(o) {
    pretty_print_pair(o,depth+1);
    if (o->next) oprint(",\n");
    else oprint("\n");
    pre_indent = true;
    o = o->next;
  }
  return pprint("}", depth);
}

size_t pretty_print_value(const value_t *v, int depth)
{
  switch(v->type) {
    case VDT_NUMBER:	return oprint("%lg", v->n);
    case VDT_TRUE:	return oprint("true");
    case VDT_FALSE:	return oprint("false");
    case VDT_NULL:	return oprint("null");
    case VDT_STRING:	return print_string(v->s);
    case VDT_ARRAY:	return pretty_print_array(v->a, depth);
    case VDT_OBJECT:	return pretty_print_object(v->o, depth);
    default:		return 0;
  }
}
