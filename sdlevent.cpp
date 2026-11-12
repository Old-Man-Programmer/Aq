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
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <algorithm>
#include <functional>
#include <regex>
extern "C" {
#include "sdlevent.h"

// FIXME: this is an Aq dependency
#include <stdio.h>
extern FILE *_ilog;
}

/*
 * SDL2 Key/gamepad event processor with remapping support
 *
 * Note:  Gamepad hot plugging is not supported.  Sorry about that if
 * you have a flaky connectino (e.g. some BT devices disconnect
 * occasionally, and Linux is poorly equiped to handle that).  It only
 * checks for/opens the devices once, on startup.
 *
 * Missing features I might like:
 * - state flags, for use as pseudo-keys.  Makes the mapping modal:  any
 *   key chorded with e.g. state0 will only be active when state bit 0 is
 *   set (by the program; maybe/probably via a key binding)
 * - "or".  The only "or" supported is the LR modifier hack.  It would
 *   be better to e.g. have a way of saying "this key is equivalent to that",
 *   for a similar effect in chords.
 * - a single, quick "ignore all modifiers"
 * - More refinement in strength selection for ax->key (not just +/-)
 *   For example, press d if half, f if full.
 * - 2-dimensional inputs with direction select.
 *   (LeftX, LeftY)[str-range][angle-range].  For example, press
 *   numeric 123 4 6 789 based on angle.  Sort of possible with
 *   chording, although you will likely get spurious non-angle
 *   events.
 * - hot reattachment of gamepads (just retry every so often)
 *   - for testing, I'd really need at least 2 real identical controllers,
 *     so I can test behavior wrt specific drivers.
 * - hot plugging of gamepads (requires re-parsing all GP specs @[un]plug)
 * - joysticks (SDL_joystick.h) (more buttons, more axes)
 * - mouse events (at least buttons), gestures
 * - touchpad presses (SDL_touch.h), gestures
 * - HIDAPI devices (SDL_hidapi.h)
 * - a way to query what gamepads are mapped, for e.g. LED or vibration
 */

using namespace std;

// The key is a modifier plus either a key, button, or axis.
enum keytype_t { KEYT_SCANCODE, KEYT_BUTTON, KEYT_AXIS };
struct keydesc_t {
  keytype_t type : 2 /* = KEYT_SCANCODE */; // needs C++-20 for bitfield init
  Uint16 mod : 14 /* = 0 */; // ignore upper 4 bits for now */
  uint16_t code = SDL_SCANCODE_UNKNOWN;
  // SDL_Scancode : 9 bits (type is 10 due to MAX entry)
  // SDL_GameController_Button : 5 bits (type is 6 due to -1 entry)
  // SDL_GameController_Axis : 3 bits (+1 for direction) (type is 4 due to -1 entry)
     // GC: rest is index into controller array
     // Just <<8, so up to 8 bits
     // You'd have to be insane to have more than 16
     // Linux only supports 32 max total input devices
//#define MAX_GP 255
     // In any case, I set a 64-bit int's bits by GP#, so:
#define MAX_GP 63
};

/***** Key descriptor parsing ******/

// these are the only supported mods:
#define KMOD_ALL (KMOD_SHIFT|KMOD_CTRL|KMOD_ALT|KMOD_GUI)
static Uint16 parse_mod(const char *s, unsigned len)
{
  // FIXME: Add Super, Meta aliases (Super==LGUI, Meta==RGUI?)
  const struct {
    const char *name;
    Uint16 mod;
  } mods[] = {
    { "ALT",    KMOD_ALT },
//  { "CAPS",   KMOD_CAPS },
    { "CMD",    KMOD_GUI },
    { "CTRL",   KMOD_CTRL },
    { "GUI",    KMOD_GUI },
    { "LALT",   KMOD_LALT },
    { "LCMD",   KMOD_LGUI },
    { "LCTRL",  KMOD_LCTRL },
    { "LGUI",   KMOD_LGUI },
    { "LOPT",    KMOD_LALT },
    { "LSHIFT", KMOD_LSHIFT },
    { "LWIN",   KMOD_LGUI },
//  { "MODE",   KMOD_MODE },
//  { "NUM",    KMOD_NUM },
    { "OPT",    KMOD_ALT },
    { "RALT",   KMOD_RALT },
    { "RCMD",   KMOD_RGUI },
    { "RCTRL",  KMOD_RCTRL },
    { "RGUI",   KMOD_RGUI },
    { "ROPT",   KMOD_RALT },
    { "RSHIFT", KMOD_RSHIFT },
    { "RWIN",   KMOD_RGUI },
    { "SCROLL", KMOD_SCROLL },
    { "SHIFT",  KMOD_SHIFT },
    { "WIN",    KMOD_GUI }
  };
  int l = 0, h = sizeof(mods)/sizeof(mods[0]) - 1;
  while(l <= h) {
    int m = (l + h) / 2;
    int c = strncasecmp(s, mods[m].name, len);
    if(!c)
      return mods[m].mod;
    if(c > 0)
      l = m + 1;
    else
      h = m - 1;
  }
  // this message will turn into "unkown key", so suppress.
  // it might actually be a -/+ key.
//  if(_ilog)
//    fprintf(_ilog, "Unknown mod %.*s\n", len, s);
  return 0;
}

// name of lowest mod bit; also clear that mod bit so you can just loop until 0
// if it's both L and R, strip off L/R and 2 bits
static const char *modbit_to_str(Uint16 *_mod)
{
  const char * const mod_names[] = {
    "LSHIFT", "RSHIFT", "LCTRL", "RCTRL", "LALT", "RALT", "LGUI", "RGUI"
  };
  Uint16 mod = *_mod;
  Uint16 m = 1;
  for(int i = 0; i < 8; i++, m <<= 1) {
    if(m == 0x04)
      m <<= 4;
    if(mod & m) {
      if(!(i & 1) && (mod & (m<<1))) {
	*_mod &= ~(m + (m<<1));
	return mod_names[i] + 1;
      }
      *_mod &= ~m;
      return mod_names[i];
    }
  }
  return NULL;
}

