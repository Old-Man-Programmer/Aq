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
/* C++ header for common GUI code */
#pragma once

#include <functional> // std::function (lambda function pointers)
#include <string>
#include <vector>

using namespace std;

typedef function<void(void)> act_func;
extern struct action {
  // parent and name both support & prefix for _/auto-ALT
  const char *parent; // If menu item, this is the parent menu
  const char *name;
  const char *def_binding; // In {<mod>+}<key>{ ...} fmt
  act_func f; // The action
  bool *toggle = 0; // If non-NULL, this is a checkbox modifing *toggle
  const char *pref = 0; // preference set by toggle
} actions[];
extern const size_t _num_actions;
extern actid_t _reset_act;
actid_t parse_action(const char *act);
char *action_to_string(actid_t act);
void toggle_menu(int act); // callback on toggle
void queue_action(actid_t act, Uint16 mod);
// Given an array of key/joy action codes, return all "possible" chords.
// "possible" includes the dedup'ed list of:
//   - the array itself
//   - any element of the array which is currently bound
//   - any currently bound chord which has an element also in the array
vector<vector<actid_t>> possible_chords(vector<actid_t> chord);
// Return chord ID if in mapped chord list
// Otherwise, return -1.
// Also, "sort -u" chord.
int chord_id_of(vector<actid_t> &chord);
// Chord ID is index into this array
extern vector<vector<actid_t>> _chords;

// Where did file f come from?  If nowhere, pick a place.
const char *file_from(const char *subdir, const char *f, char *&buf);

// execute queued actions
void exec_actions(bool just_one);

// Action support
extern bool _show_menubar;
extern struct romdesc {
  const char *what = 0;
  const char *&where;
  const char *pref = 0;
  string buf = string();
} _gamerom_desc, _mainrom_desc, _charrom_desc, _ebrom_desc;

// Action callbacks requiring GUI
void askbasic(void), askroms(void), askrom(romdesc *rom), asktape(void),
     bind_key(void), toggle_menu(void), show_about(void);
// Callback utilities
void asked_tape(void);
void ud_rom(romdesc &real, const romdesc &tmp, const char *def);

// bindkey support
extern const char * const aqkeys[]; // Primary labels
extern const char aqkeys_shift[]; // Shifted labels, upper-right
extern const char * const aqkeys_ctl[]; // CTL labels, above key blue on black
#define NKEYS 49 // length of above arrays
#define KEY_CTL 13
#define KEY_SHIFT 26
extern const int _row_nkeys[]; // 4 entries: number of keys in each row
extern const char * const dirs[]; // direction names, counter-clockwise from E
