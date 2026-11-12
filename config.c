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

bool _config_readonly = false;

const char *_config_file = "config.json";

static value_t *_config = NULL, *_last_config;
static value_t _defconfig = { .type = VDT_OBJECT };

void add_default(const char *name, const value_t *val)
{
  json_set_objval(&_defconfig, name, val);
}

void add_default_str(const char *name, const char *val)
{
  value_t v = { .type = VDT_STRING, .s = (char *)val };
  add_default(name, &v);
}

void add_default_num(const char *name, double val)
{
  value_t v = { .type = VDT_NUMBER, .n = val };
  add_default(name, &v);
}

void add_default_type(const char *name, vdt_t t)
{
  value_t v = { .type = t };
  add_default(name, &v);
}

static bool merge_includes(const value_t *i, const char *fn);

static bool merge_config_file(const char *fn)
{
  static int depth = 0;
  if(depth == 16) {
    fprintf(stderr, "%s: include nesting too deep (possible loop)\n", fn);
    // exit(1); // maybe this is a bit harsh
    return false;
  }
  errno = 0;
  value_t *v = json_decode_file(fn);
  if(!v) {
    if(errno) {
      perror(fn); // error reading file is not OK
      return false;
    }
    return true; // blank is OK, I guess
  }
  if(v->type != VDT_OBJECT) {
    json_freeval(v, true);
    fprintf(stderr, "Error reading config file %s: not a valid JSON object\n", fn);
    return false; // invalid format is not OK
  }
  if(!_last_config) {
    _last_config = malloc(sizeof(*_last_config));
    json_copyval(_last_config, v);
  }
  // Canonicalize key descriptors (and validate at the same time)
  // In the end, due to ordering, this isn't necessary for proper operation
  // because internal storage is decoded binary descriptor, anyway.
  // The only advantage is that the user can be warned of unintentional dups.
  // This is slower than it should be due to unsorted lists.
  value_t *k = json_objval(v, "keys");
  if(k && k->type != VDT_OBJECT) {
    fprintf(stderr, "Warning: %s: ignoring invalid \"keys\" (not an object)\n",
	    fn);
    json_free_objval(v, "keys");
    k = NULL;
  }
  if(k && k->o) {
    for(pair_t **pp = &k->o, *p; (p = *pp); ) {
      if(p->v.type != VDT_STRING && p->v.type != VDT_NULL) {
	fprintf(stderr, "warning: %s: ignoring binding %s: action is not a string or null\n", fn, p->name);
	*pp = p->next;
	json_freeval(&p->v, false);
	free(p);
	continue;
      }
      char *nn = canonicalize_key(p->name);
      if(!nn) {
	fprintf(stderr, "warning: %s: ignoring binding %s: key parse error\n", fn, p->name);
	*pp = p->next;
	json_freeval(&p->v, false);
	free(p);
	continue;
      }
      free(p->name);
      p->name = nn;
      for(pair_t **qp = &k->o, *q; (q = *qp) != p; qp = &q->next)
	if(!strcmp(q->name, nn)) {
	  fprintf(stderr, "Warning: %s: duplicate key binding for %s; keeping \"%s\" instead of \"%s\"\n",
		  fn, nn, p->v.type == VDT_STRING ? p->v.s : "",
		  q->v.type == VDT_STRING ? q->v.s : "");
	  *qp = q->next;
	  json_freeval(&q->v, false);
	  free(q);
	  break;
	}
      pp = &p->next;
    }
  }
  if(!_config) {
    _config = v;
    const value_t *i = config_val("include");
    if(i && !merge_includes(i, fn))
      return false;
  } else {
    for(pair_t *p = v->o; p; p = p->next) {
      if(!strcmp(p->name, "keys")) {
	if(!p->v.o)
	  continue;
	value_t *b = json_objval(_config, "keys");
	if(!b || !b->o) {
	  json_set_objval(_config, "keys", &p->v);
	  continue;
	}
	for(pair_t *bp = p->v.o; bp; bp = p->next)
	  json_set_objval(b, bp->name, &bp->v);
      } else if(strcmp(p->name, "read_only") &&
	        strcmp(p->name, "include"))
	json_set_objval(_config, p->name, &p->v);
    }
    const value_t *i = json_objval(v, "include");
    if(i && !merge_includes(i, fn)) {
      json_freeval(v, true);
      return false;
    }
    json_freeval(v, true);
  }
  return true;
}

static bool merge_includes(const value_t *i, const char *fn)
{
  const array_t *a = i->a;
  if(i->type != VDT_ARRAY) {
    char *p = json_pretty(i);
    fprintf(stderr, "Invalid \"include\": %s in %s (value must be array of strings)\n", p, fn);
    free(p);
    return false;
  }
  for(int n = 0; n < a->length; n++) {
    if(a->a[n].type != VDT_STRING) {
      char *p = json_pretty(&a->a[n]);
      fprintf(stderr, "Invalid \"include\" %d (%s) in %s (value must be string)\n",
	      n + 1, p, fn);
      free(p);
      return false;
    }
    const char *s = a->a[n].s;
    if(s[0] == '/') {
      if(!merge_config_file(s)) {
	fprintf(stderr, "Error reading config file \"%s\"\n", s);
	return false;
      }
      continue;
    }
    char buf[strlen(s) + strlen(fn)]; // no need for +1; either s or sub-fn+s
    strcpy(buf, fn);
    char *lsl = strrchr(buf, '/');
    if(lsl)
      strcpy(lsl + 1, s);
    else
      strcpy(buf, s);
    if(!access(buf, R_OK)) {
      if(!merge_config_file(buf)) {
	fprintf(stderr, "Error reading config file \"%s\"\n", s);
	return false;
      }
      continue;
    }
    const char *cf = find_xdg_file(XDG_CONFIG, "Aq", s);
    if(!cf) {
      fprintf(stderr, "Error reading config file \"%s\": file not found\n", s);
      return false;
    }
    char *nf = strdup(cf);  // cf may be clobbered by nested include
    if(!merge_config_file(nf))
      cf = NULL;
    free(nf);
    if(!cf) {
      fprintf(stderr, "Error reading config file \"%s\"\n", s);
      return false;
    }
  }
  return true;
}