static vector<keydesc_t> parse_key(const char *k, Uint16 &ign_mod)
{
  const char *ok = k; // save whole string for error messages
  keydesc_t key { .type = KEYT_SCANCODE, .mod = 0 };
  ign_mod = 0;
  if(!k || !*k)
    // This should never happen, really, so no need for a warning
    return {};
  // Find/parse mods ([!]<mod>+ prefixes)
  while(1) {
    const char *pl = k;
    while(*++pl != '+' && *pl != '-' && *pl);
    if(!*pl)
      break;
    bool is_ign;
    if((is_ign = *k == '!'))
      k++;
    Uint16 m = parse_mod(k, pl - k);
    if(!m)
      break;
    if(is_ign)
      ign_mod |= m;
    else
      key.mod |= m;
    k = pl + 1;
  }
  /* Verify no ignored mods are also required */
  if(key.mod & ign_mod) {
    if(_ilog)
      fprintf(_ilog, "'%s': can't ignore and require a mod at the same time\n", ok);
    return {};
  }
  /* Finally, a comma-separated list of key or gamepad event names */
  /* Might be more efficient to add to table immediately, but instead
   * accumulate into an array of keys, so an error adds nothing to table */
  /* Note that gamepads match multiple keys, each of which generates a
   * unique binding.  This could get quite expensive. */
  vector<keydesc_t> keys;
  /* Since SDL requires 0-terminated strings, I'll copy into a buffer */
  /* I guess I could use std::string instead, but I don't feel like it. */
  /* I guess I could use malloc instead of stack, but I don't feel like it. */
  unsigned klen = strlen(k);
  char kbuf[klen + 1];
  memcpy(kbuf, k, klen + 1);
  char *ck = kbuf;
  while(1) {
    if(!*ck) {
      if(_ilog)
	fprintf(_ilog, "'%s': can't have blank key\n", ok);
      return {};
    }
    char *comma = strchr(ck, ',');
    // There are no non-key names with a , in the name.
    // The following checks if it's guaranteed to be part of the key name if
    // the key is valid at all (i.e., it's blank w/o the comma, or the next
    // key would be blank if the comma were a separator).
    // This takes care of leading , (",") and trailing , ("Keypad ,") at end
    while(comma == ck || (comma && !comma[1]))
      comma = strchr(comma + 1, ',');
    if(comma)
      *comma = 0;
    SDL_Scancode code = SDL_GetScancodeFromName(ck);
    if(code == SDL_SCANCODE_UNKNOWN &&
       (toupper(*ck) != 'G' || toupper(ck[1]) != 'P')) {
      // There is really only one other valid case that could happen at
      // the time of writing:  "Keypad ," followed by another key.  However,
      // in the interest of future-proofing, loop over all possible end
      // points until either a match is made or end of string.
      // I'm betting that SDL uses a linear search, in which case this will
      // probably take a long time.  Blame the SDL devs if this bothers you.
      // Or, since order doesn't matter, place keys with comma in the name
      // at the end, and don't misspell them.
      while(comma) {
	*comma = ',';
	comma = strchr(comma + 1, ',');
	if(comma)
	  *comma = 0;
	code = SDL_GetScancodeFromName(ck);
	if(code != SDL_SCANCODE_UNKNOWN)
	  break;
      }
      if(code == SDL_SCANCODE_UNKNOWN) {
	if(_ilog)
	  fprintf(_ilog, "'%s': unrecognized key name '%s'\n", ok, ck);
	return {};
      }
    }
    if(code != SDL_SCANCODE_UNKNOWN) {
      key.type = KEYT_SCANCODE;
      key.code = code;
      keys.push_back(key);
      if(comma) {
	ck = comma + 1;
	continue;
      } else
	return keys;
    }
    // At this point, "GP" is definitely there.  No need to check 1st digit,
    // because strtoul() will return 0 if not a digit.  0 is invalid, anyway.
    unsigned gpno;
    char *e;
    if(!(gpno = strtoul(ck + 2, &e, 10)) || gpno > MAX_GP || *e != '_') {
      if(_ilog) {
	if(gpno)
	  fprintf(_ilog, "'%s': expected '_' '%s'\n", ok, ck);
	else if(gpno > MAX_GP)
	  fprintf(_ilog, "'%s': gamepad index out of range '%s'\n", ok, ck);
	else
	  fprintf(_ilog, "'%s': expected gamepand index '%s'\n", ok, ck);
      }
      return {};
    }
    // Let SDL parse the button/axis name.
    // If comma is non-NULL, *comma == 0, so name is already terminated
    e++;
    int ev;
    // SDL_CONTROLLER_{AXIS,BUTTON}_INVALID is -1.
    // SDL docs are wrong:  docs say failed Button parse is AXIS_INVALID.
    // Bite me, SDL, with your insanely long names.  At least shorten to SDL_GC_.
    // I'll just test for negative, instead.
    if((ev = SDL_GameControllerGetButtonFromString(e)) >= 0) {
      key.type = KEYT_BUTTON;
      key.code = ev;
    } else {
      bool is_neg = *e == '-', is_pos = *e == '+';
      if((ev = SDL_GameControllerGetAxisFromString(e + (is_neg || is_pos ? 1 : 0))) >= 0) {
	key.type = KEYT_AXIS;
	key.code = ev;
	if(is_neg)
	  key.code |= 0x80;
      } else {
	if(_ilog)
	  fprintf(_ilog, "'%s': invalid button/axis name '%s'\n", ok, e);
	return {};
      }
    }
    key.code |= (gpno - 1) << 8;
    keys.push_back(key);
    if(!comma)
      break;
    ck = comma + 1;
  }
  return keys;
}

/********* Mapping lookup table *********/

// Hash table value
struct mapval_t {
  uint32_t *groups = 0; // All chording groups this belongs to
     // 0-terminated group ID list (or NULL if list is empty)
     // maybe I'll end up changing this to std::vector<uint32_t>
  actid_t act = 0;      // Action if hit, regardless of groups it belongs to
     // 0 = no action (i.e., this key only does chording)
     // <0 = "axis" value; negated for actual value and axis in lower 8
     // Actual return value has sign based on up/down
  Uint16 ign_mod = 0;   // Ignored modifiers wrt act (stored separately for groups)
                        // Upper 4 bits correspond to mod pairs for virtual entries
};

