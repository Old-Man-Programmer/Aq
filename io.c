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

struct ports port[256];

keymode_t _keymode = TIMER;
int _keydown=0;

#define CTRL	1
#define SHIFT	2

/*
COLSCAN:
    09 0A  0B 0C 0D 0E 
 1 | ; RTN BS  =  .  :  bit0  254  0xFE
 2 | l  p   /  -  ,  0  bit1  253  0xFD
 3 | n  m   o  9  j  k  bit2  251  0xFB
 4 | h  u   i  8  b  7  bit3  247  0xF7
 5 | c  v   y  6  f  g  bit4  239  0xEF
 6 | d  r   t  5  x  4  bit5  223  0xDF
 7 |SP  z   e  3  a  s  bit6  191  0xBF
 8 |SH  q   w  2 CTL 1  bit7  127  0x7F

ROW/COL:
     80 40 20 10 08 04 02 01
01 |  2  3  5  6  8  9  -  =  bit0  254  0xFE
02 |  w  e  t  y  i  o  / BS  bit1  253  0xFD
04 |  1  s  4  g  7  k  0  :  bit2  251  0xFB
08 |  q  z  r  v  u  m  p RTN bit3  247  0xF7
10 | SH SP  d  c  h  n  l  ;  bit4  239  0xEF
20 | CTL a  x  f  b  j  ,  .  bit5  223  0xDF
*/

const char keyrows[6][8] = {
  {'2', '3', '5', '6', '8', '9', '-', '='},
  {'w', 'e', 't', 'y', 'i', 'o', '/', '\b'},
  {'1', 's', '4', 'g', '7', 'k', '0', ':'},
  {'2', 'z', 'r', 'v', 'u', 'm', 'p', '\n'},
  {'x', ' ', 'd', 'c', 'h', 'n', 'l', ';'},
  {'x', 'a', 'x', 'f', 'b', 'j', ',', '.'}
};

#if 0
// An int for every key, coutning number of down events
// Or that was the idea.  It's too hard to track for now.
static unsigned keypressed[8][6] = {};
#endif
// A bit for every key, for faster access to keypressed[]
static uint64_t keymatrix = 0ULL;
// A bit for every key, with ghosting
// In other words, for every key with another key in the same column (c) and
// same row (r), the key at (c, r) is set as well.
static volatile uint64_t ghostkeys = 0ULL;

static void update_ghost(void)
{
#if 0
  uint64_t gk = keymatrix, n = gk;
  int k = -1; // last one done
  for(int i = 0; i < 8; i++, n >>= 8) {
    if(i == k)
      continue;
    uint64_t m = gk, on = n;
    for(int j = 0; j < 8; j++, m >>= 8) {
      uint8_t mm = m & 0xff;
      if(i != j && (n & mm) && (n & mm) != mm)
	n |= mm;
    }
    if(on != n) {
      gk |= n << (8 * (k = i));
      i = -1;
    }
  }
  ghostkeys = gk;
#else
  ghostkeys = keymatrix;
#endif
}

