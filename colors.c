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
#define _XOPEN_SOURCE   500
#define NCURSES_WIDECHAR	1

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <curses.h>
#include <locale.h>

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

void screen_init(void)
{
  cchar_t ch;
  int i, j;

  setlocale(LC_ALL, "en_US.utf-8");

  initscr();
  if (start_color() < 0) {
    fprintf(stderr,"Error starting colors.\n");
  }
  if (can_change_color()) {
    for(i=0; i<16; i++) {
      if (init_color(i, aq_colors[i].r, aq_colors[i].g, aq_colors[i].b) < 0)
	fprintf(stderr,"Unable to init color\n");
    }
    for(i=0;i<16;i++)
      for(j=0;j<16;j++)
	init_pair((i<<4)|j,i,j);
  } else {
    for(i=0;i<32;i++)
      for(j=0;j<32;j++)
	init_pair((i<<5)|j,i,j);
  }
  init_pair(1024, 0, 0);

  cbreak();
  noecho();
//   raw();
//  curs_set(0);
  clear();
  refresh();
}

int main(void)
{
  int ch;

  screen_init();

  for(int c=0;c<32;c++) {
    mvprintw(5,5+(c*4),"%2d ",c);
  }
  for(int r=0; r<32;r++) {
    mvprintw(6+r,2,"%2d:", r);
    for(int c=0; c<32; c++) {
      int color = (c<<5)|r;
      attr_set(0, color?color:1024, NULL);
      mvprintw(6+r, 5+(c*4), "###");
    }
    attrset(COLOR_PAIR(0));
  }
  refresh();
  while ((ch = getch()) != 'q');

  nocbreak();
  noraw();
  echo();
  endwin();
  printf("Done.\n");
  return 0;
}