// "hash" needs to return a size_t, so why not just use raw value?
// raw value used to be sizeof(size_t), but I removed ign_mods and reduced
// code size to 14 bits
static_assert(sizeof(keydesc_t) == sizeof(uint32_t),
	      "Rewrite me!  keydesc_t must fit in size_t.");
// However, lookups are based on keyboard state, so ign_mod is ignored.
template <> struct std::hash<keydesc_t> {
  size_t operator()(const keydesc_t& k) const {
#define KEY_INT(k) (*(uint32_t *)&k)
    return KEY_INT(k);
  }
};
// Same for hash table equality operator
static bool operator==(const keydesc_t &a, const keydesc_t &b)
{
  return a.code == b.code && a.type == b.type && a.mod == b.mod;
};

// The single most important data structure in this file...
static unordered_map<keydesc_t, mapval_t> _keymap;

// A group ID is an index into the group-related arrays, plus 1 so 0 can
// be "none".

// The list of keys in each group, for easy lookup and key count
// The size of group_keys is the next ID to assign
// Each subarray sorted using the "<" operator below
static vector<vector<keydesc_t>> _group_keys;
int operator<(const keydesc_t &a, const keydesc_t &b) {
#if 0 // Sorted by key code only, since they all share the same mod bits.
  if(a.mod != b.mod)
    return a.mod < b.mod;
#endif
  if(a.type != b.type)
    return a.type < b.type;
  return a.code < b.code;
}
// The group's assigned action (equivalent to act)
static vector<actid_t> _group_act;
// The group's ignored modifiers (modifiers are part of gropu_keys[i][0])
static vector<Uint16> _group_ign_mod;

// Now, to deal with multiple modifier possibilities.  I used to iterate
// over all possibilities in the lookup function (*2 searches per pressed
// modifier; *16 max), but with the addition of ign_mod, that is much more
// expensive, although since I removed it from the key, it's not *256 at least.
// Best to just add all possibilities to the table, though.  The main
// decision is whether or not to use mod in the key any more. 
// If it gets removed, the table value becomes an array of (mod,ign_mod,val)
// tuples to scan linearly.  Simple, efficient storage, and not likely to
// blow up since users aren't likely to auto-add thousands of combinations.
// For now, I'm leaving it in, though, because it's likely a bit faster,
// because the extra mod array scan is eliminated.
//
// For each L/R mod pair:
//   If both are specified in mod, add an L and R entry. (*3)
//     "primary" is with all mod set (finding it is easy due to:)
//     "extra" entries are flagged using upper 4 bits of ign_mod
//     if all 4 mod pairs are paired, this means 3^4 = 81 entries
//   If one is specified in ign_mod, add with and without (*2)
//     "primary" is with all ign_mod bits unset in mod (finding it is easy)
//     if all 8 mods are ignored, this means 2^8 = 256 entries
// The above description applies to non-chords.  Chords are technically all
// virtual entries, so no extra flags are needed.  The group's mod bits are
// stored in _group_keys and _group_ign_mod rather than the hash entry
// itself.  However, the multipliers still apply.  For example, a 4-key
//  chord ignoring all mods will add 4*256 = 1024 entries to the table.