const struct keyscan _keys[] = {
  {0,0x00,0x00,0},      {0,0x20,0x80,0},        {0,0x10,0x80,0},        {0,0x00,0x00,0},
  {'\001',0x20,0x40,1}, {'\002',0x20,0x08,1},   {'\003',0x10,0x10,1},   {'\004',0x10,0x20,1},
  {'\005',0x02,0x40,1}, {'\006',0x20,0x10,1},   {'\007',0x04,0x10,1},   {'\b',0x02,0x01,0},
  {'\011',0x02,0x08,1}, {'\n',0x08,0x01,0},     {'\013',0x04,0x04,1},   {'\014',0x10,0x02,1},
  {'\015',0x08,0x01,0}, {'\016',0x10,0x04,1},   {'\017',0x02,0x04,1},   {'\020',0x08,0x02,1},
  {'\021',0x08,0x80,1}, {'\022',0x08,0x20,1},   {'\023',0x04,0x40,1},   {'\024',0x02,0x20,1},
  {'\025',0x08,0x08,1}, {'\026',0x08,0x10,1},   {'\027',0x02,0x80,1},   {'\030',0x20,0x20,1},
  {'\031',0x02,0x10,1}, {'\032',0x08,0x40,1},   {'\033',0x01,0x08,1},   {'\034',0x02,0x01,1},
  {'\035',0x01,0x04,1}, {'\036',0x02,0x02,1},   {'\037',0x01,0x02,1},   {' ',0x10,0x40,0},
  {'!',0x04,0x80,2},    {'"',0x01,0x80,2},      {'#',0x01,0x40,2},      {'$',0x04,0x20,2},
  {'%',0x01,0x20,2},    {'&',0x01,0x10,2},      {'\'',0x04,0x08,2},     {'(',0x01,0x08,2},
  {')',0x01,0x04,2},    {'*',0x04,0x01,2},      {'+',0x01,0x01,2},      {',',0x20,0x02,0},
  {'-',0x01,0x02,0},    {'.',0x20,0x01,0},      {'/',0x02,0x02,0},      {'0',0x04,0x02,0},
  {'1',0x04,0x80,0},    {'2',0x01,0x80,0},      {'3',0x01,0x40,0},      {'4',0x04,0x20,0},
  {'5',0x01,0x20,0},    {'6',0x01,0x10,0},      {'7',0x04,0x08,0},      {'8',0x01,0x08,0},
  {'9',0x01,0x04,0},    {':',0x04,0x01,0},      {';',0x10,0x01,0},      {'<',0x20,0x02,2},
  {'=',0x01,0x01,0},    {'>',0x20,0x01,2},      {'?',0x04,0x02,2},      {'@',0x10,0x01,2},
  {'A',0x20,0x40,2},    {'B',0x20,0x08,2},      {'C',0x10,0x10,2},      {'D',0x10,0x20,2},
  {'E',0x02,0x40,2},    {'F',0x20,0x10,2},      {'G',0x04,0x10,2},      {'H',0x10,0x08,2},
  {'I',0x02,0x08,2},    {'J',0x20,0x04,2},      {'K',0x04,0x04,2},      {'L',0x10,0x02,2},
  {'M',0x08,0x04,2},    {'N',0x10,0x04,2},      {'O',0x02,0x04,2},      {'P',0x08,0x02,2},
  {'Q',0x08,0x80,2},    {'R',0x08,0x20,2},      {'S',0x04,0x40,2},      {'T',0x02,0x20,2},
  {'U',0x08,0x08,2},    {'V',0x08,0x10,2},      {'W',0x02,0x80,2},      {'X',0x20,0x20,2},
  {'Y',0x02,0x10,2},    {'Z',0x08,0x40,2},      {'[',0x01,0x08,0},      {'\\',0x02,0x01,2},
  {']',0x01,0x04,2},    {'^',0x02,0x02,2},      {'_',0x01,0x02,2},      {'`',0x04,0x08,2},
  {'a',0x20,0x40,0},    {'b',0x20,0x08,0},      {'c',0x10,0x10,0},      {'d',0x10,0x20,0},
  {'e',0x02,0x40,0},    {'f',0x20,0x10,0},      {'g',0x04,0x10,0},      {'h',0x10,0x08,0},
  {'i',0x02,0x08,0},    {'j',0x20,0x04,0},      {'k',0x04,0x04,0},      {'l',0x10,0x02,0},
  {'m',0x08,0x04,0},    {'n',0x10,0x04,0},      {'o',0x02,0x04,0},      {'p',0x08,0x02,0},
  {'q',0x08,0x80,0},    {'r',0x08,0x20,0},      {'s',0x04,0x40,0},      {'t',0x02,0x20,0},
  {'u',0x08,0x08,0},    {'v',0x08,0x10,0},      {'w',0x02,0x80,0},      {'x',0x20,0x20,0},
  {'y',0x02,0x10,0},    {'z',0x08,0x40,0},      {'{',0x01,0x08,2},      {'|',0x00,0x00,0},
  {'}',0x01,0x04,2},    {'~',0x00,0x00,0},      {'\127',0x0B,0x01,0},
  {SDLK_BACKSPACE,0x02,0x01,0},
#if 0
  {SDLK_LSHIFT,0,0,2}, {SDLK_RSHIFT,0,0,2},
  {SDLK_LCONTROL,0,0,1}, {SDLK_RCONTROL,0,0,1},
#else
  {256,0,0,2}, {257,0,0,1},
#endif
  {-1,0,0,0}
};