void read_config(void)
{
  if(!_config_file)
    return;
  const char *cf = find_xdg_file(XDG_CONFIG, "Aq", _config_file);
  if(!cf && strchr(_config_file, '/'))
    cf = _config_file;
  if(cf) {
    char *tcf = strdup(cf); // cf may be clobbered by "include"
    merge_config_file(tcf);
    free(tcf);
  }
  add_default_type("read_only", VDT_FALSE);
  _config_readonly = config_bool("read_only");
}

const value_t *config_val(const char *name)
{
  const value_t *ret = json_objval(_config, name);
  if(ret)
    return ret;
  return json_objval(&_defconfig, name);
}

const char *config_str(const char *name)
{
  const value_t *v = config_val(name);
  // There should always be a default, so no need to check for !v
  if(v->type == VDT_STRING)
    return v->s;
  // However, if v is not a string, ignore v and switch to default
  else if(v->type != VDT_NULL)
    fprintf(stderr, "Warning: config option %s ignored; not a string\n", name);
  v = json_objval(&_defconfig, name);
  return v->type == VDT_STRING ? v->s : NULL; // default may be NULL
}

double config_num(const char *name)
{
  const value_t *v = config_val(name);
  // There should always be a default, so no need to check for !v
  if(v->type == VDT_NUMBER)
    return v->n;
  // However, if v is not a number, ignore v and switch to default
  fprintf(stderr, "Warning: config option %s ignored; not a number\n", name);
  return json_objval(&_defconfig, name)->n;
}

bool config_bool(const char *name)
{
  const value_t *v = config_val(name);
  if(v->type == VDT_TRUE)
    return true;
  if(v->type != VDT_FALSE) {
    fprintf(stderr, "Warning: config option %s ignored; not a boolean\n", name);
    return json_objval(&_defconfig, name)->type == VDT_TRUE;
  }
  return false;
}

static bool config_equal(const value_t *cv, const value_t *val)
{
  vdt_t t = val ? val->type : VDT_NULL;

  // FIXME: take _config vs. _last_config discrepancy into account
  // both blank strings
  if(cv->type == VDT_NULL)
    return t == VDT_NULL || (t == VDT_STRING && (!val->s || !val->s[0]));
  // both strings, and equal
  if(cv->type == VDT_STRING)
    return t == VDT_STRING && !strcmp(val->s, cv->s);
  // both numbers, and equal
  if(cv->type == VDT_NUMBER)
    return cv->n == val->n;
  // both booleans, and equal
  if(cv->type == VDT_TRUE || cv->type == VDT_FALSE)
    return t == cv->type;
  // no attempt at checking aggregates for match
  return 0;
}

void write_config(void)
{
  if(!_config_file || _config_readonly)
    return;
  FILE *f;
  if(!strchr(_config_file, '/')) {
    mkpath(find_xdg_file(XDG_CONFIG, "Aq", NULL));
    f = fopen(find_xdg_file(XDG_CONFIG, "Aq/config.json", NULL), "w");
  } else
    f = fopen(_config_file, "w");
  if(!f)
    return;
  char *fv = json_pretty(_last_config);
  fputs(fv, f);
  putc('\n', f);
  fclose(f);
  free(fv);
}

void set_config(const char *name, const value_t *val)
{
  value_t *v = json_objval(_config, name), *cv = json_objval(&_defconfig, name);

  if(!v)
    v = cv;
  if(config_equal(v, val))
    return;
  if(!_config) {
    _config = malloc(sizeof(*_config));
    _config->type = VDT_OBJECT;
    _config->o = NULL;
  }
  if(!_last_config) {
    _last_config = malloc(sizeof(*_last_config));
    _last_config->type = VDT_OBJECT;
    _last_config->o = NULL;
  }
  if(v == cv || !config_equal(cv, val)) {
    json_set_objval(_config, name, val);
    json_set_objval(_last_config, name, val);
  } else {
    json_free_objval(_config, name);
    json_free_objval(_last_config, name);
  }
  write_config();
}

void set_config_str(const char *name, const char *val)
{
  value_t v = { .type = val && val[0] ? VDT_STRING : VDT_NULL, .s = (char *)val };
  set_config(name, &v);
}

void set_config_num(const char *name, double val)
{
  value_t v = { .type = VDT_NUMBER, .n = val };
  set_config(name, &v);
}

void set_config_bool(const char *name, bool val)
{
  value_t v = { .type = val ? VDT_TRUE : VDT_FALSE };
  set_config(name, &v);
}