// Given a key or a chording group, either add (act!=0) or delete (!act),
// iterating through all mod possiblities.  If key overlaps another multi-
// binding, that *entire* binding will silently be deleted as well.
// Note: set key to _group_keys[g][0].mod and ign_mod to _group_ign_mod[g]
// when iterating a group.  This function assumes key is correct to begin
// with for iterations, although it does take special consideration when
// adding/deleting if group_id set.
// FIXME: add a mode to return list of potentially auto-deleted overlaps
static void multi_key(keydesc_t key, Uint16 ign_mod, uint32_t group_id,
		      actid_t act,
		      // recursion state; do not touch
		      Uint16 _ign_mod = KMOD_ALL, Uint16 _dup_mod = 0)
{
  // Recursively iterate all ign_mod possibiities of "key"
  // Max recurse depth == 8
  if(ign_mod & KMOD_ALL & _ign_mod) {
    Uint16 ign = ign_mod & _ign_mod, m;
    for(m = 1; !(ign & m); m <<= 1);
    _ign_mod &= ~m;
    keydesc_t tmp = key;
    tmp.mod |= m;
    multi_key(tmp, ign_mod, group_id, act, _ign_mod);
    // Could use tail recursion/loop for this.  Maybe optimized by compiler.
    multi_key(key, ign_mod, group_id, act, _ign_mod);
    return;
  }
  // Recursively iterate all mod possibilities of "key"
  // Max recurse depth == 4; mutually exclusive w/ above, so
  // actual max == 8, rather than 8 + 4 = 12
  for(Uint16 m = 3, b = 1, i = 0, p = 1 << 12; i < 4; i++, m <<= 2, b <<= 2, p <<= 1) {
    if(m == 0x0c) {
      m <<= 4;
      b <<= 4;
    }
    if((key.mod & m) == m && !(_dup_mod & m)) {
      keydesc_t tmp = key;
      _dup_mod |= m;
      tmp.mod &= ~b; // R
      // p is "extra" entry flag
      multi_key(tmp, ign_mod | p, group_id, act, _ign_mod, _dup_mod);
      tmp.mod ^= m; // L
      multi_key(tmp, ign_mod | p, group_id, act, _ign_mod, _dup_mod);
      // No need to rely on tail recursion, since we're in a loop
      // just loop some more
      // tmp.mod |= m;
      // tmp.ign_mod &= ~p; // doesn't really matter, actually
      // multi_key(tmp, ign_mod | p, group_id, act, _ign_mod, _dup_mod);
      // return;
    }
  }

  auto o = _keymap.find(key);
  if(o != _keymap.end()) {
    // auto-delete if incomplete overlap (ignore/overwrite otherwise)
    // incomplete if extra ign_mod bits or extra pairs
    if(!group_id && o->second.act) {
      bool clear = !(o->second.ign_mod & ~ign_mod);
      for(Uint16 m = 3, p = 1 << 12, i = 0; clear && i < 4; i++, m <<= 2, p <<= 1) {
	if(m == 0x0c)
	  m <<= 4;
	if(((o->first.mod & m) == m || (o->second.ign_mod & p)) &&
	   (key.mod & m) != m && !(ign_mod & p))
	  clear = false;
      }
      if(!clear) {
	bool willdel = !o->second.groups;
	multi_key(o->first, o->second.ign_mod, 0, 0);
	if(willdel)
	  o = _keymap.end();
      }
    } else if(group_id && o->second.groups) {
      uint32_t *&gr = o->second.groups;
      const vector<keydesc_t> &keys = _group_keys[group_id - 1];
      for(unsigned i = 0; gr[i]; i++) {
	const vector<keydesc_t> &okeys = _group_keys[gr[i] - 1];
	if(gr[i] == group_id || okeys.size() != keys.size())
	  continue;
	unsigned j;
	for(j = 0; j < keys.size(); j++)
	  if(keys[i].code != okeys[i].code)
	    break;
	if(j != keys.size())
	  continue;
	bool is_last = !gr[i + 1]; // in case this removes last group
	multi_key(okeys[0], _group_ign_mod[gr[i] - 1], gr[i], 0);
	if(is_last)
	  break;
	i -= 1; // deleted this group, so retry at this index
      }
    }
  }
  // Finally done with all the setup.  Primmary action follows.
  if(!act) {
    // delete [from] o
    if(o == _keymap.end())
      return;
    if(!group_id && o->second.groups) {
      o->second.act = 0;
      o->second.ign_mod = 0; // not really necessary
    } else if(!group_id)
      _keymap.erase(o);
    else if(o->second.groups) {
      uint32_t *&gr = o->second.groups;
      for(unsigned i = 0; gr[i]; i++)
	if(gr[i] == group_id) {
	  if(!gr[i + 1]) {
	    if(!i) {
	      free(gr);
	      gr = NULL;
	    } else
	      gr[i] = 0;
	  } else {
	    for(i++; gr[i]; i++)
	      gr[i - 1] = gr[i];
	    gr[i] = 0;
	  }
	  break;
	}
    }
  } else {
    // add [to o]
    if(o == _keymap.end())
      o =_keymap.emplace(key, mapval_t()).first;
    if(!group_id) {
      o->second.act = act;
      o->second.ign_mod = ign_mod;
    } else {
      uint32_t *&gr = o->second.groups;
      unsigned i = 0;
      if(!gr)
	gr = (uint32_t *)malloc(sizeof(*gr) * 2);
      else {
	for( ; gr[i]; i++)
	  if(!gr[i])
	    break;
	gr = (uint32_t *)realloc(gr, sizeof(*gr) * (i + 2));
      }
      if(!gr) {
	perror("keymap group");
	exit(1);
      }
      gr[i] = group_id;
      gr[i + 1] = 0;
    }
  }
}

// This is a bitmask of all added controller #s, buttons, and axes.
// It limits me to 64 max.
// Only pads, axes, and buttons in these masks are scanned.
// It is impractical to unset bits, since I would have to scan the entire
// mapping table first to see if somebody else uses it.
// It might be useful to split the button/axis masks to an array indexed by GP#
static uint64_t _buttons = 0, _axes = 0, _pads = 0;

static actid_t bind_key(vector<keydesc_t> &keys, Uint16 ign_mod, actid_t act, bool find = false)
{
  // or remove if action is 0
  // or just return first bound action if find is non-NULL
  if(keys.size() > 1) {
    // Groups are always sorted in key order
    sort(keys.begin(), keys.end());
    // See if group already exists (must be attached to 1st key at least)
    auto o = _keymap.find(keys[0]);
    uint32_t group_id = 0;
    if(o != _keymap.end() && o->second.groups)
      for(int grpno = 0; (group_id = o->second.groups[grpno]); grpno++)
	if(_group_keys[group_id - 1] == keys &&
	   _group_ign_mod[group_id - 1] == ign_mod)
	  break; // group_id has been found!
    if(find) // find group
      return group_id ? _group_act[group_id - 1] : 0;
    if(group_id && !act) {
      // Delete group
      multi_key(keys[0], ign_mod, group_id, 0);
      _group_keys[group_id - 1] = {};
      _group_ign_mod[group_id - 1] = 0; // not necessary
      // Free up space if no group above it
      unsigned i;
      for(i = group_id; i < _group_keys.size(); i++)
	if(_group_keys[i].size())
	  break;
      if(i == _group_keys.size()) {
	while(--group_id > 0)
	  if(_group_keys[group_id - 1].size())
	    break;
	_group_keys.resize(group_id);
	_group_act.resize(group_id);
      }
    } else if(!group_id && act) {
      // Create
      for(auto k: keys) {
	if(k.type == KEYT_AXIS)
	  _axes |= 1 << (k.code & 0x7f);
	else if(k.type == KEYT_BUTTON)
	  _buttons |= 1 << (k.code & 0xff);
	if(k.type == KEYT_AXIS || k.type == KEYT_BUTTON)
	  _pads |= 1 << (k.code >> 8);
      }
      _group_keys.push_back(keys);
      _group_act.push_back(act);
      _group_ign_mod.push_back(ign_mod);
      group_id = _group_keys.size();
      // Add to keys
      multi_key(keys[0], ign_mod, group_id, act);
    }
    // Maybe overwrite
    if(act)
      _group_act[group_id - 1] = act;
  } else if(!find) {
    // Add or delete single key
    multi_key(keys[0], ign_mod, 0, act);
    if(act) {
      if(keys[0].type == KEYT_AXIS)
	_axes |= 1 << (keys[0].code & 0x7f);
      else if(keys[0].type == KEYT_BUTTON)
	_buttons |= 1 << (keys[0].code & 0xff);
      if(keys[0].type == KEYT_AXIS || keys[0].type == KEYT_BUTTON)
	_pads |= 1 << (keys[0].code >> 8);
    }
  } else { // find single key
    auto o = _keymap.find(keys[0]);
    if(o != _keymap.end())
      return o->second.act;
  }
  return 0;
}

