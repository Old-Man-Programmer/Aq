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

struct token _cur = {0};

static int nextc(void);
static void unget(int c);

int _line = 1, _prev = 0;

static struct keyword {
  char *name;
  token_t tval;
} keywords[] = {
  {"true", T_TRUE},
  {"false", T_FALSE},
  {"null", T_NULL},
  {NULL, T_UNKNOWN}
};

/**
 * JSON strings are always double-quoted and may contain 0 or more characters
 * with the following allowed for characters:
 * char	  : any-Unicode-character-except-"-or-\-or-control-character
 *	  | \" | \\ | \/ | \b | \f | \n | \r | \t| \u four-hex-digits 
 * For this assignment you do not need to support \u hex sequences, although
 * you may.
 */

static token_t lexstring(char **word, unsigned *maxword)
{
  int c;
  unsigned wp = 0;
  while ((c = nextc()) != '"') {
    if(c == '\\') {
      c = nextc();
      switch(c) {
       case 'b': c = '\b'; break;
       case 'f': c = '\f'; break;
       case 'n': c = '\n'; break;
       case 'r': c = '\r'; break;
       case 't': c = '\t'; break;
       case 'u': {
	 unsigned uc = 0;
	 for(int i = 0; i < 4; i++) {
	   c = nextc();
	   if(!isxdigit(c))
	     break; // silently stop parsing
	   c = toupper(c) - '0';
	   if(c > 9)
	     c -= 'A' - '9' + 1;
	   uc = (uc << 8) + (unsigned int)c;
	 }
       }
      }
    }
    (*word)[wp++] = (char)c;
    if(wp == *maxword)
      *word = realloc(*word, *maxword *= 2);
  }
  (*word)[wp] = '\0';
  return T_STRING;
}

static token_t lex(char **word, unsigned *maxword, double *val)
{
  char stop[] = " \t\n[]{},:\"", *end;
  unsigned wp = 0;
  int c = nextc();
  if(!*word || !*maxword)
    *word = malloc(*maxword = 20);
  (*word)[wp] = '\0';

  while (isspace(c)) c = nextc();

  switch(c) {
    case '\0':
    case EOF: return T_EOI;
    case '"': return lexstring(word, maxword);
    case '[': return T_OBRAC;
    case ']': return T_CBRAC;
    case '{': return T_OCBRACE;
    case '}': return T_CCBRACE;
    case ',': return T_COMMA;
    case ':': return T_COLON;
    default:
      do {
	(*word)[wp++] = (char)c;
	if(wp == *maxword)
	  *word = realloc(*word, *maxword *= 2);
	c = nextc();
      } while (c != '\0' && c != EOF && strchr(stop,c) == NULL );
      unget(c);
      (*word)[wp] = '\0';
      if (isdigit((*word)[0]) || (*word)[0] == '-') {
	*val = strtod(*word, &end);
	if (*end != '\0') {
	  printf("Malformed number\n");
	  return T_UNKNOWN;
	}
	return T_NUMBER;
      }
      for(int i=0; keywords[i].name != NULL; i++)
	if (strcasecmp(*word, keywords[i].name) == 0) return keywords[i].tval;
      printf("Lexed unknown token '%s'\n", *word);
      return T_UNKNOWN;
  }
  // Not reached
  return T_EOI;
}

const char *input = NULL;
static int save[K];
static int saved = 0;

static int nextc()
{
  static int c = 0;

  _prev = c;
  if (saved) {
    c = save[--saved];
  } else if (input) {
    c = *input;
    if (c) input++;
  } else c = EOF;

  if (_prev == '\n') _line++;
  return c;
}

/**
 * push: save[saved++] = c
 * pop : c = save[--saved];
 */
static void unget(int c)
{
  if (c == '\n') _line--;
  save[saved++] = c;
}

void startlex(void)
{
  _cur.tok = lex(&_cur.buf, &_cur.buflen, &_cur.val);
}

/**
 * Matches a given token or dies if it doesn't match
 */
void match(token_t tok)
{
  if (tok == _cur.tok) _cur.tok = lex(&_cur.buf, &_cur.buflen, &_cur.val);
  else {
    printf("Syntax error on line %d\n", _line);
    exit(1);
  }
}

/**
 * Accept (match) the current if it matches (and returns true (1))
 * otherwise indicates failure to match (returns 0), but is not fatal.
 */
int accept(token_t tok)
{
  if (_cur.tok == tok) {
    match(tok);
    return 1;
  }
  return 0;
}
