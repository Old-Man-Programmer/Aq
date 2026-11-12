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

struct memory *_page[256], **_basic;

struct writelog _wlog[10];
int _widx = 0;

static uint8_t nomem_read(uint16_t addr, _U uint8_t *cell)
{
  return addr & 0xFF;
}

static uint8_t nomem_write(_U uint16_t addr, _U uint8_t *cell, _U uint8_t data)
{
  return 0;
}

uint8_t load(uint16_t addr)
{
  struct memory *p = _page[addr>>8];
  int off = addr&0x00FF;
  if (p->reader && (p->flags & MEM_USEREADER)) return p->reader(addr, &(p->ram[off]));
  if (p->rom && (p->flags & MEM_USEROM)) return p->rom[off];
  if (p->rom && (p->flags & MEM_USEGAMEROM)) return p->rom[off] ^ _swlock_pattern;
  return p->ram[off];
}

uint8_t store(uint16_t addr, uint8_t data)
{
  struct memory *p = _page[addr>>8];
  int off = addr&0x00FF;
  _wlog[_widx].addr = addr;
  _wlog[_widx++].data = data;
  if (p->writer && (p->flags & MEM_USEWRITER)) return p->writer(addr, &(p->ram[off]), data);
  p->ram[off] = data;
  return data;
}

static uint8_t *loadrom(const char *file, off_t *size)
{
  int fd = open(file, O_RDONLY);
  struct stat st;
  uint8_t *rom;
  int r;

  if (fd < 0) return NULL;

  fstat(fd, &st);
  rom = malloc(st.st_size);
  r = read(fd, rom, st.st_size);
  if (r < st.st_size) {
    fprintf(stderr,"Short read reading %s: %d != %ld\n", file, r, st.st_size);
    exit(1);
  }
  *size = st.st_size;
  return rom;
}

int initmem()
{
  uint8_t *basic, *game;
  int p, i;
  off_t size;

  for(p=0;p < 256; p++) {
    _page[p] = malloc(sizeof(struct memory));
    memset(_page[p], 0, sizeof(struct memory));
  }

  _cdata = loadrom(ROMS "Aquarius-chars.rom", &size);
  if (_cdata == NULL) {
    fprintf(stderr,"Unable to open ROM file %s [%s]\n", ROMS "Aquarius-chars.rom", strerror(errno));
    exit(1);
  }

  /* Map roms onto pages */
  basic = loadrom(ROMS "radofin.bin", &size);
  if (basic == NULL) {
    fprintf(stderr,"Unable to open ROM file %s [%s]\n", ROMS "radofin.bin", strerror(errno));
    exit(1);
  }
  for(i=0, p = 0x00; i < size; p++, i+= 256) {
    _page[p]->rom = basic+i;
    _page[p]->flags = MEM_USEROM;
  }

  if (_gamerom) {
    if (! (game = loadrom(_gamerom, &size))) {
      char path[PATH_MAX];
      sprintf(path, "%s/%s", ROMS, _gamerom);
      game = loadrom(path, &size);
    }
    if (game) {
      for(i=0, p = 0xFF-((size-1)/256); i < size; p++, i+= 256) {
	_page[p]->rom = game+i;
	_page[p]->flags = MEM_USEGAMEROM;
      }
    } else {
      fprintf(stderr,"Unable to open game ROM [%s]\n", strerror(errno));
      exit(1);
    }
  }

  /* Map I/O devices */
  /* Screen */
  for(p=0x30; p < 0x38; p++) {
    _page[p]->writer = screen_writer;
    _page[p]->flags = MEM_USEWRITER;
  }
  screen_init();

  return 0;
}