void clear_keys()
{
  _keymap.clear();
  _group_keys.clear();
  _group_act.clear();
  _group_ign_mod.clear();
}

bool bind_key(const char *key, actid_t act)
{
  Uint16 ign_mod;
  auto keys = parse_key(key, ign_mod);
  if(keys.empty())
    return false;
  bind_key(keys, ign_mod, act);
  return true;
}

actid_t key_binding(const char *key, bool *err)
{
  Uint16 ign_mod;
  auto keys = parse_key(key, ign_mod);
  if(err)
    *err = keys.empty();
  if(keys.empty())
    return 0;
  return bind_key(keys, ign_mod, 0, true);
}

static void add_mods(string &ret, keydesc_t k, Uint16 ign_mod)
{
  Uint16 m = k.mod;
  while(m) {
    ret += modbit_to_str(&m);
    ret += '+';
  }
  m = ign_mod & KMOD_ALL;
  while(m) {
    ret += '!';
    ret += modbit_to_str(&m);
    ret += '+';
  }
}

static void add_code(string &ret, keydesc_t k)
{
  if(k.type == KEYT_SCANCODE)
    ret += SDL_GetScancodeName((SDL_Scancode)k.code);
  else {
    ret += "GP";
    ret += to_string((k.code >> 8) + 1);
    ret += "_";
    if(k.type == KEYT_BUTTON)
      ret += SDL_GameControllerGetStringForButton((SDL_GameControllerButton)(k.code & 0x3f));
    else {
      if(k.code & 0x80)
	ret += '-';
      ret += SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)(k.code & 0x3f));
    }
  }
}

// Find all keys bound to act.  Multiple bindings are separated by ;
// NULL if none.  Free when done.
char *bound_keys(actid_t act)
{
  string ret;
  for(auto &m: _keymap) {
    auto &k = m.first;
    auto &v = m.second;
    if(v.act == act && !(v.ign_mod & (0xf << 12)) && !(v.ign_mod & k.mod)) {
      if(ret.size())
	ret += ';';
      add_mods(ret, k, v.ign_mod);
      add_code(ret, k);
    }
  }
  for(unsigned i = 0; i < _group_act.size(); i++)
    if(_group_keys[i].size() && _group_act[i] == act) {
      if(ret.size())
	ret += ';';
      bool did1 = false;
      for(auto k: _group_keys[i]) {
	if(!did1)
	  add_mods(ret, _group_keys[i][0], _group_ign_mod[i]);
	else
	  ret += ',';
	add_code(ret, k);
      }
    }
  return ret.size() ? strdup(ret.c_str()) : NULL;
}

bool is_bound(actid_t act)
{
  for(auto &m: _keymap) {
    auto &v = m.second;
    if(v.act == act)
      return true;
  }
  return false;
}

char *canonicalize_key(const char *key)
{
  Uint16 ign_mod;
  auto keys = parse_key(key, ign_mod);
  if(keys.empty())
    return NULL;
  string ret;
  bool did1 = false;
  for(auto k: keys) {
    if(!did1)
      add_mods(ret, k, ign_mod);
    else
      ret += ',';
    add_code(ret, k);
  }
  return strdup(ret.c_str());
}

/******** event processing *************/

// If a key is currently down belonging to any group, it is added to the
// active chording list, if not alredy there.  That way, multiple presses of
// the same key are ignored, among other uses.
static unordered_set<keydesc_t> _chord_keys;
// In addition, the current group count is incremented by one.
// If the count equals the group size, the down event is sent.
static vector<unsigned> _group_cnt;
// Releasing a key removes it from _chord_keys and decrements the count of
// all groups it belongs to.  An up event may be sent as a result.

static actid_t press_key(keydesc_t k, bool press, int str = -1)
{
  auto m = _keymap.find(k);
  
  if(m == _keymap.end())
    return 0;
  if(str < 0)
    str = press ? 0xff : 0x80;
  uint32_t *groups = m->second.groups;
  actid_t act = m->second.act < 0 ? -m->second.act + str : m->second.act;
  if(!groups)
    return press ? act : -act;
  for(unsigned i = 0; groups[i]; i++) {
    unsigned gno = groups[i] - 1;
    keydesc_t gk = _group_keys[gno][0];
    // same code guaranteed in key list
    gk.code = k.code;
    // but check modifier
    Uint16 mod = k.mod;
    mod &= ~_group_ign_mod[gno];
    for(Uint16 m = 3, f = 0; f < 4; f++, m <<= 2) {
      if(m == 0x0c)
	m <<= 4;
      if((gk.mod & m) == m && (mod & m))
	mod |= m;
    }
    if(mod != gk.mod)
      continue; // not the required modifier
    bool cpressed = _chord_keys.find(gk) != _chord_keys.end();
    if(press == cpressed)
      continue; // already pressed/unpressed; ignore
    if(press) {
      _group_cnt.resize(_group_keys.size(), 0); // expand if needed
      _group_cnt[gno]++;
    }
    // chording groups override unchorded bindings, once they match
    // multiple chording groups activating simultanously activate the
    // last one found
    // If the same key isn't used to activate and deactivate the chord,
    // it's possible to have dangling events.  Tough.
    if(_group_cnt[gno] == _group_keys[gno].size()) {
      act = _group_act[gno];
      act = act < 0 ? -act + str : act;
    }
    if(!press)
      _group_cnt[gno]--;
  }
  return press ? act : -act;
}

// An array of controllers detected by the system.
static vector <unsigned> _pad_jsindex;
// If a mapping explicitly references a controller,
// it is opened/scanned (see _pads).  If not, it is
// closed, and reopened later if a new referece is
// discovered.
static vector<SDL_GameController *> _pad_handle;
// Here is the state, if open
struct ctrlst_t {
  vector<uint8_t> axes;
  vector<bool> buttons;
};
static vector<ctrlst_t> _pad_state;
 
// To ease lookups, here are the names in a hash table.
static unordered_map<string, vector<unsigned>> _pad_names;

