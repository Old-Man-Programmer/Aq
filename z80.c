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
#include "instr.h"

struct regset _reg, _alt;

unsigned long _totclk = 0;

unsigned int _turbo = 0;
float turbo_rate = 0.0;
volatile bool _doreset = false;

uint8_t _I, _R, _IFF1, _IFF2;	// Interrupt / Memory Refresh / Interrupt flip-flops 1 and 2
uint16_t _IX, _IY;	// Index registers
uint16_t _SP, _PC;	// Stack pointer / Program counter
int _imode;

uint8_t _IR;	// Instruction register (not z80)

enum {
  F_S = 128, F_Z = 64, F_Y = 32, F_H = 16, F_X = 8, F_PV = 4, F_N = 2, F_C = 1,
  F_SXY = 0xA8, F_XY=0x28,
  BIT_S=7, BIT_Z=6, BIT_Y=5, BIT_H=4, BIT_X=3, BIT_PV=2, BIT_N=1, BIT_C=0,
  F_SIGN = 128, F_ZERO = 64, F_HALFCARRY = 16, F_PARITY = 4, F_ADDSUB = 2, F_CARRY = 1
};

static uint8_t reg8(uint8_t v)
{
  switch(v) {
    case 0: return _reg.B;
    case 1: return _reg.C;
    case 2: return _reg.D;
    case 3: return _reg.E;
    case 4: return _reg.H;
    case 5: return _reg.L;
    case 6: return load(_reg.HL);
    case 7: return _reg.A;
  }
  return 0;
}

static uint8_t setreg8(uint8_t r, uint8_t v)
{
  switch(r) {
    case 0: return _reg.B = v;
    case 1: return _reg.C = v;
    case 2: return _reg.D = v;
    case 3: return _reg.E = v;
    case 4: return _reg.H = v;
    case 5: return _reg.L = v;
    case 6: return store(_reg.HL, v);
    case 7: return _reg.A = v;
  }
  return -1;
}

static uint8_t immediate_value(void)
{
  uint8_t v = load(_PC++);
//  if (_log) fprintf(_log, "#%d", v);
  return v;
}

static uint16_t immediate_addr(void)
{
  uint16_t addr = load(_PC) | (load(_PC+1)<<8);
  _PC += 2;
  return addr;
}

static int LDX(int dir)
{
  uint8_t n;
  store(_reg.DE, n = load(_reg.HL));
  n += _reg.A;
  _reg.DE+=dir;
  _reg.HL+=dir;
  _reg.BC--;

  _reg.F &= ~(F_H | F_N | F_PV | F_XY);
  _reg.F |= (_reg.BC == 0? 0 : F_PV) | (n&F_X) | (n&0x02?F_Y:0);

  return 16;
}


static int CPX(int dir)
{
  uint8_t hl = load(_reg.HL);
  uint16_t n = (int16_t)_reg.A - (int16_t)hl;
  uint8_t csub = _reg.A ^ hl;
  int borrow = n ^ csub;

  _reg.HL += dir;
  _reg.BC--;

  _reg.F = (n&F_S) | (n==0?F_Z:0) | (borrow&F_H) |
    (_reg.BC?F_PV:0) | F_N | (_reg.F&F_C);
  n -= _reg.F&F_H? 1 : 0;
  _reg.F |= (n&0x02?F_Y:0) | (n&F_X);

  return 16;
}

static void ADD(uint8_t V)
{
  uint16_t value = (int16_t)_reg.A + (int16_t)V;
  int overflow = ((_reg.A&0x80) == (V&0x80)) && ((value&0x80) != (_reg.A&0x80));
  uint8_t cadd = _reg.A ^ V;
  _reg.A = value;
  uint8_t carry = _reg.A ^ cadd;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | (carry&F_H) |
    (overflow?F_PV:0) | (value&0x100?F_C:0);
}

static uint16_t ADD16(uint16_t R, uint16_t V)
{
  uint32_t value = (int32_t)R + (int32_t)V;
  uint16_t cadd = R ^ V;
  uint16_t carry = value ^ cadd;
  _reg.F = (_reg.F & (F_S | F_Z | F_PV)) | ((value>>8)&F_XY) | (carry&0x1000?F_H:0) | (value>0xFFFF?F_C:0);
  return value;
}

static void ADC(uint8_t V)
{
  uint8_t c = (_reg.F&F_C? 1 : 0);
  uint16_t value = (int16_t)_reg.A + (int16_t)V + c;
  int overflow = ((_reg.A&0x80) == (V&0x80)) && ((value&0x80) != (_reg.A&0x80));
  uint8_t cadd = _reg.A ^ V ^ c;
  _reg.A = value;
  uint8_t carry = _reg.A ^ cadd;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | (carry&F_H) |
    (overflow?F_PV:0) | (value&0x100?F_C:0);
}

static uint16_t ADC16(uint16_t R, uint16_t V)
{
  uint16_t c = (_reg.F&F_C? 1 : 0);
  uint32_t value = (int32_t)R + (int32_t)V + c;
  int overflow = ((R&0x8000) == (V&0x8000)) && ((value&0x8000) != (R&0x8000));
  uint16_t cadd = R ^ V ^ c;
  uint16_t carry = value ^ cadd;

  _reg.F = ((value>>8)&F_SXY) | (value==0?F_Z:0) | (carry&0x1000?F_H:0) |
    (overflow?F_PV:0) | (value&0x10000?F_C:0);
  return value;
}

static void SUB(uint8_t V)
{
  int16_t value = (int16_t)_reg.A - (int16_t)V;
  int overflow = ((_reg.A&0x80) != (V&0x80)) && ((value&0x80) != (_reg.A&0x80));
  uint8_t csub = _reg.A ^ V;
  int b9 = (int)V > (int)_reg.A;
  _reg.A -= V;
  uint8_t borrow = _reg.A ^ csub;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | (borrow&F_H) |
    (overflow?F_PV:0) | F_N | (b9?F_C:0);
}

static void SBC(uint8_t V)
{
  int c = (_reg.F&F_C? 1 : 0);
  int16_t value = (int16_t)_reg.A - (int16_t)V - c;
  int overflow = ((_reg.A&0x80) != (V&0x80)) && ((value&0x80) != (_reg.A&0x80));
  uint8_t csub = _reg.A ^ V ^ c;
  int b9 = ((int)V+c) > (int)_reg.A;
  _reg.A = _reg.A - V - c;
  uint8_t borrow = _reg.A ^ csub;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | (borrow&F_H) |
    (overflow?F_PV:0) | F_N | (b9?F_C:0);
}

static uint16_t SBC16(uint16_t R, uint16_t V)
{
  uint16_t c = (_reg.F&F_C? 1 : 0);
  uint16_t value = R - V - c;
  int overflow = ((R&0x8000) != (V&0x8000)) && ((value&0x8000) != (R&0x8000));
  int b17 = ((int)V+c) > (int)R;
  uint16_t csub = R ^ V ^ c;
  uint16_t borrow = value ^ csub;
  _reg.F = ((value>>8)&F_SXY) | (value==0?F_Z:0) | (borrow&0x1000?F_H:0) |
     (overflow?F_PV:0) | F_N | (b17?F_C:0);
  return value;
}

static uint8_t parity(uint8_t v)
{
  uint8_t p = F_PV, b=128;
  while(b) {
    if (v&b) p^=F_PV;
    b>>=1;
  }
  return p;
}

/*          high            low      # added   CF after
 NF | CF |  nibble |  HF |  nibble |  to A   | execution
  0 |  0 |   0-9   |  0  |   0-9   |   00    |  0
  0 |  0 |   0-8   |  0  |   a-f   |   06    |  0
  0 |  0 |   0-9   |  1  |   0-3   |   06    |  0
  0 |  0 |   a-f   |  0  |   0-9   |   60    |  1
  0 |  0 |   9-f   |  0  |   a-f   |   66    |  1
  0 |  0 |   a-f   |  1  |   0-3   |   66    |  1
  0 |  1 |   0-2   |  0  |   0-9   |   60    |  1
  0 |  1 |   0-2   |  0  |   a-f   |   66    |  1
  0 |  1 |   0-3   |  1  |   0-3   |   66    |  1
  1 |  0 |   0-9   |  0  |   0-9   |   00    |  0
  1 |  0 |   0-8   |  1  |   6-f   |   fa    |  0
  1 |  1 |   7-f   |  0  |   0-9   |   a0    |  1
  1 |  1 |   6-f   |  1  |   6-f   |   9a    |  1
*/

