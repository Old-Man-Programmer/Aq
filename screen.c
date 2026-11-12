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
#include <GL/gl.h>

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

uint32_t _colors[] = {
  0xFF000000, 0xFFFF3E41, 0xFF40FE41, 0xFFFEFE42,
  0xFF3F40FC, 0xFFFE40FD, 0xFF3DFFFD, 0xFFFFFFFF,

  0xFFBFBFBF, 0xFF81E2E1, 0xFFC667C6, 0xFF4A4AAB,
  0xFFF4F493, 0xFF78D778, 0xFFBD5B5D, 0xFF000000
};

uint8_t _bordercolor;
int _xorigin, _yorigin, _screen_w, _screen_h, _border, _scale = 5;
static int _texw, _texh;
#ifndef USE_QT
SDL_Window *_window;
SDL_GLContext _gl_context;
#endif
GLuint _texture, _vlist;
uint32_t *_screen;
uint8_t *_cdata;

void color(uint8_t data, uint32_t *fg, uint32_t *bg)
{
  int f = data>>4, b = data&0xF;
  *fg = _colors[f];
  *bg = _colors[b];
}

inline void putpixel(int x, int y, uint32_t pixel)
{
//   if (x < 0 || y < 0 || x > _texw || y > _texh) {
//     printf("x,y out of bounds (%d,%d)\n", x, y);
//     abort();
//   }
  _screen[(y*_texw)+x] = pixel;
}

void writechar(int col, int row, int ch, uint32_t fg, uint32_t bg)
{
  int x, y, cbit;

  if (row < 0 || col < 0 || row > 23 || col > 39) {
    fprintf(stderr,"col,row out of range. (%d,%d)\n", col, row);
    abort();
  }

  uint8_t *cbyte = &_cdata[ch<<3];

  int xoff = _border + col*8, yoff= _border + row*8;

  for(y=0; y < 8; y++, cbyte++) {
    for(cbit=128, x=0; x < 8; x++, (cbit>>=1)) {
      putpixel(x+xoff, y+yoff, (*cbyte & cbit)? fg : bg);
    }
  }
}

void fillrect(int x, int y, int width, int height, uint32_t bg)
{
  for(int sy=0; sy < height; sy++) {
    for(int sx = 0; sx < width; sx++) {
      putpixel(x+sx, y+sy, bg);
    }
  }
}

void gl_render(int w, int h)
{
#ifndef USE_IMGUI
    uint32_t fg, bg, dw = w, dh = h;
    color(_bordercolor, &fg, &bg);
    glViewport(0, 0, w, h);
#define col_b2f(c) (float)((c)&0xff)/0xff
    glClearColor(col_b2f(bg>>16), col_b2f(bg>>8), col_b2f(bg), 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    if(dw > dh * 4 / 3)
      dw = dh * 4 / 3;
    else if(dh > dw * 3 / 4)
      dh = dw * 3 / 4;
    glViewport((w - dw) / 2, (h - dh) / 2, dw, dh);
#endif
    // Since IMGUI preserves bound texture, it's not necessary to
    // rebind every time
#ifdef USE_QT
    glBindTexture(GL_TEXTURE_2D, _texture);
#endif
    // glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE); // if big-endian
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _texw, _texh, 0,
		 GL_BGRA, GL_UNSIGNED_BYTE, _screen);
#ifndef USE_IMGUI
    glCallList(_vlist);
    // Unbind texture; not sure why I'd want to
    // glBindTexture(GL_TEXTURE_2D, 0);
#else
    imgui_update(w, h, _texture);
#endif
}

#ifndef USE_QT
static volatile bool _continue_updating = true;

static void *screen_update(_U void *data)
{
  SDL_GL_MakeCurrent(_window, _gl_context);
  while(_continue_updating) {
    int w, h;
    SDL_GL_GetDrawableSize(_window, &w, &h);
    gl_render(w, h);
    glFinish(); // Is this necessary?  Done by SwapWindow()?
    SDL_GL_SwapWindow(_window);
    pthread_testcancel();
  }
  // not reached, but w/o optimization, gcc still complains
  return NULL;
}

static pthread_t _update_thread;
#endif