// Scan system for controllers.  Only done once.
static void gp_init(void)
{
  static bool did_init = false;
  if(did_init)
    return;
  SDL_Init(SDL_INIT_GAMECONTROLLER);
  unsigned nj = SDL_NumJoysticks();
  for(unsigned i = 0; i < nj; i++)
    if(SDL_IsGameController(i)) {
#if SDL_VERSION_ATLEAST(2,24,0)
      if(_ilog) {
	// map includes guid as first field
	// FIXME: also print name
	char *map = SDL_GameControllerMappingForDeviceIndex(i);
	if(!map) {
	  char buf[33];
	  SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
	  SDL_GUIDToString(guid, buf, sizeof(buf));
	  map = strdup(buf);
	}
	fprintf(_ilog, "Found controller %d: %s %s\n",
		(int)_pad_jsindex.size(), SDL_JoystickPathForIndex(i), map);
	free(map);
      }
#endif
      _pad_jsindex.push_back(i);
    }
  // Sort by player index, and then detection order
  sort(_pad_jsindex.begin(), _pad_jsindex.end(),
       // I assume an open isn't required before the playerindex
       // is valid.  Don't really know, since I don't think it's
       // ever valid on Linux unless manually set.
       [](unsigned &a, unsigned &b) {
	 if(SDL_JoystickGetDevicePlayerIndex(a) <
	    SDL_JoystickGetDevicePlayerIndex(b))
	   return true;
	 return a < b;
       });
  // Fill in support info
  _pad_handle.resize(_pad_jsindex.size());
  _pad_state.resize(_pad_jsindex.size());
  int i = 0;
  for(auto js: _pad_jsindex) {
    SDL_GameController *pad = SDL_GameControllerOpen(js);
    if(!pad) {
      // FIXME: probably print to better place
      perror(SDL_GetError());
      continue;
    }
    // stupid range-based for() doesn't give an iterator
    // even if a reference is used
    // int i = &js - pads.begin();
    string name;
    const char *s = SDL_GameControllerName(pad);
    if(_ilog)
      fprintf(_ilog, "Pad %d is named \"%s\"\n", i, s);
    while(s && *s)
      // FIXME: this is not Unicode-aware
      name.push_back(tolower(*s++));
    _pad_names[name].push_back(i);
    SDL_JoystickGUID guid = SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pad));
    char guids[33];
    SDL_JoystickGetGUIDString(guid, guids, sizeof(guids));
    if(_ilog)
      fprintf(_ilog, "Pad %d has guid \"%s\"\n", i, guids);
    string g;
    for(s = guids; *s; s++)
      // FIXME: this is not Unicode-aware
      g.push_back(tolower(*s));
    _pad_names[g].push_back(i);
    SDL_GameControllerClose(pad);
    ++i;
  }
  did_init = true;
}

// After parsing the controller mapping:
// An array of controllers.  Handles are:
//    _pad_handle[_pad_id_map[GP#-1][...]]
static vector<vector<unsigned>> _pad_id_map;
static uint64_t _pads_open = 0;
// And, for checking, here is the reverse map.
static vector<unsigned> _pad_id;

// Initialize controller map from JSON
// FIXME: This is sort of an Aq dependency, since it
// requires libjson.
void map_controllers(const array_t *map)
{
  gp_init();
  _pad_id_map = {};
  _pad_id = {};
  _pad_id.resize(_pad_jsindex.size(), 0);
  if(map) {
    for(unsigned i = 0; i < map->length; i++) {
      _pad_id_map.push_back({});
      if(map->a[i].type != VDT_ARRAY)
	// FIXME: this is actually an error
	continue;
      const array_t *binds = map->a[i].a;
      bool is_vb = false;
      for(unsigned j = 0; j < binds->length; j++) {
	if(binds->a[j].type != VDT_STRING)
	  // FIXME: this is actually an error
	  continue;
	if(!j && !strcmp(binds->a[i].s, "|")) {
	  is_vb = true;
	  continue;
	}
	string s(binds->a[j].s);
	unsigned fixedno = 0;
	bool is_star = false;
	for(int e = (int)s.size() - 1; e >= 0; e--) {
	  if(!isdigit(s[e])) {
	    if(s[e] == '*' && e && e == (int)s.size())
	      e--;
	    if(e == (int)s.size() - 1 || s[e] != '#')
	      break;
	    is_star = s[e + 1] == '*';
	    fixedno = strtoul(binds->a[j].s + e + 1, NULL, 10);
	    s.erase(e);
	    break;
	  }
	}
	if(is_vb && !is_star && !fixedno)
	  fixedno = 1;
	if(!s.size()) {
	  // Empty: all (but only if not yet mapped)
	  for(unsigned c = 0; c < _pad_id.size(); c++)
	    if(!fixedno || c == fixedno - 1) {
	      if(is_vb)
		j = binds->length - 1;
	      if(!_pad_id[c]) {
		if(_ilog)
		  fprintf(_ilog, "Bind pad %d to virtual %d with \"%s\"\n",
			  c, i + 1, binds->a[j].s);
		_pad_id[c] = i + 1;
		_pad_id_map[i].push_back(c);
		if(is_star)
		  break;
	      }
	    }
	  if(!fixedno && j + 1 != binds->length)
	    // FIXME: warning: "" consumes all; no more patterns accepted
	    break;
	} else if(s[0] == '^' && s.back() == '$') {
	  // all with regex match in _pad_name
	  try {
	    using namespace regex_constants;
	    regex re(s.substr(1, s.size() - 1), icase | extended);
	    smatch m;
	    set<unsigned> found;
	    for(auto c : _pad_names)
	      if(regex_match(c.first, m, re) &&
		 (unsigned)m.length() == c.first.size())
		for(auto g : c.second)
		  found.insert(g);
	    unsigned fi = 0;
	    for(auto f : found) {
	      if(!fixedno || fi == fixedno - 1) {
		if(is_vb)
		  j = binds->length - 1;
		if(!_pad_id[f]) {
		  if(_ilog)
		    fprintf(_ilog, "Bind pad %d to virtual %d with \"%s\"\n",
			    f, i + 1, binds->a[j].s);
		  _pad_id[f] = i + 1;
		  _pad_id_map[i].push_back(f);
		  if(is_star)
		    break;
		}
	      }
	      ++fi;
	    }
	  } catch(regex_error &e) {
	    if(_ilog)
	      fprintf(_ilog, "controlers[%d][%d] (\"%s\"): %s\n",
		      i, j, s.c_str(), e.what());
	  }
	} else {
	  // all with exact match in _pad_name
	  // FIXME: this is not Unicode-aware
	  string m;
	  for(auto ch : s)
	    // FIXME: this is not Unicode-aware
	    m.push_back(tolower(ch));
	  auto c = _pad_names.find(m);
	  if(c == _pad_names.end())
	    continue;
	  if(is_vb)
	    j = binds->length - 1;
	  unsigned fi = 0;
	  for(auto f : c->second) {
	    if(!fixedno || fi == fixedno - 1)
	      if(!_pad_id[f]) {
		if(_ilog)
		  fprintf(_ilog, "Bind pad %d to virtual %d with \"%s\"\n",
			  f, i + 1, binds->a[j].s);
		_pad_id[f] = i + 1;
		_pad_id_map[i].push_back(f);
		if(is_star)
		  break;
	      }
	    ++f;
	  }
	}
      }
    }
  }
  // Assign remaining controllers to empty slots in order
  unsigned i = 0;
  for(unsigned j = 0; j < _pad_id.size(); j++) {
    if(_pad_id[j])
      continue;
    while(i < _pad_id_map.size()) {
      if(!_pad_id_map[i].size())
	break;
      ++i;
    }
    if(i == _pad_id_map.size())
      _pad_id_map.resize(i + 1);
    if(_ilog)
      fprintf(_ilog, "Bind pad %d to virtual %d (default)\n",
	    j, i + 1);
    _pad_id_map[i] = {j};
    _pad_id[j] = i + 1;
  }
  // Close unused controllers
  // Open used controllers.
  for(unsigned i = 0; i < _pad_id.size(); i++) {
    bool needs_close = !(_pads & (1 << (_pad_id[i] - 1)));
    if(_pad_handle[i] && needs_close) {
      SDL_GameControllerClose(_pad_handle[i]);
      _pad_handle[i] = NULL;
    } else if(!_pad_handle[i] && !needs_close) {
      _pad_handle[i] = SDL_GameControllerOpen(_pad_jsindex[i]);
      if(!_pad_handle[i]) {
	if(_ilog)
	  fprintf(_ilog, "Can't re-open gamepad %u: %s\n", i, SDL_GetError());
      }
    }
  }
  _pads_open = _pads;
  // This would be a good place to scan the keymap to clear bits
  // in _axes/_buttons.
}