void dokey(int keyno, Uint16 mod, bool press)
{
  // I have no idea what to do with capslock.
  bool ctrl = !!(mod & KMOD_CTRL),
       shift = !!(mod & KMOD_SHIFT),
       capslk = !!(mod & KMOD_CAPS);
  keyno = findkey(fixkeys(keyno, &shift, &ctrl));
  if (capslk && (keyno >= 'a' + 3 && keyno <= 'z' + 3))
      shift = !shift;
  ctrl |= _keys[keyno].modkey == 1;
  shift |= _keys[keyno].modkey == 2;
  // Aquarius has real trouble with more than one key pressed at once.
  keymatrix = 0;

#define KM(c, r) ((uint64_t)(r) << ((c) * 8))

  if(shift /* && press */)
    keymatrix |= KM(7, 0x10);
  else
    keymatrix &= ~(KM(7, 0x10));
  if(ctrl /* && press */)
    keymatrix |= KM(7, 0x20);
  else
    keymatrix &= ~(KM(7, 0x20));
  for(int i = 0, cm = 1; i < 8; i++, cm <<= 1) {
#if 0
    if(cm == 0x80) {
      // FIXME: key's down modifier should be used instead of its
      // up modifier when released
      // For now, just set mask (above) w/o regard to count
      if(shift)
	keypressed[i][4] += press ? 1 : -1;
      if(ctrl)
	keypressed[i][5] += press ? 1 : -1;
    }
#endif
    if(_keys[keyno].col & cm) {
#if 0
      // FIXME: supporting multiple sources is too hard to track.
      // For example, from an analog event, there is a press every time the
      // analog value changes (very often), but only one release.  There may
      // also be repeated up events for key repeat.  Once these are dealt
      // with, maybe I'll look into this again.
      for(int j = 0, rm = 1; j < 6; j++, rm <<= 1)
	if(_keys[keyno].row & rm) {
	  keypressed[i][j] += press ? 1 : -1;
	  if(keypressed[i][j])
	    keymatrix |= KM(i, rm);
	  else
	    keymatrix &= ~KM(i, rm);
	  break;
	}
#else
      if(press)
	keymatrix |= KM(i, _keys[keyno].row);
      else
	keymatrix &= ~KM(i, _keys[keyno].row);
      break;
#endif
    }
  }
  update_ghost();
}

int fixkeys(int key, bool *shift, bool *ctrl)
{
//  static const char atable[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
  static const char mtable[]   = "       \"    <_>?)!@#$%^&*( : + ?@                          {|}  ~";
  if (*ctrl) return key;
  if (*shift) {
    if (key >= 32 && key <= 96 && mtable[key-32] != ' ') key = mtable[key-32];
    if (key == ':') *shift = false;
  } else {
    if (key == '[' || key == ']' || key == '\'') *shift = true;
  }
  return key;
}

int findkey(int k)
{
  int i;
  if (k> 0 && k < 128) return k+3;
  for(i=131;_keys[i].ch != -1; i++) {
    if (_keys[i].ch == k) return i;
  }
  return 0;
}

