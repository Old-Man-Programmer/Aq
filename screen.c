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

#define SCREEN	0x3028
#define COLOR	0x3428

struct color {
  int r, g, b;
} aq_colors[16] = {
  {0, 0, 0},		// BLACK
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

static int _bordercolor, _cbgcolor = 0;
int _xorigin, _yorigin, _screen_w, _screen_h, _border, _scale = 5;
SDL_Surface *_screen;
uint8_t *_cdata;

void color(uint8_t data, uint32_t *fg, uint32_t *bg)
{
  int f = data>>4, b = data&0xF;
  *fg = SDL_MapRGB(_screen->format, aq_colors[f].r, aq_colors[f].g, aq_colors[f].b);
  *bg = SDL_MapRGB(_screen->format, aq_colors[b].r, aq_colors[b].g, aq_colors[b].b);
//   int fg = (data>>4);
//   int bg = (data&0x0F);
// 
//   return (_colors[fg]<<4)|_colors[bg];
}

/*
 * Set the pixel at (x, y) to the given value
 * NOTE: The surface must be locked before calling this!
 */
void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

void writechar(int col, int row, int ch, uint32_t fg, uint32_t bg)
{
  int x, y, cr, cbit;

  if (row < 0 || col < 0 || row > 23 || col > 39) {
    fprintf(stderr,"col,row out of range. (%d,%d)\n", col, row);
    abort();
  }
  /* Lock the screen for direct access to the pixels */
  if ( SDL_MUSTLOCK(_screen) ) {
    if ( SDL_LockSurface(_screen) < 0 ) {
      fprintf(stderr, "Can't lock screen: %s\n", SDL_GetError());
      return;
    }
  }

  uint8_t *cbyte = &_cdata[ch<<3];

  int xo = _xorigin + ((col*8)*_scale);
  int yo = _yorigin + ((row*8)*_scale);

  for(y=yo, cr = 0; cr < 8; (y+=_scale), cr++, cbyte++) {

    for(cbit = 128, x = xo; cbit; (x+=_scale), (cbit>>=1)) {
      uint32_t color = (*cbyte & cbit)? fg : bg;

      for(int sy=0; sy < _scale; sy++) {
	for(int sx=0; sx < _scale; sx++) {
	  putpixel(_screen, x+sx, y+sy, color);
	}
      }

    }
  }
  if ( SDL_MUSTLOCK(_screen) ) {
    SDL_UnlockSurface(_screen);
  }

  /* Update just the part of the display that we've changed */
  SDL_UpdateRect(_screen, xo, yo, 8*_scale, 8*_scale);
}

void fillrect(int x, int y, int width, int height, uint32_t bg)
{
  /* Lock the screen for direct access to the pixels */
  if ( SDL_MUSTLOCK(_screen) ) {
    if ( SDL_LockSurface(_screen) < 0 ) {
      fprintf(stderr, "Can't lock screen: %s\n", SDL_GetError());
      return;
    }
  }
  for(int sy=y; sy < y+height; sy++) {
    for(int sx=x; sx < x+width; sx++) {
      putpixel(_screen, sx, sy, bg);
    }
  }
  if ( SDL_MUSTLOCK(_screen) ) {
    SDL_UnlockSurface(_screen);
  }

  /* Update just the part of the display that we've changed */
  SDL_UpdateRect(_screen, x, y, width, height);
}

void screen_init(void)
{
  _screen_w = (40*8)*_scale;
  _screen_h = (24*8)*_scale;
  _border = 10*_scale;
  _xorigin = _yorigin = _border;

  _screen = SDL_SetVideoMode(_screen_w+(_border*2), _screen_h+(_border*2), 8, SDL_SWSURFACE);
  if (_screen == NULL ) {
    fprintf(stderr, "Couldn't set %dx%dx8 video mode: %s\n", _screen_w+(_border*2), _screen_h+(_border*2),
                    SDL_GetError());
    return;
  }
}

uint8_t screen_reader(_U uint16_t addr, uint8_t *cell)
{
  return *cell;
}

#define SCREEN	0x3028
#define COLOR	0x3428

uint8_t screen_writer(uint16_t addr, uint8_t *cell, uint8_t data)
{
  uint32_t fg, bg;
  int row, col;
  uint8_t page = addr>>8, old;
  int pos;

  old = *cell;
  if (old == data) return data;
  *cell = data;
  switch (addr) {
    case 0x3400:
      color(_bordercolor = data, &fg, &bg);
      fillrect(0, 0, _screen_w+(_border*2), _screen_h+(_border*2), bg);
      for(int r=0; r<24; r++) {
	for(int c=0; c<40; c++) {
	  pos = (r*40)+c;
	  color(load(COLOR+pos), &fg, &bg);
	  writechar(c,r,load(SCREEN+pos), fg, bg);
	}
      }
      break;
    case 0x3421:
      _cbgcolor = data;
      break;
  }
  if ((page >= 0x34) && (page < 0x38)) {
    pos = addr - COLOR;
    if (pos < 0) return data;
    row = pos/40;
    if (row < 0 || row > 23) return data;
    col = (pos%40);
    color(data, &fg, &bg);
    writechar(col, row, load(addr-0x0400), fg, bg);
  }
  if (page >= 0x30 && page < 0x34) {
    pos = addr - SCREEN;
    if (pos < 0) return data;
    row = pos/40;
    if (row < 0 || row > 23) return data;
    col = (pos%40);
    color(load(addr+0x0400), &fg, &bg);
    writechar(col, row, data, fg, bg);
  }
  return data;
}