static void gp_update(void)
{
  if(_pads_open != _pads) {
    // There will only ever be added pads, never removed
    // If I do ever scan for removed pads, I'll close them during the scan
    uint64_t p = _pads & ~_pads_open;
    for(unsigned i = 0; i < _pad_id.size(); i++) {
      if(!(p & (1 << (_pad_id[i] - 1))))
	continue;
      if(!_pad_handle[i]) {
	_pad_handle[i] = SDL_GameControllerOpen(_pad_jsindex[i]);
	if(!_pad_handle[i]) {
	  if(_ilog)
	    fprintf(_ilog, "Can't re-open gamepad %u: %s\n", i, SDL_GetError());
	}
      }
    }
    _pads_open = _pads;
  }
  SDL_GameControllerUpdate();
}

bool proc_sdl_event(const SDL_Event *event,
		    function<bool(keydesc_t,bool,int)> proc)
{
  if(event) {
    if(event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
      keydesc_t k = { .type = KEYT_SCANCODE,
	              .mod = (Uint16)(event->key.keysym.mod & KMOD_ALL),
	              .code = (uint16_t)event->key.keysym.scancode };
      bool pressed = event->type == SDL_KEYDOWN;
      proc(k, pressed, -1);
    }
    return false;
  }
  if(!_pads || !_pad_jsindex.size()) // no bindings; ignore gamepads
    return false;
  gp_update();
  bool ret = false;
  Uint16 mod = SDL_GetModState();
  for(unsigned p = 0; p < _pad_id.size(); p++) {
    auto gc = _pad_handle[p];
    if(!gc)
      continue;
    ctrlst_t &cs = _pad_state[p];
    uint64_t mask = _axes;
    for(uint16_t i = 0; mask; i++, mask >>= 1)
      if(mask & 1) {
	int axval = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)i);
	axval = (axval + 32768) >> 8;
	if(i >= cs.axes.size())
	  cs.axes.resize(i + 1, 0x80);
	if(axval == cs.axes[i])
	  continue;
	cs.axes[i] = axval;
	keydesc_t k = { .type = KEYT_AXIS, .mod = mod, .code = i };
	ret |= proc(k, axval < 0x60 || axval > 0xa0, axval);
	axval = (0x100 - axval) & 0xff; // can't ever go all the way -
	k.code |= 0x80;
	ret |= proc(k, axval < 0x60 || axval > 0xa0, axval);
      }
    mask = _buttons;
    for(uint16_t i = 0; mask; i++, mask >>= 1)
      if(mask & 1) {
	bool pressed = SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)i);
	if(i >= cs.buttons.size())
	  cs.buttons.resize(i + 1, false);
	if(pressed == cs.buttons[i])
	  continue;
	cs.buttons[i] = pressed;
	keydesc_t k = { .type = KEYT_BUTTON, .mod = mod, .code = i };
	ret |= proc(k, pressed, -1);
      }
    }

#if 0
    // Is there a reliable way to detect errors?
    // I have no idea.  All of the above functions just say "SDL_GetError()"
    // but I get an error every. single. time.  SDL_ClearError() above that
    // seems to do nothing.  So if there is a disconnect, weird things might
    // happen.  I don't care.
    const char *err = SDL_GetError(); // maybe disconnect?
    if(err) {
	perror(err);
	// note: closed controllers are not handled right now, actually.
	SDL_GameControllerClose(gc);
	_pad_handle[p] = NULL;
    }
