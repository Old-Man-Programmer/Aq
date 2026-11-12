/* $Copyright: $
 * Copyright (c) 2022-2025 by Thomas Moore (darktjm@gmail.com)
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
#include <ctype.h>
#include <string.h>

/* Suspiciously missing: \313 \314 \315 \316 */
/* Unassigned: \340..\377 */
static const char * const tokens[] = {
    "\252*",
    "\250+",
    "\251-",
    "\253/",
    "\261<",
    "\260=",
    "\257>",
    "\264ABS",
    "\255AND",
    "\305ASC",
    "\300ATN",
    "\306CHR$",
    "\325CIRCLE", /* Extended BASIC */
    "\231CLEAR",
    "\232CLOAD",
    "\323CLS", /* Extended BASIC */
    "\226CONT",
    "\222COPY",
    "\275COS",
    "\233CSAVE",
    "\203DATA",
    "\223DEF",
    "\205DIM",
    "\332DIR", /* QuickDisk DOS */
    "\337DISKCOPY", /* QuickDisk DOS */
    "\330DRAW", /* Extended BASIC */
    "\320EDIT", /* Extended BASIC */
    "\200END",
    "\274EXP",
    "\242FN",
    "\201FOR",
    "\266FRE",
    "\326GET", /* Extended BASIC */
    "\214GOSUB",
    "\210GOTO",
    "\212IF",
    "\336INIT", /* QuickDisk DOS */
    "\244INKEY",
    "\204INPUT",
    "\263INT",
    "\331KILL", /* QuickDisk DOS */
    "\307LEFT$",
    "\302LEN",
    "\207LET",
    "\324LINE", /* Extended BASIC */
    "\227LIST",
    "\230LLIST",
    "\333LOAD", /* QuickDisk DOS */
    "\321LOCATE", /* Extended BASIC */
    "\273LOG",
    "\267LPOS",
    "\221LPRINT",
    "\322MENU", /* Extended BASIC */
    "\311MID$",
    "\237NEW",
    "\202NEXT",
    "\246NOT",
    "\220ON",
    "\256OR",
    "\301PEEK",
    "\312POINT",
    "\224POKE",
    "\270POS",
    "\235PRESET",
    "\225PRINT",
    "\234PSET",
    "\327PUT", /* Extended BASIC */
    "\206READ",
    "\216REM",
    "\213RESTORE",
    "\215RETURN",
    "\310RIGHT$",
    "\272RND",
    "\211RUN",
    "\334SAVE", /* QuickDisk DOS */
    "\262SGN",
    "\276SIN",
    "\236SOUND",
    "\243SPC(",
    "\271SQR",
    "\247STEP",
    "\217STOP",
    "\303STR$",
    "\240TAB(",
    "\277TAN",
    "\245THEN",
    "\241TO",
    "\265USR",
    "\304VAL",
    "\335WRITE", /* QuickDisk DOS */
    "\317XOR",
    "\254^"
};

#define NTOK (sizeof(tokens)/sizeof(tokens[0]))

/* Tokenize text into buf; buf should be at least as long as text */
void tokenize(const char *text, char *buf)
{
    while(*text) {
	int l = 0, h = NTOK - 1, m, o = 0;
	char u = toupper(text[o]);
	while(l <= h) {
	    m = (l + h) / 2;
	    if(tokens[m][o+1] == u) {
		o++;
		if(!tokens[m][o+1])
		    break;
		if(!text[o]) {
		    l = h + 1;
		    break;
		}
		u = toupper(text[o]);
	    } else {
		if(u < tokens[m][o+1])
		    h = m - 1;
		else
		    l = m + 1;
		if(o) {
		    o = 0;
		    u = toupper(text[o]);
		}
	    }
	}
	if(l <= h) {
	    *buf++ = tokens[m][0];
	    text += o;
	    if(tokens[m][0] == (char)0216) { /* REM */
		strcpy(buf, text);
		return;
	    }
	} else {
	    *buf++ = *text++;
	    if(buf[-1] == '"') {
		while(*text && *text != '"')
		    *buf++ = *text++;
		*buf++ = *text;
		if(!*text)
		    return;
		text++;
	    }
	}
    }
    *buf++ = 0;
}

const char *expand_tok(char tok)
{
    for(int i = 0; i < NTOK; i++)
	if(tokens[i][0] == tok)
	    return tokens[i] + 1;
    return NULL;
}
