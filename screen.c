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

/* Color codes:
  +---------+-----------+  +---------+------------+
  | Dec Hex |   Color   |  | Dec Hex |   Color    |
  |  0   0  |  BLACK    |  |  8   8  |  LT GRAY   |
  |  1   1  |  RED      |  |  9   9  |  CYAN      |
  |  2   2  |  GREEN    |  | 10   A  |  MAGENTA   |
  |  3   3  |  YELLOW   |  | 11   B  |  DK BLUE   |
  |  4   4  |  BLUE     |  | 12   C  |  LT YELLOW |
  |  5   5  |  VIOLET   |  | 13   D  |  LT GREEN  |
  |  6   6  |  CYAN     |  | 14   E  |  ORANGE    |
  |  7   7  |  WHITE    |  | 15   F  |  DK GRAY   |
  +---------+-----------+  +---------+------------+
*/

int _screen_needsupdate = 0;

#define SCREEN	0x3028
#define COLOR	0x3428

struct color {
  int r, g, b;
} aq_colors[16] = {
  {22, 22, 22},		// BLACK
  {216, 16, 46},	// RED
  {17, 196, 0},		// GREEN
  {208, 190, 22},	// YELLOW
  {10, 28, 196},	// BLUE
  {201, 22, 219},	// VIOLET
  {3, 203, 173},	// Lt. BLUE GREEN (CYAN)
  {254, 254, 254},	// WHITE
  {196, 196, 196},	// Lt. GREY
  {73, 173, 158},	// BLUE-GREEN (CYAN)
  {148, 58, 156},	// MAGENTA
  {26, 35, 121},	// DARK BLUE
  {193, 184, 97},	// Lt. YELLOW
  {71, 160, 62},	// Lt. GREEN
  {145, 45, 60},	// ORANGE
  {22, 22, 22},		// Dark GRAY
};

int _colors[] = {
  0, 9, 10, 11, 12, 13, 14, 15,
  7, 6,  5,  4, 11, 10,  3,  8
};

static int _bordercolor, _cbgcolor = 0, _rw, _rh;

WINDOW *_rootwin, *_cwin;


static int color(uint8_t data)
{
  return data?data:256;

//   int fg = (data>>4);
//   int bg = (data&0x0F);
// 
//   return (_colors[fg]<<4)|_colors[bg];
}


void screen_init(void)
{
  int i, j;
  _rootwin = initscr();
  start_color();
  if (can_change_color()) {
    for(i=0; i<16; i++)
      init_color(i, aq_colors[i].r*4, aq_colors[i].g*4, aq_colors[i].b*4);
    for(i=0;i<16;i++)
      for(j=0;j<16;j++)
	init_pair((i<<4)|j,i,j);
  } else {
    for(i=0;i<16;i++)
      for(j=0;j<16;j++)
	init_pair((i<<4)|j,_colors[i],_colors[j]);
  }
  init_pair(256, 0, 0);

  cbreak();
  noecho();
  raw();
  curs_set(0);
  getmaxyx(_rootwin, _rh, _rw);
  if (_rh < 24 || _rw < 40) {
    fprintf(stderr,"Screen size is too small, must be at least 40 columns x 25 rows\n");
    quit();
  }
  clear();
  bkgd('.');
  refresh();
  _cwin = newwin(24, 40, (_rh-24)/2, (_rw-40)/2);
  wclear(_cwin);
  wrefresh(_cwin);
}

uint8_t screen_reader(_U uint16_t addr, uint8_t *cell)
{
  return *cell;
}

#define SCREEN	0x3028
#define COLOR	0x3428

uint8_t screen_writer(uint16_t addr, uint8_t *cell, uint8_t data)
{
  cchar_t ch;
  int row, col;
  uint8_t page = addr>>8, old;
  int pos;

  old = *cell;
  if (old == data) return data;
  *cell = data;
  switch (addr) {
    case 0x3400:
      _bordercolor = data;
      clear();
      bkgd(' ' | COLOR_PAIR(color(data)));
      refresh();
      break;
    case 0x3421:
      _cbgcolor = data;
      break;
  }
  if ((page >= 0x34) && (page < 0x38)) {
    pos = addr - COLOR;
    row = pos/40;
    if (row < 0 || row > 23) return data;
    col = (pos%40);
//    ch.attr = COLOR_PAIR(color(data));
    ch.attr = 0;
    ch.chars[0] = load(addr-0x0400); // + 0xE000;
    if (ch.chars[0] < 32 || ch.chars[0] > 126) ch.chars[0] += 0xE000;
    ch.chars[1] = 0;
    ch.chars[2] = 0;
    ch.ext_color = color(data);
    mvwadd_wch(_cwin, row, col, &ch);
    wrefresh(_cwin);
  }
  if (page >= 0x30 && page < 0x34) {
    pos = addr - SCREEN;
    row = pos/40;
    if (row < 0 || row > 23) return data;
    col = (pos%40);
//    ch.attr = COLOR_PAIR(color(load(addr+0x0400)));
    ch.attr = 0;
    ch.chars[0] = (data); // +0xE000;
    if (ch.chars[0] < 32 || ch.chars[0] > 126) ch.chars[0] += 0xE000;
    ch.chars[1] = 0;
    ch.chars[2] = 0;
    ch.ext_color = color(load(addr+0x0400));
    mvwadd_wch(_cwin, row, col, &ch);
    wrefresh(_cwin);
  }
  return data;
}

