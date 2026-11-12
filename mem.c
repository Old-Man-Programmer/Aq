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

struct writelog _wlog[10];
int _widx = 0;

uint16_t _gameromstart = 0xFFFF;

uint8_t _mem[64*K];
uint8_t (*mem_reader[64*K])(uint16_t addr);
uint8_t (*mem_writer[64*K])(uint16_t addr, uint8_t data);

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

inline uint8_t load(uint16_t addr)
{
  return _mem[addr];
}

inline uint8_t store(uint16_t addr, uint8_t data)
{
  return mem_writer[addr](addr, data);
}


uint8_t ram_writer(uint16_t addr, uint8_t data)
{
  return (_mem[addr] = data);
}

uint8_t rom_writer(uint16_t addr, uint8_t data)
{
  return _mem[addr];
}

// Too many magic numbers.  Should make #defines for them all.
int initmem()
{
  uint8_t *basic, *game;
  int i;
  off_t size;

  memset(_mem, 0, sizeof(uint8_t)*64*K);
  for(i=0; i < 64*K; i++) {
    mem_writer[i] = ram_writer;
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
  memcpy(_mem+0x0000, basic, size);
//   for(i=0; i < size; i++) {
//     mem_writer[i+0x0000] = rom_writer;
//   }
  if (_extendedbasic) {
    basic = loadrom(ROMS "ebasic.bin", &size);
    if (basic == NULL) {
      fprintf(stderr,"Unable to open ROM file %s [%s]\n", ROMS "ebasic.bin", strerror(errno));
      exit(1);
    }
    memcpy(_mem+0x2000, basic, size);
    for(int i=0; i < size; i++) {
      mem_writer[i+0x2000] = rom_writer;
    }
  }

  if (_gamerom) {
    if (! (game = loadrom(_gamerom, &size))) {
      char path[PATH_MAX];
      sprintf(path, "%s/%s", ROMS, _gamerom);
      game = loadrom(path, &size);
    }
    if (game) {
      _gameromstart = 0xFFFF-(size-1);
      memcpy(_mem+_gameromstart, game, size);
      for(i=0; i < size; i++) {
	mem_writer[i+_gameromstart] = rom_writer;
      }
    } else {
      fprintf(stderr,"Unable to open game ROM [%s]\n", strerror(errno));
      exit(1);
    }
  }

  /* Map I/O devices */
  /* Screen */
  if (!_batchmode) {
    mem_writer[0x3400] = screen_border;
    for(i=0; i < (40*24); i++) {
      mem_writer[0x3028+i] = screen_charscreen;
      mem_writer[0x3428+i] = screen_colorscreen;
    }
    screen_init();
  }

  return 0;
}
