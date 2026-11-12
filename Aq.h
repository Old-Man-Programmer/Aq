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
#include "aqsnd.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>

#define ROMS	"./roms/"

#define CLKRATE	3579545 	/* clocks / second */
#define VSCLKS	55840		/* 15.6ms * CLKRATE */
#define VACTIVE	45780		/* Active scan period */

typedef enum { TIMER = 0, SCAN = 1 } keymode_t;
#define KEYDOWN		20000
#define KEYUP		22000
#define KEYSCANS	4
#define MAXKEYS		10


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
};

enum memflags {
  MEM_RAM = 0,
  MEM_USEROM = 1, MEM_USEREADER = 2, MEM_USEWRITER = 4, MEM_USEGAMEROM = 8
};

struct memory {
  enum memflags flags;
  uint8_t ram[256];
  uint8_t *rom;
  uint8_t (*reader)(uint16_t addr, uint8_t *cell);
  uint8_t (*writer)(uint16_t addr, uint8_t *cell, uint8_t data);
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
extern FILE *_log, *_ilog;
extern const char *_gamerom;
extern const char *_tape;
extern int *_keybuf, _nkeys;

/* z80.c */
void reset(void);
extern unsigned long _totclk;
extern struct regset _reg;
extern uint16_t _PC;

/* mem.c */
uint8_t load(uint16_t addr);
uint8_t store(uint16_t addr, uint8_t data);
int initmem(void);
extern struct memory *_page[256];
extern struct writelog _wlog[10];
extern int _widx;

/* screen.c */
void color(uint8_t data, uint32_t *fg, uint32_t *bg);
void putpixel(int x, int y, uint32_t pixel);
void writechar(int col, int row, int ch, uint32_t fg, uint32_t bg);
void fillrect(int x1, int y1, int x2, int y2, uint32_t bg);
void screen_update(void);
void screen_init(void);
uint8_t screen_reader(uint16_t addr, uint8_t *cell);
uint8_t screen_writer(uint16_t addr, uint8_t *cell, uint8_t data);
extern int _scale;
extern uint8_t *_cdata;

/* io.c */
int fixkeys(int key, int *shift, int *ctrl);
int findkey(int k);
void io_init(void);
uint8_t io_read(uint8_t port, uint8_t top);
void io_write(uint8_t port, uint8_t top, uint8_t data);
extern keymode_t _keymode;
extern int _keydown;
extern uint8_t _swlock_pattern;
extern int _key, _ctrl, _shift;

/* aqjoy.c */
uint8_t aqjoy_read(uint8_t port);

/* For suppressing unused warnings */
#ifdef __GNUC__
#define _U __attribute__((__unused__))
#else
#define _U
#endif
