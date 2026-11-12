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
#ifndef _SDLEVENT_H
#define _SDLEVENT_H

#include <stdbool.h>
#include <SDL2/SDL.h>

// Action is a signed integer.  The returned sign indicates "pressed" (positive)
// or "released" (negative).  0 means no action, so don't use it.
// When assigning an action, negate the actual desired code for axis mode.
// In axis mode, the lower 8 bits of the ID are filled in with axis strength,
// which is a uint8_t centered around 0x80; the events are sent as "pressed"
// whenever it is not centered, and "released" when centered.
typedef int32_t actid_t;

// Process 1 SDL event, or, if NULL, process gamepads
bool sdl_event(const SDL_Event *event, void (*f)(actid_t act, Uint16 mod));

// Capture an SDL "press".  In other words, wait until a key is pressed,
// capture all key presses, and return when all of the captured keys are
// released.  Axes are considered "pressed" at 25%, but a future addition
// might distinguish using other cutoff points for multiple positions
// Abort with ESC pressed twice or RMB.  Force accept with LMB.
// Returns binding string; free when done.
char *capture_sdl_events(void);
extern bool _capturing;

// Clear all key bindings
void clear_keys(void);

// Parse key spec and bind to act.
// Returns false if key can't be parsed.
// act_id == 0 removes a mapping
bool bind_key(const char *key, actid_t act);

// Find all keys bound to act.  Multiple bindings are separated by ;
// Free when done.
char *bound_keys(actid_t act);

// True if any keys bound to act.
bool is_bound(actid_t act);

// Sort and case-adjust a key spec so two identical specs compare equal
// Returns NULL if there is an error parsing key.
// Free result when done.
char *canonicalize_key(const char *key);

// Given a key spec, find if it would perform an action
// Set err to non-NULL to detect errors in key
actid_t key_binding(const char *key, bool *err);

// (Re)assign system controller index.  Note that scanning only happens once,
// but this function can re-apply the controller-to-index mapping
#include "json.h"
void map_controllers(const array_t *map);
#endif