void screen_init()
{
  if(_screen)
    return;
  _screen_w = (40*8);
  _screen_h = (24*8);
  _border = 10;
  _xorigin = _yorigin = _border;
  _texw = _screen_w+(_border*2);
  _texh = _screen_h+(_border*2);
  _screen = malloc((_texw*_texh)*sizeof(uint32_t));
  memset(_screen, 128, (_texw*_texh)*sizeof(uint32_t));

#ifdef USE_QT
  qt_init(_texw*_scale, _texh*_scale);
#else
#ifdef USE_IMGUI
  imgui_glinit();
#endif
  _window = SDL_CreateWindow("Aquarius", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             _texw*_scale, _texh*_scale,
			    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (_window == NULL) {
    fprintf(stderr, "Couldn't get a window [%s]\n", SDL_GetError());
    exit(1);
  }
  if(_fullscreen)
    set_fullscreen();
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  _gl_context = SDL_GL_CreateContext(_window);
  if (_gl_context == NULL) {
    fprintf(stderr, "Couldn't get a GL context [%s]\n", SDL_GetError());
    exit(1);
  }
  SDL_GL_MakeCurrent(_window, _gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync
#ifdef USE_IMGUI
  imgui_init(_scale);
#endif
  gl_init();

  SDL_GL_MakeCurrent(_window, NULL);

  if(pthread_create(&_update_thread, NULL, screen_update, NULL)) {
    perror("screen thread");
    exit(1);
  }
#endif
}

void gl_init()
{
  glDisable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glGenTextures(1, &_texture);
  if (!_texture) {
    // FIXME: get GL error
    fprintf(stderr, "Couldn't get a texture [%s]\n", SDL_GetError());
    exit(1);
  }
  glBindTexture(GL_TEXTURE_2D, _texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // make the scaled rendering look less blurry.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

#ifndef USE_IMGUI
  _vlist = glGenLists(1);
  glNewList(_vlist, GL_COMPILE);
  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0.0, 0.0);
  glVertex3i(0, 0, 0);
  glTexCoord2f(1.0, 0.0);
  glVertex3i(1.0, 0, 0);
  glTexCoord2f(0.0, 1.0);
  glVertex3i(0, 1.0, 0);
  glTexCoord2f(1.0, 1.0);
  glVertex3i(1.0, 1.0, 0);
  glEnd();
  glEndList();
  glFinish();
#endif
}

void screen_shutdown()
{
#ifndef USE_QT
  // A generic cancel might cause locking problems in SDL
  // pthread_cancel(_update_thread);
  // so use a loop terminator flag
  _continue_updating = false;
  pthread_join(_update_thread, NULL);
  SDL_GL_DeleteContext(_gl_context);
  SDL_DestroyWindow(_window);
#endif
}

#define SCREEN	0x3028
#define COLOR	0x3428

uint8_t screen_border(uint16_t addr, uint8_t data)
{
  uint32_t fg, bg;

  if (_mem[addr] == data) return data;
  _mem[addr] = data;

  color(_bordercolor = data, &fg, &bg);
  fillrect(0, 0, _screen_w+(_border*2), _screen_h+(_border*2), bg);
  for(int r=0; r<24; r++) {
    for(int c=0; c<40; c++) {
      int pos = (r*40)+c;
      color(load(COLOR+pos), &fg, &bg);
      writechar(c,r,load(SCREEN+pos), fg, bg);
    }
  }
  return data;
}

uint8_t screen_charscreen(uint16_t addr, uint8_t data)
{
  uint32_t fg, bg;

  if (_mem[addr] == data) return data;
  _mem[addr] = data;
  int pos = addr - SCREEN;
  int row = pos/40, col = (pos%40);
  color(load(addr+0x0400), &fg, &bg);
  writechar(col, row, data, fg, bg);
  return data;
}

uint8_t screen_colorscreen(uint16_t addr, uint8_t data)
{
  uint32_t fg, bg;

  if (_mem[addr] == data) return data;
  _mem[addr] = data;
  int pos = addr - COLOR;
  int row = pos/40, col = (pos%40);
  color(data, &fg, &bg);
  writechar(col, row, load(addr-0x0400), fg, bg);
  return data;
}
