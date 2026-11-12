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

#ifndef AQSND_H
#define AQSND_H
#include <stdint.h>

/* Note:  this does not handle controllers */
/* For controllers, write_f7 saves written values and read_f7 just returns */
/* saved values.  For convenience, ports are initialized to 0xff (no press) */
/*  R7, b7 = DDR B; b5 = DDR A (0 = in, 1 = out) */
/*  R14 = Port A data; R15 = Port B data */
/* I suggest that after aqsnd_{read,write}_f6, you call an equivalent
 * version for just the controller bits if read_f7 returns 14 or 15,
 * and just assume the DDR bits are always 0 */

void aqsnd_reset(void); // call @ startup
void aqsnd_write_fc(uint8_t val); // beeps and tapes
uint8_t aqsnd_read_fc(void); // tapes
void auto_end_tape(void);  // unset _is_tape if possible
void aqsnd_write_f6(uint8_t val); // sound chip data
uint8_t aqsnd_read_f6(void);
void aqsnd_write_f7(uint8_t val); // sound chip addr
uint8_t aqsnd_read_f7(void);

extern int _is_tape; // true if currently reading/writing a tape (may turbo)

#define write_reg(r, v) \
    aqsnd_write_f7(r); \
    aqsnd_write_f6((uint8_t)(uint16_t)(v))
#define write_reg16(r, v) \
    write_reg(r, v); \
    write_reg(r+1, (uint16_t)(v)>>8)

#endif