static uint8_t DAA(void)
{
  uint8_t add = 0, flags = 0;
  int ch = (_reg.F& F_N? 4: 0) | (_reg.F & F_C? 2 : 0) | (_reg.F & F_H? 1 : 0);
  int h = (_reg.A>>4)&0x0F, l = _reg.A & 0x0F;

#if 0
  switch(ch) {
  // Addition:
    case 0:
      if (l > 9) {
	if (h < 9) { add = 0x06; flags = F_H; }
	else { add = 0x66; flags = F_C | F_H; }
      } else if (h > 9) { add = 0x60; flags = F_C; }
      break;
    case 1:
      if (l < 4) {
	if (h < 10) add = 0x06;
	else { add = 0x66; flags = F_C; }
      }
      break;
    case 2:
      if (h < 3) {
	if (l < 10) add = 0x60;
	else { add = 0x66; flags = F_H; }
	flags |= F_C;
      }
      break;
    case 3:
      if (h < 4 && l < 4) { add = 0x66;  flags = F_C; }
      break;
  // Subtraction:
    case 5:
      if (h < 9 && l > 5) { add = 0xFA; flags = F_H; }
      break;
    case 6:
      if (h > 6 && l < 10) { add = 0xA0; flags = F_C; }
      break;
    case 7:
      if (h > 5 && l > 5) { add = 0x9A; flags = F_C | F_H; }
      break;
  }
  _reg.A += add;
#else
/* "updated" table: */
/*
C h   H l   diff
0 0-9 0 0-9 00
0 0-9 1 0-9 06
0 0-8 * a-f 06
0 a-f 0 0-9 60
1  *  0 0-9 60
1  *  1 0-9 66
1  *  * a-f 66
0 9-f * a-f 66
0 a-f 1 0-9 66
 */
  switch(ch&3) {
    case 0:
/*
C h   H l   diff
0 0-9 0 0-9 00
0 0-8 * a-f 06
0 a-f 0 0-9 60
0 9-f * a-f 66
 */
      add = l > 9 ? 6 : 0;
      if(h > 9 || (h == 9 && l > 9))
	  add += 0x60;
      break;
    case 1:
/*
C h   H l   diff
0 0-9 1 0-9 06
0 0-8 * a-f 06
0 9-f * a-f 66
0 a-f 1 0-9 66
 */
      add = 0x06;
      if(h > 9 || (h == 9 && l > 9))
	add += 0x60;
      break;
    case 2:
/*
C h   H l   diff
1  *  0 0-9 60
1  *  * a-f 66
 */
      add = l > 9 ? 0x66 : 0x60;
      break;
    case 3:
/*
C h   H l   diff
1  *  1 0-9 66
1  *  * a-f 66
 */
      add = 0x66;
      break;
  }
  _reg.A += (_reg.F&F_N ? -add : add);
/*
C h   l   C'
0 0-9 0-9 0
0 0-8 a-f 0
0 9-f a-f 1
0 a-f 0-9 1
1 *   *   1
*/
    flags = _reg.F&F_C ? F_C : h > 9 ? F_C : h == 9 && l > 9 ? F_C : 0;
/*
N H l   H'
0 * 0-9 0
0 * a-f 1
1 0 *   0
1 1 6-f 0
1 1 0-5 1
*/
    if(_reg.F&F_N)
      flags |= l < 6 && (_reg.F&F_H) ? F_H : 0;
    else
      flags |= l > 9 ? F_H : 0;
#endif
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | parity(_reg.A) | flags | (_reg.F&F_N);
  return 4;
}

static void AND(uint8_t V)
{
  _reg.A &= V;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | F_H | parity(_reg.A);
}

static void OR(uint8_t V)
{
  _reg.A |= V;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | parity(_reg.A);
}

static void XOR(uint8_t V)
{
  _reg.A ^= V;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) | parity(_reg.A);
}

static void CP(uint8_t V)
{
  uint16_t value = (int8_t)_reg.A - (int8_t)V;
  int overflow = ((_reg.A&0x80) != (V&0x80)) && ((value&0x80) != (_reg.A&0x80));
  uint8_t csub = _reg.A ^ V;
  int b9 = (int)V > (int)_reg.A;
  uint8_t borrow = (uint8_t)value ^ csub;
  _reg.F = (value&F_S) | (V&F_XY) | (value==0?F_Z:0) |
    (borrow&F_H) | (overflow?F_PV:0) | F_N | (b9?F_C:0);
}

static uint8_t INC(uint8_t a)
{
  uint8_t cadd = a ^ 1, b = a+1;
  uint8_t carry = b ^ cadd;
  _reg.F = (b&F_SXY) | (b==0?F_Z:0) | (carry&F_H) | (a==0x7F?F_PV:0) |
    (_reg.F&F_C);
  return b;
}

static uint8_t DEC(uint8_t a)
{
  uint8_t csub = a ^ 1, b = a-1;
  uint8_t borrow = b ^ csub;
  _reg.F = (b&F_SXY) | (b==0?F_Z:0) | (borrow&F_H) | (a==0x80?F_PV:0) |
     F_N | (_reg.F&F_C);
  return b;
}

static int NEG(void)
{
  int acc = _reg.A;
  _reg.A = 0 - _reg.A;
  uint8_t borrow = _reg.A ^ acc;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0?F_Z:0) |
    (borrow&F_H) | (acc==0x80?F_PV:0) | F_N | (acc?F_C:0);
  return 8;
}

