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

extern int _line;
extern struct token _cur;

static value_t *newvalue(vdt_t type, void *value)
{
  value_t *v = malloc(sizeof(value_t));
  v->type = type;
  v->any = value;
  return v;
}

static pair_t *newpair(char *name, value_t *v)
{
  pair_t *p = malloc(sizeof(pair_t));
  p->name = name;
  p->v = *v;
  p-> next = NULL;
  return p;
}

static value_t *parse_array(void)
{
  array_t *a = malloc(sizeof(array_t));
  value_t *v;

  a->length = 0;
  a->a = NULL;

  do {
    // Putting this in here Allows a hanging , prior to ]:
    if (accept(T_CBRAC)) return newvalue(VDT_ARRAY, a);

    if ((v = parse_value()) == NULL) return NULL;

    if (a->length == 0) a->a = v;
    else {
      a->a = realloc(a->a, sizeof(value_t) * (size_t)(a->length+1));
      a->a[a->length] = *v;
    }
    a->length++;
  } while (accept(T_COMMA));

  if (accept(T_CBRAC)) return newvalue(VDT_ARRAY, a);
  fprintf(stderr,"libjson: Missing closing ] in input on line %d\n", _line);
  return NULL;
}

static pair_t *parse_pair(void)
{
  char *name;
  value_t *v;

  // TODO: Add support for identifiers as keys:
  if (_cur.tok != T_STRING) {
    fprintf(stderr,"libjson: Invalid key in key/value pair on line %d\n", _line);
    return NULL;
  }
  name = strdup(_cur.buf);
  match(_cur.tok);

  if (!accept(T_COLON)) {
    fprintf(stderr,"libjson: Missing : in key/value pair on line %d\n", _line);
    return NULL;
  }

  if ((v = parse_value()) == NULL) {
    fprintf(stderr,"libjson: Missing value in key/value pair on line %d\n", _line);
    return NULL;
  }

  return newpair(name, v);
}

static value_t *parse_object(void)
{
  pair_t *o = NULL, *p, *e;

  do {
    // Putting this here allows a hanging comma at end of object:
    if (accept(T_CCBRACE)) return newvalue(VDT_OBJECT, o);

    if ((p = parse_pair()) == NULL) return NULL;
    if (o == NULL) e = o = p;
    else {
      e->next = p;
      e = p;
    }
  } while(accept(T_COMMA));

  if (accept(T_CCBRACE)) return newvalue(VDT_OBJECT, o);
  fprintf(stderr,"libjson: Missing } for object on line %d\n", _line);
  return NULL;
}

value_t *parse_value(void)
{
  value_t *v;

  switch(_cur.tok) {
    case T_NUMBER:
      v = newvalue(VDT_NUMBER, NULL);
      v->n = _cur.val;
      match(_cur.tok);
      return v;
    case T_STRING:
      v = newvalue(VDT_STRING, strdup(_cur.buf));
      match(_cur.tok);
      return v;
    case T_TRUE:
      match(_cur.tok);
      return newvalue(VDT_TRUE, NULL);
    case T_FALSE:
      match(_cur.tok);
      return newvalue(VDT_FALSE, NULL);
    case T_NULL:
      match(_cur.tok);
      return newvalue(VDT_NULL, NULL);
    case T_OBRAC:
      match(_cur.tok);
      return parse_array();
    case T_OCBRACE:
      match(_cur.tok);
      return parse_object();
    default:
      fprintf(stderr,"libjson: Unknown token %d on line %d\n", _cur.tok, _line);
      return NULL;
  }
}
