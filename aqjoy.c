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
#include <math.h>

/* Joystick ports, managed entirely via custom key events:
 *   J<n>_<ev> -> event <ev> for joystick <n>.
 *     <n> == 1/A/L or 2/B/R.
 *     <ev> == 1..6 for buttons
 *     <ev> == cardinal direction name for dial
 * Analog input axes are supported for the dial.  Normally, these
 * would be assigned to E (X) and N (Y).  On the other hand, if you
 * have a game that requires angled input, but don't like that, you
 * can just assign it to e.g. NE (X) and NW (Y).  Or whatever.
 */

bool _swap_joy = false; // swap J1/J2 mapping

/* Defauit config.  Override/supplement if you don't like it. */
/* Note: GP2 is mapped the same as GP1, but with J1 and J2 swapped */
static struct {
  actid_t code;
    // See gui_common.cpp 'J' parser for codes:
  const char *name;
} _mapping[] = {
  /* Joy 1: left stick */
  { JC_AX(1, 0),  "GP1_LeftX" },
  { JC_AX(1, 12), "GP1_LeftY" }, // inverted: + == south
  /* Right buttons in similar arrangement to real controller */
  { JC_BUT(1, 1), "GP1_X" },
  { JC_BUT(1, 2), "GP1_Y" },
  { JC_BUT(1, 3), "GP1_RightShoulder" },
  { JC_BUT(1, 4), "GP1_A" },
  { JC_BUT(1, 5), "GP1_B" },
  { JC_BUT(1, 6), "GP1_RightTrigger" },
  /* Joy 2: right stick */
  { JC_AX(2, 0),  "GP1_RightX" },
  { JC_AX(2, 12), "GP1_RightY" },
  /* Left buttons in similar arrangement to real controller */
  /* Well, technically, this only applies to aligned sticks, like the
   * Sony Dual Shock controllers, rather than the offset sticks, like the
   * XBox controllers, where the digital pad is below the left analog stick.
   * In that case, you'd have to swap stick and hat, which would be weird,
   * since you'd be using an analog stick as buttons and buttons as an
   * analog stick.  I think it works well enough in either case, since you'd
   * be pressing the shoulder/trigger with your index/middle finger and using
   * your thumb for the dpad regardless of location.
   */
  /* The fact that you can't chord the dpad isn't bad, either, because
   * the cheap-ass Aquarius controller barely even supports pressing a button
   * and the dial at the same time, much less N-key rollover.   Besides,
   * you can change the config, and you'd be better off using the left stick
   * mapping, instead.  The only reason the 2nd controller is on the same
   * gamepad at all is for "twin stick" type games, which generally don't
   * involve buttons, anyway (e.g. Robotron 2084, not that there is an
   * Aquarius port that I know of).
   */
  { JC_BUT(2, 1), "GP1_DPLeft" },
  { JC_BUT(2, 2), "GP1_DPUp" },
  { JC_BUT(2, 3), "GP1_LeftShoulder" },
  { JC_BUT(2, 4), "GP1_DPDown" },
  { JC_BUT(2, 5), "GP1_DPRight" },
  { JC_BUT(2, 6), "GP1_LeftTrigger" },

  // Since the cursor keys and many other specials aren't mapped, why
  // not map them to the joystick by default?
  { JC_AX(1, 0),  "Right" },
  { JC_AX(1, 8),  "Left" },
  { JC_AX(1, 4),  "Up" },
  { JC_AX(1, 12), "Down" },
  { JC_BUT(1, 1), "Insert" },
  { JC_BUT(1, 2), "Home" },
  { JC_BUT(1, 3), "PageUp" },
  { JC_BUT(1, 4), "Delete" },
  { JC_BUT(1, 5), "End" },
  { JC_BUT(1, 6), "PageDown" },
  // Same goes for the numeric keypad.  This is a non-traditional mapping.
  { JC_AX(1, 0),  "Keypad 3" },
  { JC_AX(1, 2),  "Keypad 6" },
  { JC_AX(1, 4),  "Keypad 5" },
  { JC_AX(1, 6),  "Keypad 4" },
  { JC_AX(1, 8),  "Keypad 1" },
  { JC_AX(1, 10), "Keypad 0" },
  { JC_AX(1, 12), "Keypad 2" },
  { JC_AX(1, 14), "Keypad ." },
  // buttons are offset right by one to avoid NumLock
  // NumLock afffects the keypad behavior, and only returns a keypress event
  // half the time.
#if 0
  { JC_BUT(1, 1), "NumLock" },
  { JC_BUT(1, 2), "Keypad /" },
  { JC_BUT(1, 3), "Keypad *" },
  { JC_BUT(1, 4), "Keypad 7" },
  { JC_BUT(1, 5), "Keypad 8" },
  { JC_BUT(1, 6), "Keypad 9" }
#else
  { JC_BUT(1, 1), "Keypad /" },
  { JC_BUT(1, 2), "Keypad *" },
  { JC_BUT(1, 3), "Keypad -" },
  { JC_BUT(1, 4), "Keypad 8" },
  { JC_BUT(1, 5), "Keypad 9" },
  { JC_BUT(1, 6), "Keypad +" }
#endif
};