static uint8_t RLC(uint8_t r)
{
  int n = r&0x80;
  r = (r<<1) | (n? 1: 0);
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t RL(uint8_t r)
{
  int n = r&0x80;
  r = (r<<1) | (_reg.F&F_C? 1: 0);
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t RRC(uint8_t r)
{
  int n = r&0x01;
  r = (r>>1) | (n? 0x80: 0);
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t RR(uint8_t r)
{
  int n = r&0x01;
  r = (r>>1) | (_reg.F&F_C? 0x80: 0);
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t SLA(uint8_t r)
{
  int n = r&0x80;
  r <<= 1;
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t SLL(uint8_t r)
{
  int n = r&0x80;
  r = (r<<1) | 1;
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t SRA(uint8_t r)
{
  int n = r&0x01;
  r = ((int8_t)r >> 1);
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t SRL(uint8_t r)
{
  int n = r&0x01;
  r >>= 1;
  _reg.F = (r&F_SXY) | (r==0? F_Z:0) | parity(r) | (n? F_C:0);
  return r;
}

static uint8_t RLD(uint8_t r)
{
  uint8_t h = (r>>4)&0x0F;
  r = r<<4 | (_reg.A&0x0F);
  _reg.A = (_reg.A&0xF0) | h;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0? F_Z:0) | parity(_reg.A) | (_reg.F&F_C);
  return r;
}

static uint8_t RRD(uint8_t r)
{
  uint8_t l = (r&0x0F);
  r = (_reg.A&0x0F)<<4 | r>>4;
  _reg.A = (_reg.A&0xF0) | l;
  _reg.F = (_reg.A&F_SXY) | (_reg.A==0? F_Z:0) | parity(_reg.A) | (_reg.F&F_C);
  return r;
}

static void BIT(uint8_t b, uint8_t r)
{
  _reg.F &= ~(F_Z | F_N | F_SXY | F_PV);
  int set = r&(1<<b);
  _reg.F |= (set?0:F_Z|F_PV) | (set & F_S) | (r & F_XY) | F_H;
}

static int CALL(uint16_t addr)
{
  store(--_SP, (_PC>>8)&0xFF);
  store(--_SP, _PC & 0xFF);
  _PC = addr;
  return 17;
}

static int RET(void)
{
  _PC = load(_SP) | (load(_SP+1)<<8);
  _SP+=2;
  return 11;
}

static uint8_t IO(uint8_t v)
{
  _reg.F = (v&F_SXY) | (v==0?F_Z:0) | parity(v) | (_reg.F&F_C);
  return v;
}

static int INX(int dir)
{
  uint8_t n;
  store(_reg.HL, n = io_read(_reg.C, _reg.B--));
  _reg.HL += dir;
  _reg.F &= ~(F_Z|F_S);
  _reg.F |= (_reg.B&F_SXY) | (_reg.B==0?F_Z:0) | F_N;
  if ((_reg.C+dir+n) > 255) _reg.F |= (F_C|F_H);
  return 16;
}

static int OUTX(int dir)
{
  uint8_t n;
  io_write(_reg.C, --_reg.B, n = load(_reg.HL));
  _reg.HL += dir;

  _reg.F &= ~(F_Z|F_S|F_N);
  _reg.F |= (_reg.B&F_SXY) | (_reg.B == 0? F_Z: 0) | (n&0x80?F_N:0);
  if ((_reg.L+n) > 255) _reg.F |= (F_C|F_H);

  return 16;
}

#if 0
static char *status(uint8_t F)
{
  static char f[8];
  f[0] = F & F_S? 'S' : '-';
  f[1] = F & F_Z? 'Z' : '-';
  f[2] = F & F_H? 'H' : '-';
  f[3] = F & F_PV? 'P' : '-';
  f[4] = F & F_N? 'N' : '-';
  f[5] = F & F_C? 'C' : '-';
  f[6] = 0;
  return f;
}
#endif

static void pinstr(uint16_t PC, int pfx, uint8_t d, const char *instr)
{
  uint16_t addr;

  if (instr == NULL) return;
#if 0
  fprintf(_log,"a%02Xf%sbc%04X(%02X)de%04X(%02X)hl%04X(%02X%02X)ix%04Xiy%04X ",
	  _reg.A, status(_reg.F), _reg.BC, load(_reg.BC), _reg.DE, load(_reg.DE),
	  _reg.HL, load(_reg.HL+1),load(_reg.HL), _reg.IX, _reg.IY);
#endif
  if (pfx) fprintf(_log, "%04X %X %02X:", PC, pfx, _IR);
  else fprintf(_log, "%04X %02X:", PC, _IR);
  for(int i=0; instr[i]; i++) {
    switch(instr[i]) {
      case 'd':
	fprintf(_log, "%02X", d);
	break;
      case 'e':
	fprintf(_log, "%02X", load(_PC));
	break;
      case 'n':
	if (instr[i+1] == 'n') {
	  i++;
	  addr = load(_PC) | load(_PC+1)<<8;
	  fprintf(_log, "%04X", addr);
	} else {
	  fprintf(_log, "%02X", load(_PC));
	}
	break;
      default:
	fputc(instr[i], _log);
	break;
    }
  }
  fputc('\n', _log);
}

static void run()
{
  struct timeval t;
  time_t turbo_sec;
  unsigned long cur, prev, turbo_clks = 0;
  int cycles = 0, clocks = 0;
  uint16_t addr;
  uint8_t l, h, o, n;
  int halted = false;

  gettimeofday(&t, NULL);
  prev = t.tv_sec*1000000+t.tv_usec;
  turbo_sec = t.tv_sec;

  while(1) {
    _R = (((_R&0x7F)+1)&0x7F) | (_R&0x80);
    _IR = load(_PC++);
     if (_log) pinstr(_PC-1, 0, 0, _instr[_IR]);

    switch(_IR) {
      case 0x00:	// NOP
	cycles = 4; break;
      case 0x76:	// HALT
	halted = true;
	cycles = 4; break;
      case 0xF3:	// DI
	_IFF1 = _IFF2 = 0;
	cycles = 4;
	break;
      case 0xFB:	// EI
	_IFF1 = _IFF2 = 1;
	cycles = 4;
	break;
      case 0x02:	// LD (BC),A
	store(_reg.BC, _reg.A);
	cycles = 7;
	break;
      case 0x12:	// LD (DE),A
	store(_reg.DE, _reg.A);
	cycles = 7;
	break;
      case 0x36:	// LD (HL),n
	store(_reg.HL, immediate_value());
	cycles = 10;
	break;
      case 0x70 ... 0x75:  // LD (HL),r
      case 0x77:
	store(_reg.HL, reg8(_IR & 0x7));
	cycles = 7;
	break;
      case 0x32:	// LD (nn), A
	store(immediate_addr(), _reg.A);
	cycles = 13;
	break;
      case 0x22:	// LD (nn), HL
	addr = immediate_addr();
	store(addr,   _reg.L);
	store(addr+1, _reg.H);
	cycles = 16;
	break;
      case 0x0A:	// LD A,(BC)
	_reg.A = load(_reg.BC);	cycles = 7; break;
      case 0x1A:	// LD A,(DE)
	_reg.A = load(_reg.DE);	cycles = 7; break;
      case 0x7E:	// LD A,(HL)
	_reg.A = load(_reg.HL);	cycles = 7; break;
      case 0x3A:	// LD A,(nn)
	_reg.A = load(immediate_addr());
	cycles = 13;
	break;
      case 0x3E:	// LD A,n
	_reg.A = immediate_value();
	cycles = 7;
	break;
      case 0x78 ... 0x7D:	// LD A,r
      case 0x7F:
	_reg.A = reg8(_IR & 0x07);
	cycles = 4;
	break;
      case 0x46:	// LD B,(HL)
	_reg.B = load(_reg.HL);
	cycles = 7;
	break;
      case 0x06:	// LD B,n
	_reg.B = immediate_value();
	cycles = 7;
	break;
      case 0x40 ... 0x45: // LD B,r
      case 0x47:
	_reg.B = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x4E:	// LD C,(HL)
	_reg.C = load(_reg.HL);
	cycles = 7;
	break;
      case 0x0E:	// LD C,n
	_reg.C = immediate_value();
	cycles = 7;
	break;
      case 0x48 ... 0x4D: // LD C,r
      case 0x4F:
	_reg.C = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x56:	// LD D,(HL)
	_reg.D = load(_reg.HL);
	cycles = 7;
	break;
      case 0x16:	// LD D,n
	_reg.D = immediate_value();
	cycles = 7;
	break;
      case 0x50 ... 0x55: // LD D,r
      case 0x57:
	_reg.D = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x5E:	// LD E,(HL)
	_reg.E = load(_reg.HL);
	cycles = 7;
	break;
      case 0x1E:	// LD E,n
	_reg.E = immediate_value();
	cycles = 7;
	break;
      case 0x58 ... 0x5D: // LD E,r
      case 0x5F:
	_reg.E = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x66:	// LD H,(HL)
	_reg.H = load(_reg.HL);
	cycles = 7;
	break;
      case 0x26:	// LD H,n
	_reg.H = immediate_value();
	cycles = 7;
	break;
      case 0x60 ... 0x65: // LD H,r
      case 0x67:
	_reg.H = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x6E:	// LD L,(HL)
	_reg.L = load(_reg.HL);
	cycles = 7;
	break;
      case 0x2E:	// LD L,n
	_reg.L = immediate_value();
	cycles = 7;
	break;
      case 0x68 ... 0x6D: // LD L,r
      case 0x6F:
	_reg.L = reg8(_IR & 0x7);
	cycles = 4;
	break;
      case 0x2A:	// LD HL,(nn)
	addr = immediate_addr();
	_reg.L = load(addr);
	_reg.H = load(addr+1);
	cycles = 16;
	break;
      case 0x01:	// LD BC,nn
	_reg.BC = immediate_addr();
	cycles = 10;
	break;
      case 0x11:	// LD DE,nn
	_reg.DE = immediate_addr();
	cycles = 10;
	break;
      case 0x21:	// LD HL,nn
	_reg.HL = immediate_addr();
	cycles = 10;
	break;
      case 0x31:	// LD SP,nn
	_SP = immediate_addr();
	cycles = 10;
	break;
      case 0xF9:	// LD SP,HL
	_SP = _reg.HL;
	cycles = 6;
	break;
      case 0xF1:	// POP AF
	_reg.F = load(_SP++);
	_reg.A = load(_SP++);
	cycles = 10;
	break;
      case 0xC1:	// POP BC
	_reg.C = load(_SP++);
	_reg.B = load(_SP++);
	cycles = 10;
	break;
      case 0xD1:	// POP DE
	_reg.E = load(_SP++);
	_reg.D = load(_SP++);
	cycles = 10;
	break;
      case 0xE1:	// POP HL
	_reg.L = load(_SP++);
	_reg.H = load(_SP++);
	cycles = 10;
	break;
      case 0xF5:	// PUSH AF
	store(--_SP, _reg.A);
	store(--_SP, _reg.F);
	cycles = 11;
	break;
      case 0xC5:	// PUSH BC
	store(--_SP, _reg.B);
	store(--_SP, _reg.C);
	cycles = 11;
	break;
      case 0xD5:	// PUSH DE
	store(--_SP, _reg.D);
	store(--_SP, _reg.E);
	cycles = 11;
	break;
      case 0xE5:	// PUSH HL
	store(--_SP, _reg.H);
	store(--_SP, _reg.L);
	cycles = 11;
	break;
      case 0xEB:	// EX DE,HL
	addr = _reg.DE;	_reg.DE = _reg.HL; _reg.HL = addr;
	cycles = 4;
	break;
      case 0x08:	// EX AF,AF'
	addr = _reg.AF;	_reg.AF = _alt.AF; _alt.AF = addr;
	cycles = 4;
	break;
      case 0xD9:	// EXX
	addr = _reg.BC; _reg.BC = _alt.BC; _alt.BC = addr;
	addr = _reg.DE;	_reg.DE = _alt.DE; _alt.DE = addr;
	addr = _reg.HL;	_reg.HL = _alt.HL; _alt.HL = addr;
	cycles = 4;
	break;
      case 0xE3:	// EX (SP),HL;
	l = load(_SP);   store(_SP,  _reg.L); _reg.L = l;
	h = load(_SP+1); store(_SP+1,_reg.H); _reg.H = h;
	cycles = 19;
	break;
      case 0x80 ... 0x85:	// ADD A,r
      case 0x87:
	ADD(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xC6:	// ADD A,n
	ADD(immediate_value());
	cycles = 7;
	break;
      case 0x86:	// ADD A,(HL)
	ADD(load(_reg.HL));
	cycles = 7;
	break;
      case 0x88 ... 0x8D:	// ADC A,r
      case 0x8F:
	ADC(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xCE:	// ADC A,n
	ADC(immediate_value());
	cycles = 7;
	break;
      case 0x8E:	// ADC A,(HL)
	ADC(load(_reg.HL));
	cycles = 7;
	break;
      case 0x90 ... 0x95:	// SUB r
      case 0x97:
	SUB(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xD6:	// SUB n
	SUB(immediate_value());
	cycles = 7;
	break;
      case 0x96:	// SUB (HL)
	SUB(load(_reg.HL));
	cycles = 7;
	break;
      case 0x98 ... 0x9D:	// SBC A,r
      case 0x9F:
	SBC(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xDE:	// SBC A,n
	SBC(immediate_value());
	cycles = 7;
	break;
      case 0x9E:	// SBC A,(HL)
	SBC(load(_reg.HL));
	cycles = 7;
	break;
      case 0xA0 ... 0xA5:	// AND r
      case 0xA7:
	AND(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xE6:	// AND n
	AND(immediate_value());
	cycles = 7;
	break;
      case 0xA6:	// AND (HL)
	AND(load(_reg.HL));
	cycles = 7;
	break;
      case 0xA8 ... 0xAD:	// XOR r
      case 0xAF:
	XOR(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xEE:	// XOR n
	XOR(immediate_value());
	cycles = 7;
	break;
      case 0xAE:	// XOR (HL)
	XOR(load(_reg.HL));
	cycles = 7;
	break;
      case 0xB0 ... 0xB5:	// OR r
      case 0xB7:
	OR(reg8(_IR&0x07));
	cycles = 4;
	break;
      case 0xF6:	// OR n
	OR(immediate_value());
	cycles = 7;
	break;
      case 0xB6:	// OR (HL)
	OR(load(_reg.HL));
	cycles = 7;
	break;
      case 0xB8 ... 0xBD:	// CP r
      case 0xBF:
	CP(reg8(_IR&0x07));
	cycles = 4; break;
      case 0xFE:	// CP n
	CP(immediate_value());
	cycles = 7; break;
      case 0xBE:	// CP (HL)
	CP(load(_reg.HL));
	cycles = 7; break;

      case 0x3C:	// INC A
	INC(_reg.A++); cycles = 4; break;
      case 0x04:	// INC B
	INC(_reg.B++); cycles = 4; break;
      case 0x0C:	// INC C
	INC(_reg.C++); cycles = 4; break;
      case 0x14:	// INC D
	INC(_reg.D++); cycles = 4; break;
      case 0x1C:	// INC E
	INC(_reg.E++); cycles = 4; break;
      case 0x24:	// INC H
	INC(_reg.H++); cycles = 4; break;
      case 0x2C:	// INC L
	INC(_reg.L++); cycles = 4; break;
      case 0x34:	// INC (HL)
	store(_reg.HL, INC(load(_reg.HL)));  cycles = 11; break;

      case 0x3D:	// DEC A
	DEC(_reg.A--); cycles = 4; break;
      case 0x05:	// DEC B
	DEC(_reg.B--); cycles = 4; break;
      case 0x0D:	// DEC C
	DEC(_reg.C--); cycles = 4; break;
      case 0x15:	// DEC D
	DEC(_reg.D--); cycles = 4; break;
      case 0x1D:	// DEC E
	DEC(_reg.E--); cycles = 4; break;
      case 0x25:	// DEC H
	DEC(_reg.H--); cycles = 4; break;
      case 0x2D:	// DEC L
	DEC(_reg.L--); cycles = 4; break;
      case 0x35:	// DEC (HL)
	store(_reg.HL, DEC(load(_reg.HL))); cycles = 11; break;

      case 0x2F:	// CPL
	_reg.A = ~_reg.A; _reg.F = (_reg.F&(F_S|F_Z|F_PV|F_C)) | (_reg.A&F_XY) | (F_H | F_N);
	cycles = 4;
	break;
      case 0x3F:	// CCF
	h = _reg.F&F_C?F_H:0;
	_reg.F ^= F_C; _reg.F &= ~(F_N|F_H|F_XY); _reg.F |= h | (_reg.A&F_XY);
	cycles = 4;
	break;
      case 0x37:	// SCF
	_reg.F &= ~(F_H|F_N|F_XY); _reg.F |= F_C | (_reg.A&F_XY);
	cycles = 4;
	break;
      case 0x09:	// ADD HL,BC
	_reg.HL = ADD16(_reg.HL, _reg.BC); cycles = 11; break;
      case 0x19:	// ADD HL,DE
	_reg.HL = ADD16(_reg.HL, _reg.DE); cycles = 11; break;
      case 0x29:	// ADD HL,HL
	_reg.HL = ADD16(_reg.HL, _reg.HL); cycles = 11; break;
      case 0x39:	// ADD HL,SP
	_reg.HL = ADD16(_reg.HL, _SP);     cycles = 11; break;

      case 0x03:	// INC BC
	_reg.BC++; cycles = 6; break;
      case 0x13:	// INC DE
	_reg.DE++; cycles = 6; break;
      case 0x23:	// INC HL
	_reg.HL++; cycles = 6; break;
      case 0x33:	// INC SP
	_SP++; cycles = 6; break;
      case 0x0B:	// DEC BC
	_reg.BC--; cycles = 6; break;
      case 0x1B:	// DEC DE
	_reg.DE--; cycles = 6; break;
      case 0x2B:	// DEC HL
	_reg.HL--; cycles = 6; break;
      case 0x3B:	// DEC SP
	_SP--; cycles = 6; break;

      case 0x27:
	cycles = DAA();
	break;

      case 0x07:	// RCLA
	n = _reg.A&0x80;
	_reg.A = (_reg.A << 1) | (n? 1 : 0);
	_reg.F &= (F_S | F_Z | F_PV | F_XY); _reg.F |= (n? F_C : 0) | (_reg.A&F_XY);
	cycles = 4;
	break;
      case 0x17:	// RLA
	n = _reg.F&F_C;
	_reg.F &= (F_S | F_Z | F_PV); _reg.F |= (_reg.A&0x80? F_C : 0);
	_reg.A = (_reg.A << 1) | (n? 1 : 0);
	_reg.F |= _reg.A & F_XY;
	cycles = 4;
	break;
      case 0x0F:	// RRCA
	n = _reg.A&0x01;
	_reg.A = (_reg.A >> 1) | (n? 0x80 : 0);
	_reg.F &= (F_S | F_Z | F_PV); _reg.F |= (n? F_C : 0) | (_reg.A&F_XY);
	cycles = 4;
	break;
      case 0x1F:	// RRA
	n = _reg.F&F_C;
	_reg.F &= (F_S | F_Z | F_PV); _reg.F |= (_reg.A&0x01? F_C : 0);
	_reg.A = (_reg.A >> 1) | (n? 0x80 : 0);
	_reg.F |= _reg.A & F_XY;
	cycles = 4;
	break;
      case 0xC3:	// JP nn
	_PC = immediate_addr();  cycles = 10; break;
      case 0xE9:	// JP (HL)
	_PC = _reg.HL; cycles = 4; break;
      case 0xC2:	// JP NZ,nn
	addr = immediate_addr(); cycles = 10;
	if (!(_reg.F & F_Z)) _PC = addr;
	break;
      case 0xCA:	// JP Z,nn
	addr = immediate_addr(); cycles = 10;
	if (_reg.F & F_Z) _PC = addr;
	break;
      case 0xD2:	// JP NC,nn
	addr = immediate_addr(); cycles = 10;
	if (!(_reg.F & F_C)) _PC = addr;
	break;
      case 0xDA:	// JP C,nn
	addr = immediate_addr(); cycles = 10;
	if (_reg.F & F_C) _PC = addr;
	break;
      case 0xE2:	// JP PO,nn
	addr = immediate_addr(); cycles = 10;
	if (!(_reg.F & F_PV)) _PC = addr;
	break;
      case 0xEA:	// JP PE,nn
	addr = immediate_addr(); cycles = 10;
	if (_reg.F & F_PV) _PC = addr;
	break;
      case 0xF2:	// JP P,nn
	addr = immediate_addr(); cycles = 10;
	if (!(_reg.F & F_S)) _PC = addr;
	break;
      case 0xFA:	// JP M,nn
	addr = immediate_addr(); cycles = 10;
	if (_reg.F & F_S) _PC = addr;
	break;
      case 0x10:	// DJNZ o
	o = immediate_value(); cycles = 8;
	if (--_reg.B != 0) { _PC += (int8_t)o; cycles += 5; }
	break;
      case 0x18:	// JR e
	_PC += (int8_t)immediate_value();
	cycles = 12;
	break;
      case 0x20:	// JR NZ,o
	o = immediate_value(); cycles = 7;
	if (!(_reg.F & F_Z)) { _PC += (int8_t)o; cycles += 5; }
	break;
      case 0x28:	// JR Z,o
	o = immediate_value(); cycles = 7;
	if (_reg.F & F_Z) { _PC += (int8_t)o; cycles += 5; }
	break;
      case 0x30:	// JR NC,o
	o = immediate_value(); cycles = 7;
	if (!(_reg.F & F_C)) { _PC += (int8_t)o; cycles += 5; }
	break;
      case 0x38:	// JR C,o
	o = immediate_value(); cycles = 7;
	if (_reg.F & F_C) { _PC += (int8_t)o; cycles += 5; }
	break;
      case 0xCD:	// CALL nn
	cycles = CALL(immediate_addr());
	break;
      case 0xC4:	// CALL NZ,nn
	addr = immediate_addr();
	if (!(_reg.F & F_Z)) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xCC:	// CALL Z,nn
	addr = immediate_addr();
	if (_reg.F & F_Z) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xD4:	// CALL NC,nn
	addr = immediate_addr();
	if (!(_reg.F & F_C)) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xDC:	// CALL C,nn
	addr = immediate_addr();
	if (_reg.F & F_C) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xE4:	// CALL PO,nn
	addr = immediate_addr();
	if (!(_reg.F & F_PV)) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xEC:	// CALL PE,nn
	addr = immediate_addr();
	if (_reg.F & F_PV) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xF4:	// CALL P,nn
	addr = immediate_addr();
	if (!(_reg.F & F_S)) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xFC:	// CALL M,nn
	addr = immediate_addr();
	if (_reg.F & F_S) cycles = CALL(addr);
	else cycles = 10;
	break;
      case 0xC9:	// RET
	cycles = RET()-1;
	break;
      case 0xC0:	// RET NZ
	if (!(_reg.F & F_Z)) cycles = RET();
	else cycles = 5;
	break;
      case 0xC8:	// RET Z
	if (_reg.F & F_Z) cycles = RET();
	else cycles = 5;
	break;
      case 0xD0:	// RET NC
	if (!(_reg.F & F_C)) cycles = RET();
	else cycles = 5;
	break;
      case 0xD8:	// RET C
	if (_reg.F & F_C) cycles = RET();
	else cycles = 5;
	break;
      case 0xE0:	// RET PO
	if (!(_reg.F & F_PV)) cycles = RET();
	else cycles = 5;
	break;
      case 0xE8:	// RET PE
	if (_reg.F & F_PV) cycles = RET();
	else cycles = 5;
	break;
      case 0xF0:	// RET P
	if (!(_reg.F & F_S)) cycles = RET();
	else cycles = 5;
	break;
      case 0xF8:	// RET M
	if (_reg.F & F_S) cycles = RET();
	else cycles = 10;
	break;
      case 0xC7: case 0xCF: case 0xD7: case 0xDF:	// RST p
      case 0xE7: case 0xEF: case 0xF7: case 0xFF:
	CALL(_IR & 0x38);
	cycles = 11;
	break;
      case 0xDB:	// IN A,(n)
	_reg.A = io_read(immediate_value(), _reg.A);
	cycles = 11;
	break;
      case 0xD3:	// OUT (n),A
	io_write(immediate_value(), _reg.A, _reg.A);
	cycles = 11;
	break;


      case 0xCB:	// CB instruction set:
	_IR = load(_PC++);
	if (_log) pinstr(_PC-2, 0xCB, 0, CB_instr[_IR]);

	switch(_IR) {
	  case 0x00:	// RLC B
	    _reg.B = RLC(_reg.B); cycles = 8; break;
	  case 0x01:	// RLC C
	    _reg.C = RLC(_reg.C); cycles = 8; break;
	  case 0x02:	// RLC D
	    _reg.D = RLC(_reg.D); cycles = 8; break;
	  case 0x03:	// RLC E
	    _reg.E = RLC(_reg.E); cycles = 8; break;
	  case 0x04:	// RLC H
	    _reg.H = RLC(_reg.H); cycles = 8; break;
	  case 0x05:	// RLC L
	    _reg.L = RLC(_reg.L); cycles = 8; break;
	  case 0x06:	// RLC (HL)
	    store(_reg.HL, RLC(load(_reg.HL))); cycles = 15; break;
	  case 0x07:	// RLC A
	    _reg.A = RLC(_reg.A); cycles = 8; break;

	  case 0x08:	// RRC B
	    _reg.B = RRC(_reg.B); cycles = 8; break;
	  case 0x09:	// RRC C
	    _reg.C = RRC(_reg.C); cycles = 8; break;
	  case 0x0A:	// RRC D
	    _reg.D = RRC(_reg.D); cycles = 8; break;
	  case 0x0B:	// RRC E
	    _reg.E = RRC(_reg.E); cycles = 8; break;
	  case 0x0C:	// RRC H
	    _reg.H = RRC(_reg.H); cycles = 8; break;
	  case 0x0D:	// RRC L
	    _reg.L = RRC(_reg.L); cycles = 8; break;
	  case 0x0E:	// RRC (HL)
	    store(_reg.HL, RRC(load(_reg.HL))); cycles = 15; break;
	  case 0x0F:	// RRC A
	    _reg.A = RRC(_reg.A); cycles = 8; break;

	  case 0x10:	// RL B
	    _reg.B = RL(_reg.B); cycles = 8; break;
	  case 0x11:	// RL C
	    _reg.C = RL(_reg.C); cycles = 8; break;
	  case 0x12:	// RL D
	    _reg.D = RL(_reg.D); cycles = 8; break;
	  case 0x13:	// RL E
	    _reg.E = RL(_reg.E); cycles = 8; break;
	  case 0x14:	// RL H
	    _reg.H = RL(_reg.H); cycles = 8; break;
	  case 0x15:	// RL L
	    _reg.L = RL(_reg.L); cycles = 8; break;
	  case 0x16:	// RL (HL)
	    store(_reg.HL, RL(load(_reg.HL))); cycles = 15; break;
	  case 0x17:	// RL A
	    _reg.A = RL(_reg.A); cycles = 8; break;

	  case 0x18:	// RR B
	    _reg.B = RR(_reg.B); cycles = 8; break;
	  case 0x19:	// RR C
	    _reg.C = RR(_reg.C); cycles = 8; break;
	  case 0x1A:	// RR D
	    _reg.D = RR(_reg.D); cycles = 8; break;
	  case 0x1B:	// RR E
	    _reg.E = RR(_reg.E); cycles = 8; break;
	  case 0x1C:	// RR H
	    _reg.H = RR(_reg.H); cycles = 8; break;
	  case 0x1D:	// RR L
	    _reg.L = RR(_reg.L); cycles = 8; break;
	  case 0x1E:	// RR (HL)
	    store(_reg.HL, RR(load(_reg.HL))); cycles = 15; break;
	  case 0x1F:	// RR A
	    _reg.A = RR(_reg.A); cycles = 8; break;

	  case 0x20:	// SLA B
	    _reg.B = SLA(_reg.B); cycles = 8; break;
	  case 0x21:	// SLA C
	    _reg.C = SLA(_reg.C); cycles = 8; break;
	  case 0x22:	// SLA D
	    _reg.D = SLA(_reg.D); cycles = 8; break;
	  case 0x23:	// SLA E
	    _reg.E = SLA(_reg.E); cycles = 8; break;
	  case 0x24:	// SLA H
	    _reg.H = SLA(_reg.H); cycles = 8; break;
	  case 0x25:	// SLA L
	    _reg.L = SLA(_reg.L); cycles = 8; break;
	  case 0x26:	// SLA (HL)
	    store(_reg.HL, SLA(load(_reg.HL))); cycles = 15; break;
	  case 0x27:	// SLA A
	    _reg.A = SLA(_reg.A); cycles = 8; break;

	  case 0x28:	// SRA B
	    _reg.B = SRA(_reg.B); cycles = 8; break;
	  case 0x29:	// SRA C
	    _reg.C = SRA(_reg.C); cycles = 8; break;
	  case 0x2A:	// SRA D
	    _reg.D = SRA(_reg.D); cycles = 8; break;
	  case 0x2B:	// SRA E
	    _reg.E = SRA(_reg.E); cycles = 8; break;
	  case 0x2C:	// SRA H
	    _reg.H = SRA(_reg.H); cycles = 8; break;
	  case 0x2D:	// SRA L
	    _reg.L = SRA(_reg.L); cycles = 8; break;
	  case 0x2E:	// SRA (HL)
	    store(_reg.HL, SRA(load(_reg.HL))); cycles = 15; break;
	  case 0x2F:	// SRA A
	    _reg.A = SRA(_reg.A); cycles = 8; break;

	  case 0x30:	// SLL B
	    _reg.B = SLL(_reg.B); cycles = 8; break;
	  case 0x31:	// SLL C
	    _reg.C = SLL(_reg.C); cycles = 8; break;
	  case 0x32:	// SLL D
	    _reg.D = SLL(_reg.D); cycles = 8; break;
	  case 0x33:	// SLL E
	    _reg.E = SLL(_reg.E); cycles = 8; break;
	  case 0x34:	// SLL H
	    _reg.H = SLL(_reg.H); cycles = 8; break;
	  case 0x35:	// SLL L
	    _reg.L = SLL(_reg.L); cycles = 8; break;
	  case 0x36:	// SLL (HL)
	    store(_reg.HL, SLL(load(_reg.HL))); cycles = 15; break;
	  case 0x37:	// SLL A
	    _reg.A = SLL(_reg.A); cycles = 8; break;

	  case 0x38:	// SRL B
	    _reg.B = SRL(_reg.B); cycles = 8; break;
	  case 0x39:	// SRL C
	    _reg.C = SRL(_reg.C); cycles = 8; break;
	  case 0x3A:	// SRL D
	    _reg.D = SRL(_reg.D); cycles = 8; break;
	  case 0x3B:	// SRL E
	    _reg.E = SRL(_reg.E); cycles = 8; break;
	  case 0x3C:	// SRL H
	    _reg.H = SRL(_reg.H); cycles = 8; break;
	  case 0x3D:	// SRL L
	    _reg.L = SRL(_reg.L); cycles = 8; break;
	  case 0x3E:	// SRL (HL)
	    store(_reg.HL, SRL(load(_reg.HL))); cycles = 15; break;
	  case 0x3F:	// SRL A
	    _reg.A = SRL(_reg.A); cycles = 8; break;
	  case 0x40 ... 0x7F:	// BIT b,r / BIT b,(HL)
	    BIT((_IR>>3)&0x07, reg8(_IR&0x07));
	    if((_IR&0x07)==6)
	      _reg.F &= ~F_XY;
	    cycles = 8 + ((_IR&0x07)==6?4:0); break;
	  case 0xC0 ... 0xFF:	// SET b,r / SET b,(HL)
	    h = (_IR>>3)&0x07; l = _IR&0x07;
	    setreg8(l, reg8(l)|(1<<h));
	    cycles = 8 + (l==6?7:0);
	    break;
	  case 0x80 ... 0xBF:	// RES b,r / RES b,(HL)
	    h = (_IR>>3)&0x07; l = _IR&0x07;
	    setreg8(l, reg8(l) & ~(1<<h));
	    cycles = 8 + (l==6?7:0);
	    break;
	  default:
	    if (_log) {
	      fprintf(_log, "Illegal CB instruction: %02X\n", _IR);
	      fflush(_log);
	    }
	    break;
	}
	break;

      case 0xDD:	// DD instruction set:
	_IR = load(_PC++);
	if (_log) pinstr(_PC-2, 0xDD, 0, DD_instr[_IR]);

	switch(_IR) {
	  case 0x36:	// LD (IX+o),n
	    o = immediate_value();
	    store(_reg.IX+(int8_t)o, immediate_value());
	    cycles = 19;
	    break;
	  case 0x70 ... 0x75:	// LD (IX+o),r
	  case 0x77:
	    store(_reg.IX+(int8_t)immediate_value(), reg8(_IR&7));
	    cycles = 19;
	    break;
	  case 0x22:	// LD (nn),IX
	    addr = immediate_addr();
	    store(addr,   _reg.IXl);
	    store(addr+1, _reg.IXh);
	    cycles = 20;
	    break;
	  case 0x7E:	// LD A,(IX+o)
	    _reg.A = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x46:	// LD B,(IX+o)
	    _reg.B = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x4E:	// LD C,(IX+o)
	    _reg.C = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x56:	// LD D,(IX+o)
	    _reg.D = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x5E:	// LD E,(IX+o)
	    _reg.E = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x66:	// LD H,(IX+o)
	    _reg.H = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x6E:	// LD L,(IX+o)
	    _reg.L = load(_reg.IX + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x2A:	// LD IX,(nn)
	    addr = immediate_addr();
	    _reg.IX = load(addr) | (load(addr+1)<<8);
	    cycles = 20;
	    break;
	  case 0x21:	// LD IX,nn
	    _reg.IX = immediate_addr();
	    cycles = 14;
	    break;
	  case 0xF9:	// LD SP,IX
	    _SP = _reg.IX;
	    cycles = 10;
	    break;
	  case 0xE1:	// POP IX
	    _reg.IXl = load(_SP++);
	    _reg.IXh = load(_SP++);
	    cycles = 14;
	    break;
	  case 0xE5:	// PUSH IX
	    store(--_SP, _reg.IXh);
	    store(--_SP, _reg.IXl);
	    cycles = 14;
	    break;
	  case 0xE3:	// EX (SP),IX
	    l = load(_SP);   store(_SP,   _reg.IXl);
	    h = load(_SP+1); store(_SP+1, _reg.IXh);
	    _reg.IX = (h<<8) | l;
	    cycles = 23;
	    break;
	  case 0x86:	// ADD A,(IX+o)
	    ADD(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x8E:	// ADC A,(IX+o)
	    ADC(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x96:	// SUB (IX+o)
	    SUB(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x9E:	// SBC A,(IX+o)
	    SBC(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xA6:	// AND (IX+o)
	    AND(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xAE:	// XOR (IX+o)
	    XOR(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xB6:	// OR (IX+o)
	    OR(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xBE:	// CP (IX+o)
	    CP(load(_reg.IX+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x34:	// INC (IX+o)
	    addr = _reg.IX+(int8_t)immediate_value();
	    store(addr, INC(load(addr)));
	    cycles = 23;
	    break;
	  case 0x35:	// DEC (IX+o)
	    addr = _reg.IX+(int8_t)immediate_value();
	    store(addr, DEC(load(addr)));
	    cycles = 23;
	    break;
	  case 0x09:	// ADD IX, BC
	    _reg.IX = ADD16(_reg.IX, _reg.BC); cycles = 15; break;
	  case 0x19:	// ADD IX, DE
	    _reg.IX = ADD16(_reg.IX, _reg.DE); cycles = 15; break;
	  case 0x29:	// ADD IX, IX
	    _reg.IX = ADD16(_reg.IX, _reg.IX); cycles = 15; break;
	  case 0x39:	// ADD IX, SP
	    _reg.IX = ADD16(_reg.IX, _SP);     cycles = 15; break;

	  case 0x23:	// INC IX
	    _reg.IX++; cycles = 10; break;
	  case 0x2B:	// DEX IX
	    _reg.IX--; cycles = 10; break;
	  case 0xE9:	// JP (IX)
	    _PC = _reg.IX; cycles = 8; break;


	  case 0xCB:
	    o = immediate_value();
	    _IR = load(_PC++);
	    if (_log) pinstr(_PC-5, 0xDDCB, o, DDCB_instr[_IR]);

	    switch(_IR) {
	      case 0x06:	// RLC (IX+o)
		store(_reg.IX+(int8_t)o, RLC(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x0E:	// RRC (IX+o)
		store(_reg.IX+(int8_t)o, RRC(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x16:	// RL (IX+o)
		store(_reg.IX+(int8_t)o, RL(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x1E:	// RR (IX+o)
		store(_reg.IX+(int8_t)o, RR(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x26:	// SLA (IX+o)
		store(_reg.IX+(int8_t)o, SLA(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x2E:	// SRA (IX+o)
		store(_reg.IX+(int8_t)o, SRA(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x36:	// SLL (IX+o)
		store(_reg.IX+(int8_t)o, SLL(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x3E:	// SRL (IX+o)
		store(_reg.IX+(int8_t)o, SRL(load(_reg.IX+(int8_t)o))); cycles = 23; break;
	      case 0x46: case 0x4E: case 0x56: case 0x5E: // BIT b,(IX+o)
	      case 0x66: case 0x6E: case 0x76: case 0x7E:
		BIT((_IR>>3)&0x07, load(_reg.IX+(int8_t)o)); cycles = 20;
		// _reg.F = (_reg.F&~F_XY) | (((_reg.IX+((int8_t)o))>>8)&F_XY);
		_reg.F &= ~F_XY;
		break;
	      case 0xC6: case 0xCE: case 0xD6: case 0xDE: // SET b,(IX+o)
	      case 0xE6: case 0xEE: case 0xF6: case 0xFE:
		store(_reg.IX+(int8_t)o, load(_reg.IX+(int8_t)o) | (1<<((_IR>>3)&0x07)));
		cycles = 23;
		break;
	      case 0x86: case 0x8E: case 0x96: case 0x9E: // RES b,(IX+o)
	      case 0xA6: case 0xAE: case 0xB6: case 0xBE:
		store(_reg.IX+(int8_t)o, load(_reg.IX+(int8_t)o) & ~(1<<((_IR>>3)&0x07)));
		cycles = 23;
		break;
	      default:
		if (_log) {
		  fprintf(_log, "Illegal DDCB instruction: %02X\n", _IR);
		  fflush(_log);
		}
		break;
	    }
	    break;
	  default:
	    /* Undocumented:  DD/FD are NOPs that set internal flag */
	    /* If next instruction ignores flag, it's just a NOP */
	    _PC --;
	    cycles = 4;
	    if (_log) {
	      fprintf(_log, "Illegal DD instruction: %02X\n", _IR);
	      fflush(_log);
	    }
	    break;

	}
	break;


      case 0xED:	// ED instruction set:
	_IR = load(_PC++);
	if (_log) pinstr(_PC-2, 0xED, 0, ED_instr[_IR]);

	switch(_IR) {
	  case 0x46:	// IM 0
	    _imode = 0; cycles = 8; break;
	  case 0x56:	// IM 1
	    _imode = 1; cycles = 8; break;
	  case 0x5E:	// IM 2
	    _imode = 2; cycles = 8; break;
	  case 0x43:	// LD (nn),BC
	    addr = immediate_addr();
	    store(addr, _reg.C);
	    store(addr+1, _reg.B);
	    cycles = 20;
	    break;
	  case 0x53:	// LD (nn),DE
	    addr = immediate_addr();
	    store(addr, _reg.E);
	    store(addr+1, _reg.D);
	    cycles = 20;
	    break;
	  case 0x63:	// LD (nn),HL
	    addr = immediate_addr();
	    store(addr, _reg.L);
	    store(addr+1, _reg.H);
	    cycles = 20;
	    break;
	  case 0x73:	// LD (nn),SP
	    addr = immediate_addr();
	    store(addr, _SP & 0xFF);
	    store(addr+1, _SP >> 8);
	    cycles = 20;
	    break;
	  case 0x57:	// LD A,I
	    _reg.A = _I;
	    _reg.F = (_I & F_SXY) | ((_I==0)? F_Z : 0) |
	             (_IFF2? F_PV : 0) | (_reg.F & F_C);
	    cycles = 9;
	    break;
	  case 0x47:	// LD I,A
	    _I = _reg.A;
	    cycles = 9;
	    break;
	  case 0x5F:	// LD A,R
	    _reg.A = _R;
	    _reg.F = (_R & F_SXY) | ((_R==0)? F_Z : 0) |
	             (_IFF2? F_PV : 0) | (_reg.F & F_C);
	    cycles = 9;
	    break;
	  case 0x4F:	// LD R,A
	    _R = _reg.A;
	    cycles = 9;
	    break;
	  case 0x4B:	// LD BC,(nn)
	    addr = immediate_addr();
	    _reg.C = load(addr);
	    _reg.B = load(addr+1);
	    cycles = 20;
	    break;
	  case 0x5B:	// LD DE,(nn)
	    addr = immediate_addr();
	    _reg.E = load(addr);
	    _reg.D = load(addr+1);
	    cycles = 20;
	    break;
	  case 0x6B:	// LD HL,(nn)
	    addr = immediate_addr();
	    _reg.L = load(addr);
	    _reg.H = load(addr+1);
	    cycles = 20;
	    break;
	  case 0x7B:	// LD SP,(nn)
	    addr = immediate_addr();
	    _SP = load(addr) | (load(addr+1)<<8);
	    cycles = 20;
	    break;
	  case 0xA0:	// LDI
	    cycles = LDX(1); break;
	  case 0xB0:	// LDIR
	    cycles = LDX(1);
	    if (_reg.BC) { _PC -= 2; cycles += 5; }
	    break;
	  case 0xA8:	// LDD
	    cycles = LDX(-1); break;
	  case 0xB8:	// LDDR
	    cycles = LDX(-1);
	    if (_reg.BC) { _PC -= 2; cycles += 5; }
	    break;
	  case 0xA1:	// CPI
	    cycles = CPX(1); break;
	  case 0xB1:	// CPIR
	    cycles = CPX(1);
	    if (_reg.BC && !(_reg.F&F_Z)) { _PC -= 2; cycles += 5; }
	    break;
	  case 0xA9:	// CPD
	    cycles = CPX(-1); break;
	  case 0xB9:	// CPDR
	    cycles = CPX(-1);
	    if (_reg.BC && !(_reg.F&F_Z)) { _PC -= 2; cycles += 5; }
	    break;
	  case 0x44:	// NEG
	    cycles = NEG(); break;

	  case 0x4A:	// ADC HL,BC
	    _reg.HL = ADC16(_reg.HL, _reg.BC); cycles = 15; break;
	  case 0x5A:	// ADC HL,DE
	    _reg.HL = ADC16(_reg.HL, _reg.DE); cycles = 15; break;
	  case 0x6A:	// ADC HL,HL
	    _reg.HL = ADC16(_reg.HL, _reg.HL); cycles = 15; break;
	  case 0x7A:	// ADC HL,SP
	    _reg.HL = ADC16(_reg.HL, _SP);     cycles = 15; break;

	  case 0x42:	// SBC HL,BC
	    _reg.HL = SBC16(_reg.HL, _reg.BC); cycles = 15; break;
	  case 0x52:	// SBC HL,DE
	    _reg.HL = SBC16(_reg.HL, _reg.DE); cycles = 15; break;
	  case 0x62:	// SBC HL,HL
	    _reg.HL = SBC16(_reg.HL, _reg.HL); cycles = 15; break;
	  case 0x72:	// SBC HL,SP
	    _reg.HL = SBC16(_reg.HL, _SP);     cycles = 15; break;
	  case 0x6F:	// RLD
	    store(_reg.HL, RLD(load(_reg.HL))); cycles = 18; break;
	  case 0x67:	// RRD
	    store(_reg.HL, RRD(load(_reg.HL))); cycles = 18; break;

	  case 0x78:	// IN A,(C)
	    _reg.A = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x40:	// IN B,(C)
	    _reg.B = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x48:	// IN C,(C)
	    _reg.C = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x50:	// IN D,(C)
	    _reg.D = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x58:	// IN E,(C)
	    _reg.E = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x60:	// IN H,(C)
	    _reg.H = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x68:	// IN L,(C)
	    _reg.L = IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0x70:	// IN F,(C)
	    IO(io_read(_reg.C, _reg.B)); cycles = 12; break;
	  case 0xA2:	// INI
	    cycles = INX(1); break;
	  case 0xB2:	// INIR
	    cycles = INX(1);
	    if (_reg.B) { _PC -= 2; cycles += 5; }
	    break;
	  case 0xAA:	// IND
	    cycles = INX(-1); break;
	  case 0xBA:	// INDR
	    cycles = INX(-1);
	    if (_reg.B) { _PC -= 2; cycles += 5; }
	    break;

	  case 0x79:	// OUT (C),A
	    io_write(_reg.C, _reg.B, _reg.A); cycles = 12; break;
	  case 0x41:	// OUT (C),B
	    io_write(_reg.C, _reg.B, _reg.B); cycles = 12; break;
	  case 0x49:	// OUT (C),C
	    io_write(_reg.C, _reg.B, _reg.C); cycles = 12; break;
	  case 0x51:	// OUT (C),D
	    io_write(_reg.C, _reg.B, _reg.D); cycles = 12; break;
	  case 0x59:	// OUT (C),E
	    io_write(_reg.C, _reg.B, _reg.E); cycles = 12; break;
	  case 0x61:	// OUT (C),H
	    io_write(_reg.C, _reg.B, _reg.H); cycles = 12; break;
	  case 0x69:	// OUT (C),L
	    io_write(_reg.C, _reg.B, _reg.L); cycles = 12; break;
	  case 0xA3:	// OUTI
	    cycles = OUTX(1); break;
	  case 0xB3:	// OTIR
	    cycles = OUTX(1);
	    if (_reg.B) { _PC -= 2; cycles += 5; }
	    break;
	  case 0xAB:	// OUTD
	    cycles = OUTX(-1); break;
	  case 0xBB:	// OTDR
	    cycles = OUTX(-1);
	    if (_reg.B) { _PC -= 2; cycles += 5; }
	    break;

	  case 0x4D:	// RETI
	  case 0x45:	// RETN
	    _PC = (load(_SP+1)<<8) | load(_SP);
	    _SP+=2;
	    _IFF1 = _IFF2;
	    cycles = 14;
	    break;
	  default:
	    if (_log) {
	      fprintf(_log, "Illegal ED instruction: %02X\n", _IR);
	      fflush(_log);
	    }
	    break;
	}
	break;

      case 0xFD:	// FD instruction set:
	_IR = load(_PC++);
	if (_log) pinstr(_PC-2, 0xFD, 0, FD_instr[_IR]);

	switch(_IR) {
	  case 0x36:	// LD (IY+o),n
	    o = immediate_value();
	    store(_reg.IY+(int8_t)o, immediate_value());
	    cycles = 19;
	    break;
	  case 0x70 ... 0x75:	// LD (IY+o),r
	  case 0x77:
	    store(_reg.IY+(int8_t)immediate_value(), reg8(_IR & 0x07));
	    cycles = 19;
	    break;
	  case 0x22:	// LD (nn),IY
	    addr = immediate_addr();
	    store(addr,   _reg.IYl);
	    store(addr+1, _reg.IYh);
	    cycles = 20;
	    break;
	  case 0x7E:	// LD A,(IY+o)
	    _reg.A = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x46:	// LD B,(IY+o)
	    _reg.B = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x4E:	// LD C,(IY+o)
	    _reg.C = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x56:	// LD D,(IY+o)
	    _reg.D = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x5E:	// LD E,(IY+o)
	    _reg.E = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x66:	// LD H,(IY+o)
	    _reg.H = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x6E:	// LD L,(IY+o)
	    _reg.L = load(_reg.IY + (int8_t)immediate_value());
	    cycles = 19;
	    break;
	  case 0x2A:	// LD IY,(nn)
	    addr = immediate_addr();
	    _reg.IY = load(addr) | (load(addr+1)<<8);
	    cycles = 20;
	    break;
	  case 0x21:	// LD IY,nn
	    _reg.IY = immediate_addr();
	    cycles = 14;
	    break;
	  case 0xF9:	// LD SP,IY
	    _SP = _reg.IY;
	    cycles = 10;
	    break;
	  case 0xE1:	// POP IY
	    _reg.IYl = load(_SP++);
	    _reg.IYh = load(_SP++);
	    cycles = 14;
	    break;
	  case 0xE5:	// PUSH IY
	    store(--_SP, _reg.IYh);
	    store(--_SP, _reg.IYl);
	    cycles = 15;
	    break;
	  case 0xE3:	// EX (SP),IY
	    l = load(_SP);   store(_SP,   _reg.IYl);
	    h = load(_SP+1); store(_SP+1, _reg.IYh);
	    _reg.IY = (h<<8) | l;
	    cycles = 23;
	    break;
	  case 0x86:	// ADD A,(IY+o)
	    ADD(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x8E:	// ADC A,(IY+o)
	    ADC(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x96:	// SUB (IY+o)
	    SUB(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x9E:	// SBC A,(IY+o)
	    SBC(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xA6:	// AND (IY+o)
	    AND(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xAE:	// XOR (IY+o)
	    XOR(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xB6:	// OR (IY+o)
	    OR(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0xBE:	// CP (IY+o)
	    CP(load(_reg.IY+(int8_t)immediate_value()));
	    cycles = 19;
	    break;
	  case 0x34:	// INC (IY+o)
	    addr = _reg.IY+(int8_t)immediate_value();
	    store(addr, INC(load(addr)));
	    cycles = 23;
	    break;
	  case 0x35:	// DEC (IY+o)
	    addr = _reg.IY+(int8_t)immediate_value();
	    store(addr, DEC(load(addr)));
	    cycles = 23;
	    break;
	  case 0x09:	// ADD IY, BC
	    _reg.IY = ADD16(_reg.IY, _reg.BC); cycles = 15; break;
	  case 0x19:	// ADD IY, DE
	    _reg.IY = ADD16(_reg.IY, _reg.DE); cycles = 15; break;
	  case 0x29:	// ADD IY, IX
	    _reg.IY = ADD16(_reg.IY, _reg.IY); cycles = 15; break;
	  case 0x39:	// ADD IY, SP
	    _reg.IY = ADD16(_reg.IY, _SP);     cycles = 15; break;

	  case 0x23:	// INC IY
	    _reg.IY++; cycles = 10; break;
	  case 0x2B:	// DEC IY
	    _reg.IY--; cycles = 10; break;

	  case 0xE9:	// JP (IY)
	    _PC = _reg.IY; cycles = 8; break;

	  case 0xCB:
	    o = immediate_value();
	    _IR = load(_PC++);
	    if (_log) pinstr(_PC-5, 0xFDCB, o, FDCB_instr[_IR]);

	    switch(_IR) {
	      case 0x06:	// RLC (IY+o)
		store(_reg.IY+(int8_t)o, RLC(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x0E:	// RRC (IY+o)
		store(_reg.IY+(int8_t)o, RRC(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x16:	// RL (IY+o)
		store(_reg.IY+(int8_t)o, RL(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x1E:	// RR (IY+o)
		store(_reg.IY+(int8_t)o, RR(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x26:	// SLA (IY+o)
		store(_reg.IY+(int8_t)o, SLA(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x2E:	// SRA (IY+o)
		store(_reg.IY+(int8_t)o, SRA(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x36:	// SLL (IY+o)
		store(_reg.IY+(int8_t)o, SLL(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x3E:	// SRL (IY+o)
		store(_reg.IY+(int8_t)o, SRL(load(_reg.IY+(int8_t)o))); cycles = 23; break;
	      case 0x46: case 0x4E: case 0x56: case 0x5E: // BIT b,(IY+o)
	      case 0x66: case 0x6E: case 0x76: case 0x7E:
		BIT((_IR>>3)&0x07, load(_reg.IY+(int8_t)o)); cycles = 20;
		// _reg.F = (_reg.F&~F_XY) | (((_reg.IY+((int8_t)o))>>8)&F_XY);
		_reg.F &= ~F_XY;
		break;
	      case 0xC6: case 0xCE: case 0xD6: case 0xDE: // SET b,(IY+o)
	      case 0xE6: case 0xEE: case 0xF6: case 0xFE:
		store(_reg.IY+(int8_t)o, load(_reg.IY+(int8_t)o) | (1<<((_IR>>3)&0x07)));
		cycles = 23;
		break;
	      case 0x86: case 0x8E: case 0x96: case 0x9E: // RES b,(IX+o)
	      case 0xA6: case 0xAE: case 0xB6: case 0xBE:
		store(_reg.IY+(int8_t)o, load(_reg.IY+(int8_t)o) & ~(1<<((_IR>>3)&0x07)));
		cycles = 23;
		break;
	      default:
		if (_log) {
		  fprintf(_log, "Illegal FDCB instruction: %02X\n", _IR);
		  fflush(_log);
		}
		break;
	    }
	    break;
	  default:
	    /* Undocumented:  DD/FD are NOPs that set internal flag */
	    /* If next instruction ignores flag, it's just a NOP */
	    _PC --;
	    cycles = 4;
	    if (_log) {
	      fprintf(_log, "Illegal FD instruction: %02X\n", _IR);
	      fflush(_log);
	    }
	    break;
	}
	break;
	default:
	  if (_log) {
	    fprintf(_log, "Illegal instruction: %02X\n", _IR);
	    fflush(_log);
	  }
	  break;
    }

    clocks += cycles;
    _totclk += cycles;
#define PROCESS_RATE 1000

    if (!_batchmode && (clocks > (CLKRATE+(PROCESS_RATE/2))/PROCESS_RATE)) {
      // These are the only user-initiated events processed here:
      //  - quit
      //  - reset
      pthread_testcancel();
      if(_doreset) {
	_doreset = false;
	break;
      }
      aqsnd_process();

      clocks -= (CLKRATE+(PROCESS_RATE/2))/PROCESS_RATE;
      gettimeofday(&t, NULL);
      cur = t.tv_sec*1000000+t.tv_usec;
      long diff = 1000000/PROCESS_RATE - (cur - prev);
      if(_turbo) {
	prev = cur;
	if (t.tv_sec > turbo_sec) {
	  turbo_sec = t.tv_sec;
	  turbo_rate = (_totclk - turbo_clks) / (float)CLKRATE;
	  win_title("Aquarius");
	  turbo_clks = _totclk;
	}
      } else if(diff < -20000) { /* don't fall behind more than 20ms */
	fprintf(stderr, "lost %ldus\n", -diff);
	prev = cur;
      } else
	prev = cur + diff;
      if (!_turbo && diff > 0)
	usleep(diff);
    }

    if (_keymode == TIMER && _keydown > 0) _keydown -= cycles;
//     if (_log) {
//       int i;
//       fprintf(_log, "\tPC: %04X SP:1%02X A:%d IX:%d IY:%d F:%s\n",_PC, _SP, _reg.A, _reg.IX, _reg.IY, status(_reg.F));
//       for(i=0;i<_widx;i++) {
// 	fprintf(_log,"  %04X -> %02X/%d\n", _wlog[i].addr, _wlog[i].data, _wlog[i].data);
//       }
//     }
    _widx = 0;

    if (halted) {
      //FIXME: Wait for an interrupt
      // Then again, no known devices on Aquarius generate interrupts
      puts("HALTed");
      quit();
    }
  }
}

void reset()
{
  do {
    initmem();
    io_init();
    _PC = 0x0000;
    _SP = 0xFFFF;
    _reg.AF = 0xFFFF;
    _reg.BC = _reg.DE = _reg.HL = 0x0000;
    _alt.AF = 0xFFFF;
    _alt.BC = _alt.DE = _alt.HL = 0x0000;
    _reg.IX = _reg.IY = 0;
//    _IX = _IY = 0;
    _IFF1 = _IFF2 = 0;
    _imode = 0;

    _I = _R = 0;

    run();
  } while (1);
}