static void drain(void)
{
  int i;
  if (_keybuf[0] != 0) {
    _keybuf[0] = 0;
    _keydown = _keymode==TIMER? KEYUP : KEYSCANS;
    return;
  }
  for(i=0; i<_nkeys-1; i++) {
    _keybuf[i] = _keybuf[i+1];
  }
  _nkeys--;
  if (_nkeys) _keydown = _keymode==TIMER? (KEYDOWN + (_keybuf[0]==6? KEYDOWN*3 : 0)) : KEYSCANS;
  else _turbo &= ~TURBO_BASIC;
}

static uint8_t kb_read(uint8_t column)
{
  const struct keyscan *k;
  uint8_t d = 0;

  column = ~column;
  if (_nkeys) {
    k = &_keys[_keybuf[0]];
    if (k->col & column)
      d |= k->row;
    if (k->modkey && (_keys[k->modkey].col & column)) d |= _keys[k->modkey].row;
    if (_keymode == SCAN) {
      if(column == 0xff) _keydown--;
    }
    if (_keydown <= 0) {
      drain();
      d = 0;
    }
  } else {
    uint64_t gk = ghostkeys;
    for(int col = 0, mask = 1; col < 8; col++, mask <<= 1, gk >>= 8)
      if(column & mask)
	d |= gk & 0xff;
  }
  return ~d;
}

static void snd_write(_U uint8_t top, uint8_t data)
{
  aqsnd_write_fc(data);
}

static uint8_t tape_read(_U uint8_t top)
{
  return aqsnd_read_fc();
}

static void snd_write_addr(_U uint8_t top, uint8_t data)
{
  aqsnd_write_f7(data);
}

static void snd_write_data(_U uint8_t top, uint8_t data)
{
  aqsnd_write_f6(data);
}

static uint8_t snd_read_addr(_U uint8_t top)
{
  return aqsnd_read_f7();
}

static uint8_t snd_read_data(_U uint8_t top)
{
  uint8_t port = aqsnd_read_f7();
  if(port >= 14)
    return aqjoy_read(port);
  else
    return aqsnd_read_f6();
}

uint8_t _swlock_pattern = 0;

static void sw_lock(_U uint8_t top, uint8_t data)
{
  uint8_t old = _swlock_pattern;
//  printf("Scamble code = %d\n", data);
  _swlock_pattern = data;
  if (_gameromstart != 0xFFFF) {
    for(int i=_gameromstart; i <= 0xFFFF; i++) _mem[i] ^= old ^ _swlock_pattern;
  }
}

static uint8_t vs_read(_U uint8_t top)
{
  return (_totclk%VSCLKS)<VACTIVE? 1 : 0;
}

uint8_t io_read(uint8_t p, uint8_t top)
{
  uint8_t ret = port[p].reader ? port[p].reader(top) : 0xff;
  if (_ilog) {
    fprintf(_ilog, "%04X/%8lu io  read: port: %0X, top: %d, data: %02x\n", _PC, _totclk, p, top, (int)ret);
    fflush(_ilog);
  }
  return ret;
}

void io_write(uint8_t p, uint8_t top, uint8_t data)
{
  if (_ilog) {
    fprintf(_ilog, "%04X/%8lu io write: port: %0X, top: %d, data: %d\n", _PC, _totclk, p, top, data);
    fflush(_ilog);
  }
  if (port[p].writer) port[p].writer(top, data);
}

void io_init(void)
{
  memset(port, 0, sizeof(port));

  for(int i=0; i <= 0xFF; i++) {
    port[i].reader = NULL;
    port[i].writer = NULL;
  }

  port[0XFC].reader = tape_read;
  port[0xFC].writer = snd_write;

  port[0xF6].writer = snd_write_data;
  port[0xF6].reader = snd_read_data;
  port[0xF7].writer = snd_write_addr;
  port[0xF7].reader = snd_read_addr;

  port[0xFD].reader = vs_read;

  port[0xFF].reader = kb_read;
  port[0xFF].writer = sw_lock;

  aqsnd_reset();
  start_sdl_events();
}