static bool _bpressed[2][6], _axpressed[2][16];
static uint8_t _axval[2][16];
// dial and button stored separately for independent computation
static uint8_t _dialmask[2], _bmask[2];

void aqjoy_but(int joyno, int but, bool press)
{
  const uint8_t bmask[] = {
    0xbf, 0x7b, 0x5f, 0xdf, 0x7d, 0x7e
  };
  if(press) {
    _bpressed[joyno][but] = true;
    _bmask[joyno] &= bmask[but];
    return;
  }
  if(!_bpressed[joyno][but])
    return;
  _bpressed[joyno][but] = false;
  // Need to recompute mask if unpressed
  uint8_t m = 0xff;
  for(int i = 0; i < 6; i++)
    if(_bpressed[joyno][i])
      m &= _bmask[i];
  _bmask[joyno] = m;
}

void aqjoy_dial(int joyno, int dir, uint8_t val)
{
  if(val == 0x80) {
    if(!_axpressed[joyno][dir])
      return;
    // FIXME: I don't track values by source, so this does not
    // properly revert to the most recent still-pressed source.
    // Instead, it just retains the current value until all sources released.
    // Most people won't be using multiple inputs simultaneously, anyway,
    // so it's not a big deal.
    _axpressed[joyno][dir] = false;
  } else {
    _axpressed[joyno][dir] = true;
    _axval[joyno][dir] = val;
  }
  // Update value
  // Sum up vectors of given magnitude and direction
  double x = 0, y = 0;
  bool didone = false;
  for(dir = 0; dir < 16; dir++) {
    int val = (int)_axval[joyno][dir] - 0x80;
    if(!_axpressed[joyno][dir] || !val)
      continue;
    // At least one input must be >= 50% for activation.
    if(val >= 0x40 || val <= -0x40)
      didone = true;
    // Convert to rectangular, and add.
    // My memory is hazy:  is there a better way to do this?
    double angle = 2 * M_PI / 16 * (16 - dir); // dir is cw, not ccw, so invert
    x += val * cos(angle);
    y += val * sin(angle);
  }
  // At least one input must be active
  if(!didone) {
    _dialmask[joyno] = 0xff;
    return;
  }
  // Convert to polar for actual r/dir
  /* y is negative for up, so it needs inversion, but axmap goes south
   * first, so it needs inversion as well, and they cancel out */
  /* atan(y/x) == -pi/2 .. pi/2 -> -4 .. 4 */
  double th = x ? atan(y/x) / M_PI * 8.0 : y > 0 ? -4.0 : 4.0;
  /* correction for quadrant 2-3 */
  if(x < 0)
    th += 8.0; // -4 .. 12
  dir = (int)round(th) & 15;
  // if(_ilog) fprintf(_ilog, "stick x=%d, y=%d angle=%d\n", x, y, dir);
  /* from https://www.vdsteenoven.com/aquarius/hndcntrl.html */
  const uint8_t axmap[] = { /* E ESE .... ENE */
    0xfd, 0xed, 0xec, 0xfc, 0xfe, 0xee, 0xe6, 0xf6,
    0xf7, 0xe7, 0xe3, 0xf3, 0xfb, 0xeb, 0xe9, 0xf9
  };
  _dialmask[joyno] = axmap[dir];
}

uint8_t aqjoy_read(uint8_t port)
{
  if(port < 14 || port > 15)
    return 0;
  port -= 14;
  if(_swap_joy)
    port = 1 - port;
  return _bmask[port] & _dialmask[port];
}

void aqjoy_init()
{
  char gbuf[20]; // 17 is enough
  for(unsigned i = 0; i < 16; i++) {
    strcpy(gbuf, _mapping[i].name);
    actid_t code = _mapping[i].code;
    bind_key(gbuf, code);
    code ^= 0x100; // joystick #
    gbuf[2] = '2';
    bind_key(gbuf, code);
  }
  for(unsigned i = 16; i < sizeof(_mapping)/sizeof(_mapping[0]); i++)
    bind_key(_mapping[i].name, _mapping[i].code);
  aqjoy_reset();
}

void aqjoy_reset()
{
  for(int i = 0; i < 2; i++) {
    memset(_axpressed[i], 0, sizeof(_axpressed[i]));
    memset(_bpressed[i], 0, sizeof(_bpressed[i]));
    _dialmask[i] = _bmask[i] = 0xff;
  }
}