#endif
  return ret;
}

static pthread_mutex_t _caplock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _capsig = PTHREAD_COND_INITIALIZER;
bool _capturing = false;

static unordered_set<keydesc_t> _cap_keys, _cap_pressed;

// Unlike press_key, all keys are immediately added to _cap_keys on press
// and also added to _cap_pressed.  Release remves from _cap_pressed.
// A non-empty _cap_keys + emtpy _cap_pressed thus ends the capture.
// FIXME: save the axis state, and reduce jitter.  Don't accept an axis value
//        until this reduced-jitter value has been stable for at least .1 sec
static bool capture_key(keydesc_t k, bool press, int str = -1)
{
  auto pressed = _cap_pressed.find(k);
  keydesc_t sk = k; // signed w/ bit 15
  if(press && str >= 0 && str < 0x80)
    sk.code |= 0x8000;
  if(pressed != _cap_pressed.end()) { // already captured
    if(press) { // press when already pressed: mostly ignore
      // If it's an axis, it might change sign
      // The only way to do this right is to do the .1sec wait, since it
      // might cross 0 while changing sign.
      if(_cap_keys.find(sk) == _cap_keys.end()) {
	// k or -k must be there, since it was already pressed
	_cap_keys.insert(sk);
	sk.code ^= 0x8000;
	_cap_keys.erase(sk);
      }
      return true;
    } else { // release
      _cap_pressed.erase(k);
      if(_cap_pressed.size())
	// This would be a good time to wait .1s if all "keys" are axes
	return true;
      // DONE
      return false;
    }
  } else { // not currently known to be pressed
    auto cap = _cap_keys.find(sk);
    // double-esc aborts
    // FIXME: right-click aborts, too
    if(cap != _cap_keys.end() && k.type == KEYT_SCANCODE &&
       k.code == SDL_SCANCODE_ESCAPE) {
      _cap_keys.clear();
      _cap_pressed.clear();
      // DONE
      return false;
    }
    if(cap != _cap_keys.end())
      return true; // ignore dups
    _cap_keys.insert(sk);
    _cap_pressed.insert(k);
    // if it's an axis inversion, delete the other dir
    if(sk.type == KEYT_AXIS) {
      sk.code ^= 0x8000;
      _cap_keys.erase(sk);
    }
    return true;
  }
}

bool sdl_event(const SDL_Event *event, void (*f)(actid_t act, Uint16 mod))
{
  if(_capturing) {
    if(proc_sdl_event(event, capture_key))
      return true;
    pthread_mutex_lock(&_caplock);
    _capturing = false;
    pthread_cond_signal(&_capsig);
    pthread_mutex_unlock(&_caplock);
    return true;
  }
  return proc_sdl_event(event,
    [=](keydesc_t k, bool pressed, int str) {
      actid_t act = press_key(k, pressed, str);
      if(!act)
	return false;
      f(act, k.mod);
      return true;
    });
}

static Uint16 key_to_mod(Uint16 k)
{
  switch(k) {
   case SDL_SCANCODE_LSHIFT:  return KMOD_LSHIFT;
   case SDL_SCANCODE_RSHIFT: return KMOD_RSHIFT;
   case SDL_SCANCODE_LALT:    return KMOD_LALT;
   case SDL_SCANCODE_RALT:   return KMOD_RALT;
   case SDL_SCANCODE_LCTRL:   return KMOD_LCTRL;
   case SDL_SCANCODE_RCTRL:  return KMOD_RCTRL;
   case SDL_SCANCODE_LGUI:    return KMOD_LGUI;
   case SDL_SCANCODE_RGUI:   return KMOD_RGUI;
  }
  return 0;
}

char *capture_sdl_events()
{
  _cap_keys.clear();
  _cap_pressed.clear();
  pthread_mutex_lock(&_caplock);
  _capturing = true;
  while(_capturing && pthread_cond_wait(&_capsig, &_caplock));
  pthread_mutex_unlock(&_caplock);
  // now the complete key is in _cap_keys.
  if(!_cap_keys.size())
    return NULL; // aborted
  string ret;
  // Modifiers: what do I do?
  Uint16 mods = 0, kmods = 0;
  for(auto k : _cap_keys) {
    // take all active modifiers and or them together.  Unfortunately, this
    // means that pre-existing modifiers will affect the capture.
    mods |= k.mod;
    Uint16 m;
    if(k.type == KEYT_SCANCODE && (m = key_to_mod(k.code))) {
      // take all explicit modifier keys and add them to the mask
      // also, track of which modifiers
      mods |= m;
      kmods += m;
    } else {
      if(ret.size())
	ret += ',';
      add_code(ret, k);
    }
  }
  if(ret.size()) {
    // not all events are modifiers: drop modifiers/convert to mask
    string m;
    while(mods) {
      m += modbit_to_str(&mods);
      m += '+';
    }
    if(m.size())
      ret = m + ret;
  } else {
    // if all keys are modifiers, add each key with the others as a
    // modifier and self ignored.  E.g. lalt+lshift would become:
    //  [raw:] !lalt-!lshift-Left Alt,Left Shift (if it works, only this)
    mods &= ~kmods;
    string m;
    while(mods) {
      m += modbit_to_str(&mods);
      m += '+';
    }
    Uint16 km = kmods;
    ret = m;
    while(km) {
      ret += '!';
      ret += modbit_to_str(&km);
      ret += '+';
    }
    bool did1 = false;
    for(auto k : _cap_keys) {
      if(did1)
	ret += ',';
      add_code(ret, k);
    }
#if 1 // enable if necessary (FIXME: test)
    //  [add:] ;!lalt-lshift-Left Alt;lalt-!lshift-Left Shift
    for(auto k : _cap_keys) {
      Uint16 m1 = key_to_mod(k.code);
      km = kmods & ~m1;
      ret += ';';
      ret += m;
      ret += '!';
      ret += modbit_to_str(&m1);
      ret += '+';
      while(km) {
	ret += modbit_to_str(&km);
	ret += '+';
      }
      add_code(ret, k);
    }
  }
#endif
  _cap_keys.clear();
  return strdup(ret.c_str());
}
