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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include "json.h"
#include "xdg.h"
#include "aqsnd.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <time.h>
#include <pthread.h>

#include "config.h"

#define K	1024
#define CLKRATE	3579545 	/* clocks / second */
#define VSCLKS	55840		/* 15.6ms * CLKRATE */
#define VACTIVE	45780		/* Active scan period */

typedef enum { TIMER = 0, SCAN = 1 } keymode_t;
#define KEYDOWN		20000
#define KEYUP		22000
#define KEYSCANS	71 // BASIC scans 70 times before ignoring

struct regset {
  union {
    struct {
      uint8_t F, A;	// Accumulator / Flags
    };
    uint16_t AF;
  };
  // General purpose registers:
  union {
    struct {
      uint8_t C, B;
    };
    uint16_t BC;
  };
  union {
    struct {
      uint8_t E, D;
    };
    uint16_t DE;
  };
  union {
    struct {
      uint8_t L, H;
    };
    uint16_t HL;
  };
  union {
    struct {
      uint8_t IXl, IXh;
    };
    uint16_t IX;
  };
  union {
    struct {
      uint8_t IYl, IYh;
    };
    uint16_t IY;
  };
  uint16_t SP;
};

struct writelog {
  uint16_t addr;
  uint8_t data;
};

struct keyscan {
  int ch, row, col, modkey;
};

struct ports {
  uint8_t (*reader)(uint8_t top);
  void (*writer)(uint8_t rdata, uint8_t data);
};

/* main.c */
void quit(void);
void load_basic(const char *basicfile);
int open_file(const char *f, const char *dir);
const char *file_from(const char *f, const char *subdir, char **buf);
int mkpath(const char *p);
extern FILE *_log, *_ilog;
extern const char *_gamerom, *_charrom, *_mainrom, *_ebrom;
extern const char *_tape;
extern int *_keybuf, _nkeys;
extern bool _extendedbasic, _batchmode;
extern int _argc;
extern char **_argv;

/* z80.c */
void reset(void);
extern unsigned long _totclk;
extern unsigned int _turbo;
extern volatile bool _doreset;
/* reason flag for turbo: */
#define TURBO_TAPE	1
#define TURBO_BASIC	2
#define TURBO_KEY	4
#define TURBO_BATCH	8
extern struct regset _reg;
extern uint16_t _PC;

/* mem.c */
#define DEFAULT_MAINROM "radofin.bin"
#define DEFAULT_CHARROM "Aquarius-chars.rom"
#define DEFAULT_EBROM   "ebasic.bin"
extern uint8_t _mem[64*K];
uint8_t load(uint16_t addr);
uint8_t store(uint16_t addr, uint8_t data);
int initmem(void);
// extern struct memory *_page[256];
extern uint16_t _gameromstart;
extern struct writelog _wlog[10];
extern int _widx;

/* screen.c */
void color(uint8_t data, uint32_t *fg, uint32_t *bg);
void putpixel(int x, int y, uint32_t pixel);
void writechar(int col, int row, int ch, uint32_t fg, uint32_t bg);
void fillrect(int x1, int y1, int x2, int y2, uint32_t bg);
void screen_init(void);
void gl_init();
void gl_render(int w, int h);
void screen_shutdown(void);
uint8_t screen_border(uint16_t addr, uint8_t data);
uint8_t screen_charscreen(uint16_t addr, uint8_t data);
uint8_t screen_colorscreen(uint16_t addr, uint8_t data);
extern SDL_Window *_window;
extern SDL_GLContext _gl_context;
extern int _scale;
extern uint8_t *_cdata;
extern uint8_t _bordercolor;

/* io.c */
void dokey(int keyno, Uint16 mod, bool press);
int fixkeys(int key, bool *shift, bool *ctrl);
int findkey(int k);
void io_init(void);
uint8_t io_read(uint8_t port, uint8_t top);
void io_write(uint8_t port, uint8_t top, uint8_t data);
extern keymode_t _keymode;
extern int _keydown;
extern uint8_t _swlock_pattern;
extern int _key, _ctrl, _shift, _capslk;

/* aqjoy.c */
void aqjoy_init(void); // call once, @ startup
void aqjoy_reset(void); // call every reset
void aqjoy_dial(int joyno, int dir, uint8_t val); // event
void aqjoy_but(int joyno, int but, bool press); // event
uint8_t aqjoy_read(uint8_t port); // read I/O port
extern bool _swap_joy; // config: swap A & B ports
// Action IDs
// joy2: 0x100
#define JC_JOY(n) ((n)==2 ? 0x100 :0)
// button: 0x400 + butlab-1 */
#define JC_BUT(j, n) (0x400 + ((n)-1) + JC_JOY(j))
// ax: 0x1000 * dir# + 0x800 (E == 0, N == 4)
#define JC_AX(j, v) -(0x800 + JC_JOY(j) + (v) * 0x1000)

/* config.c */
void add_default(const char *name, const value_t *val);
void add_default_str(const char *name, const char *val);
void add_default_num(const char *name, double val);
void add_default_type(const char *name, vdt_t t);
const value_t *config_val(const char *name);
const char *config_str(const char *name);
double config_num(const char *name);
bool config_bool(const char *name);
void read_config(void);
void set_config(const char *name, const value_t *val);
void set_config_str(const char *name, const char *val);
void set_config_num(const char *name, double val);
void set_config_bool(const char *name, bool val);
void write_config(void);
extern const char *_config_file;

#ifdef USE_QT
/* qtgui.cpp */
void qt_init(int w, int h);
#endif
#ifdef USE_IMGUI
/* dimgui.cpp */
void imgui_glinit(void);
void imgui_init(int scale);
bool imgui_event(SDL_Event *event);
void imgui_update(unsigned w, unsigned h, unsigned long texid);
#endif
/* *gui.cpp */
void win_title(const char *t);
void reqtape(void);
/* gui_common.cpp */
void init_gui_config(void);
typedef int32_t actid_t;
void start_sdl_events(void);
void stop_sdl_events(void);
void set_fullscreen(void);
extern bool _fullscreen;

/* sdlevent.cpp */
#include "sdlevent.h"

/* For suppressing unused warnings */
#ifdef __GNUC__
#define _U __attribute__((__unused__))
#else
#define _U
#endif
