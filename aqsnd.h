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
/****
 * Sound driver for Mattel Aquarius.
 * See top comment blocks in aqsnd.c for detailed documentation.
 */
#ifndef AQSND_H
#define AQSND_H
#include <stdint.h>

void aqsnd_reset(void); // call @ startup & system reset
void aqsnd_write_fc(uint8_t val); // beeps and tapes (just bit 0)
uint8_t aqsnd_read_fc(void); // tapes
void aqsnd_write_f6(uint8_t val); // sound chip data
uint8_t aqsnd_read_f6(void);
void aqsnd_write_f7(uint8_t val); // sound chip addr (low 4 bits)
uint8_t aqsnd_read_f7(void);

// This needs to be called at least once per sound sample period (~10ms)
// It is automatically called if sound registers are updated
void aqsnd_process(void); // bring sound state up-to-date

// Even though you should do volume contol by other means, here's
// a simple internal volume control.  Set to amount to attenuate generated
// samples by (i.e. logarithmic vol control, 0 = max, 15 = off, 13 = min)
extern int _vol_shift;

#endif
