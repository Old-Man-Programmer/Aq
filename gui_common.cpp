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
/*
 * Code in commmon with qtgui and imgui
 * If one or the other is dropped, this can just be merged into the
 * main qtgui/imgui code
 */
#include <unordered_map>
#include <set>
extern "C" {
#include "Aq.h"
}
#include "gui_common.h"

bool _show_menubar = true, _fullscreen = false;

// Flags for non-flag-like actions
static bool _mute = false, _keyscan = false, _lturbo = false;

// Mute is handled locally (not saved to pref file)
static int _old_vol = 0; // FIXME: read from pref file or whatever

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
romdesc _gamerom_desc = {
  "Cartridge", _gamerom
};
romdesc _mainrom_desc = {
  "Main (BASIC)", _mainrom, "basicrom"
};
romdesc _charrom_desc = {
  "Character", _charrom, "charrom"
};
romdesc _ebrom_desc = {
  "Extended BASIC", _ebrom, "ebrom"
};

struct action actions[] = {
  { "&Aq",   "&About...",                  "Alt+H", // alt-a conflicts w/ &Aq
      []{show_about();}},
  { "&Aq",   "Load &BASIC...",             "Alt+B",
      []{askbasic();}},
  { "&Aq",   "Change &ROMs...",            "Alt+R",
      []{askroms();}},
#define RESET_ACT "RESET"
  { "&Aq",   RESET_ACT,                    "F11",
      []{_doreset = true;}},
  { "&Aq",   "&Quit",                      "F12",
      []{quit();}},
#define ASK_TAPE "Set &Tape File..."
  { "&Tape", ASK_TAPE,          "Alt+P", // Alt+T == Tape menu
      []{asktape();}},
  { "&Tape", "Auto Ask on &Save",          NULL,
      []{}, &_tape_ask_save, "tape_ask_save" },
  { "&Tape", "Auto Ask at &End",           NULL,
      []{}, &_tape_ask_end, "tape_ask_end" },
  { "&Tape", "Auto Ask at &Load",          NULL,
      []{}, &_tape_ask_load, "tape_ask_load" },
  { "G&UI",  "Toggle &Fullscreen",         "Alt+F", // imgui uses(?) Alt+Enter
      []{set_fullscreen();}, &_fullscreen, "fullscreen" },
  { "G&UI",  "Show &Menu",                 "F10",
      []{toggle_menu();}, &_show_menubar, "menubar" },
  { "G&UI",   "Bind &Keys",                 "Alt+K",
      []{bind_key();}},
  // Non-menu acitons
  // ROM actions used to be in a &ROM menu, but I llike it better as a dialog
  // so they can all be set in one go, and text entries to allow manual path
  // removal so they always retrieve from xdg path
  { NULL,    "Set Cartridge...",           "Alt+G", // Alt+C?
      []{askrom(&_gamerom_desc);}},
  { NULL,    "Set BASIC ROM...",           NULL,
      []{askrom(&_mainrom_desc);}},
  { NULL,    "Set Character ROM...",       NULL,
      []{askrom(&_charrom_desc);}},
  { NULL,    "Set Extended BASIC ROM...",  NULL,
      []{askrom(&_ebrom_desc);}},
  { NULL,    "Use Extended BASIC",         NULL,
      []{}, &_extendedbasic, "use_ebasic" },
  // This makes no sense in a menu
  { NULL,    "Turbo Toggle",               "F1",
      []{
	if(_lturbo)
	  _turbo |= TURBO_KEY;
	else
	  _turbo &= ~TURBO_KEY;
	win_title(_lturbo ? "Aquarius (turbo)" : "Aquarius");
      }, &_lturbo},
  // Volume control only barely makes sense in a menu
  { NULL,    "Volume Up",                  NULL,
      []{
	int &v = _mute ? _old_vol : _vol_shift;
	if(v) {
	  v--;
	  set_config_num("snd_quiet", v);
	}
      }},
  { NULL,    "Volume Down",                NULL,
      []{
	int &v = _mute ? _old_vol : _vol_shift;
	if(v < 13) {
	  v++;
	  set_config_num("snd_quiet", v);
	}
      }},
  { NULL,    "Mute Toggle",                NULL,
      []{
	if(_mute) {
	  _old_vol = _vol_shift;
	  _vol_shift = 15;
	} else
	  _vol_shift = _old_vol;
      }, &_mute},
  // I just never thought of these as being menu items.  Maybe if a future
  // "other" config dialog exists, they will be moved there.
  { NULL,    "Keyboard Mode Toggle",        NULL,
      []{ _keymode = _keyscan ? SCAN : TIMER; }, &_keyscan, "keyscan"},
  { NULL,    "Swap Joysticks",              "Keypad Enter",
      []{}, &_swap_joy, "swap_joy" }
};
const size_t _num_actions = sizeof(actions)/sizeof(actions[0]);

using namespace std;

static void strip_actname(string &s)
{
  // strip/ignore non-alnum except /
  for(int p = s.size() - 1; p >= 0; p--)
    if(!isalnum(s[p]) && s[p] != '/')
      s.erase(p, 1);
    else if(isupper(s[p]))
      s[p] = tolower(s[p]);
}

static bool _acts_ready = false;
static unordered_map<string, int> _acts;

vector<vector<actid_t>> _chords;

const char * const dirs[] = {
  "E", "ENE", "NE", "NNE", "N", "NNW", "NW", "WNW",
  "W", "WSW", "SW", "SSW", "S", "SSE", "SE", "ESE"
};

actid_t parse_action(const char *act)
{
  if(!act || !*act)
    return 0;
  // 1.._num_actions == actions[code-1] (63 max)
  // +0x100 = +/- present; bits 6..7 == what:
  //   0    - -
  //   0x40 - +
  //   0x80 - -+
  //   0xc0 - +-
  if(!_acts_ready) {
    string n, m;

    for(unsigned i = 0; i < _num_actions; i++) {
      n = actions[i].name;
      strip_actname(n);
      if(!i || actions[i - 1].parent != actions[i].parent) {
	if(actions[i].parent) {
	  m = actions[i].parent;
	  strip_actname(m);
	  m += '/';
	}
      }
      _acts[n] = i + 1;
      if(actions[i].parent)
	_acts[m + n] = i + 1;
    }
    _acts_ready = true;
  }
  string n(act);
  actid_t adj = 0;
  if(n.size() > 1 && (n[0] == '+' || n[0] == '-')) {
    adj += 0x100 + (n[0] == '+' ? 0x40 : 0);
    if(n.size() > 2 && n[1] != n[0] && (n[1] == '-' || n[1] == '+')) {
      adj += 0x80;
      n.erase(0, 2);
    } else
      n.erase(0, 1);
  }
  strip_actname(n);
  auto a = _acts.find(n);
  if(a != _acts.end())
    return a->second + adj;
  // Now it must be a key or chord
  const char *s = act;
  int chord_id = -1;
  vector<actid_t> chord;
  while(1) {
    // Chords are separated by +, -, or ,.
    const char *p;
    for(p = s; *p && *p != '+' && *p != '-' && *p != ','; p++);
    // But an actual +, -, or , key is skipped
    if(p == s) {
      p++;
      // After which there must be either a chord separator or end-of-string
      if(*p && *p != '+' && *p != '-' && *p != ',') {
	// parse error; FIXME: I should show a message
	return 0;
      }
    }
    // <- key is skipped as well
    if(p == s + 1 && *s == '<' && *p == '-' &&
       (!p[1] || p[1] == '+' || p[1] == '-' || p[1] == ','))
       p++;
    if(*p && chord_id < 0)
      chord_id = _chords.size();

    // Key
    int c = p == s + 1 ? *s : 0;
    if(c < 32 || c == '[' || c == ']' || c > 'z')
      c = 0;
    else if(isupper(c))
      c = tolower(c);
    if(p > s + 1) {
      if(!strncasecmp(s, "SPACE", (int)(p - s)))
	c = ' ';
      else if(!strncasecmp(s, "RTN", (int)(p - s)))
	c = '\n';
      else if(!strncmp(s, "<-", (int)(p - s)) ||
	      !strncasecmp(s, "BS", (int)(p - s)))
	c = '\b';
      else if(!strncasecmp(s, "CTL", (int)(p - s)))
	c = 1 - 3; // 257;
      else if(!strncasecmp(s, "SHIFT", (int)(p - s)))
	c = 2 - 3; // 256;
    }
    if(c) {
      c = c < 0 ? c + 3 : findkey(c);
      // plain key: 0x200 + <keyid> (_keys[] array index)
      if(chord_id >= 0)
	chord.push_back(c + 0x200);
      else
	return c + 0x200;
    } else {
      bool is_joy2 = false;
      if(p - s < 4 || p - s > 6 || toupper(*s) != 'J' || s[2] != '_' ||
	 ((is_joy2 = s[1] != '1' && toupper(s[1]) != 'A' && toupper(s[1]) != 'L') &&
	  s[1] != '2' && toupper(s[1]) != 'B' && toupper(s[1]) != 'R')) {
	// parse error; FIXME: I should show a message
	return 0;
      }
      int code = is_joy2 ? 0x100 : 0;
      s += 3;
      if(*s >= '1' && *s <= '6') {
	if(p - s > 1) {
	  // parse error; FIXME: I should show a message
	  return 0;
	}
	// plain joy button: 0x400 + 0x100 * <joy> + <but>
	code += 0x400 + *s - '1';
      } else {
	// Linear search.  Don't care.
	int d;
	for(d = 0; d < 16; d++)
	  if(!strncasecmp(dirs[d], s, (int)(p - s)) && !dirs[d][(int)(p - s)])
	    break;
	if(d == 16) {
	  // parser error; FIXME: I should show a message
	  return 0;
	}
	// plain joy dir: 0x1000 * <dir#> + 0x800 + 0x100 * <joy>
	code += (d << 12) + 0x800;
	// (axis mode)
	code = -code;
      }
      if(chord_id >= 0)
	chord.push_back(code);
      else
	return code;
    }
    if(!*p)
      break;
    s = p + 1;
    if(!*s) {
      // parse error; FIXME: I should show a message
      return 0;
    }
  }
  // multi-key/joy:  0x10000 + <chord#>
  chord_id = chord_id_of(chord);
  if(chord_id < 0) {
    chord_id = _chords.size();
  if(chord_id == (int)_chords.size())
    _chords.push_back(chord);
  }
  return 0x10000 + chord_id;
}

int chord_id_of(vector<actid_t> &chord)
{
  set<actid_t> chmap; // sort -u
  for(auto a : chord)
    chmap.insert(a);
  chord = {};
  for(auto a : chmap)
    chord.push_back(a);
  for(int chord_id = 0; chord_id < (int)_chords.size(); chord_id++)
    if(chord == _chords[chord_id])
      return chord_id;
  return -1;
}

char *action_to_string(actid_t act)
{
  string s;
  if(!act)
    return NULL;
  if(act < 0)
    act = -act;
  if(act < 0x200) {
    // 1.._num_actions == actions[code-1] (63 max)
    // +0x100 = +/- present; bits 6..7 == what:
    //   0    - -
    //   0x40 - +
    //   0x80 - -+
    //   0xc0 - +-
    if(act & 0x100) {
      s += (act & 0x40) ? '+' : '-';
      if(act & 0x80)
	s += (act & 0x40) ? '-' : '+';
    }
    act = (act & 0x3f) - 1;
    string n(actions[act].parent ? actions[act].parent : "");
    if(actions[act].parent)
      n += '/';
    n += actions[act].name;
    strip_actname(n);
    s += n;
    return strdup(s.c_str());
  }
  // multi-key/joy:  0x10000 + <chord#>
  if(act >= 0x10000) {
    for(auto k: _chords[act - 0x10000]) {
      if(s.size())
	s += '+';
      char *ks = action_to_string(k);
      s += ks;
      free(ks);
    }
    return strdup(s.c_str());
  }
  // plain key: 0x200 + <keyid> (_keys[] array index)
  if(act < 0x400) {
    act &= 0x7f;
    if(act == 1)
      return strdup("CTL");
    else if(act == 2)
      return strdup("SHIFT");
    else if(act == 3 + ' ')
      return strdup("SPACE");
    else if(act == 3 + '\n')
      return strdup("RTN");
    else if(act == 3 + '\b')
      return strdup("<-");
    char r[2] = { (char)(act - 3), 0 };
    return strdup(r);
  }
  // plain joy button: 0x400 + 0x100 * <joy> + <but>
  // plain joy dir: KV_AX + 0x1000 * <dir#> + 0x800 + 0x100 * <joy>
  s = "J";
  s += (act & 0x100) ? 'R' : 'L';
  s += '_';
  if(act < 0x800)
    s += (act & 7) + '1';
  else
    s += dirs[act >> 12];
  return strdup(s.c_str());
}

// Given an array of key/joy action codes, return all "possible" chords.
// "possible" includes the dedup'ed list of:
//   - the array itself
//   - any element of the array which is currently bound
//   - any currently bound chord which has an element also in the array
vector<vector<actid_t>> possible_chords(vector<actid_t> chord)
{
  set<vector<actid_t>> rets = { chord };
  for(auto a : chord) {
    if(is_bound(a))
      rets.insert({a});
    for(unsigned i = 0; i < _chords.size(); i++) {
      auto &c = _chords[i];
      for(auto e : c)
	if(e == a && is_bound(0x10000 + i)) {
	  rets.insert(_chords[i]);
	  break;
	}
    }
  }
  vector<vector<actid_t>> ret;
  for(auto c : rets)
    ret.push_back(c);
  return ret;
}

struct queued_act {
  actid_t act;
  Uint16 mod;
};

static vector <queued_act> _act_queue;

void queue_action(actid_t act, Uint16 mod) // mod is for key-type events
{
  if(act)
    _act_queue.push_back({ .act = act, .mod = mod });
}

static actid_t _asktape;
actid_t _reset_act;

#if defined(USE_QT) || defined(USE_IMGUI)
volatile bool _tape_done;
static pthread_mutex_t _asktape_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _asktape_sig = PTHREAD_COND_INITIALIZER;
#endif

// always executed in a different thread than gui
void reqtape(void)
{
#if defined(USE_QT) || defined(USE_IMGUI)
  _tape_done = false;
  _act_queue.push_back({ .act = _asktape + 1, .mod = 0 });
  pthread_mutex_lock(&_asktape_lock);
  while(!_tape_done) {
    while(pthread_cond_wait(&_asktape_sig, &_asktape_lock)) {
      if(errno == EAGAIN || errno == EINTR)
	continue;
      perror("asktape");
      exit(1);
    }
  }
  pthread_mutex_unlock(&_asktape_lock);
#endif
}

#if defined(USE_QT) || defined(USE_IMGUI)
// called from GUI thread
void asked_tape(void)
{
  pthread_mutex_lock(&_asktape_lock);
  _tape_done = true;
  pthread_cond_signal(&_asktape_sig);
  pthread_mutex_unlock(&_asktape_lock);
}
#endif

void exec_actions(bool just_one)
{
  while(_act_queue.size() > 0) {
    auto act = _act_queue[0].act;
    auto mod = _act_queue[0].mod;
    _act_queue.erase(_act_queue.begin());
    bool is_up = act < 0;
    if(is_up)
      act = -act;
    if(act < 0x200) {
      // 1.._num_actions == actions[code-1] (63 max)
      // +0x100 = +/- present; bits 6..7 == what:
      //   0    - -
      //   0x40 - +
      //   0x80 - -+
      //   0xc0 - +-
      unsigned ract = act & 0x3f;
      if(actions[ract - 1].toggle) {
	bool nval;
	if(!(act & 0x80) && is_up)
	  return;
	if(act & 0x100) {
	  if(act & 0x80)
	    nval = is_up ^ !!(act & 0x40);
	  else
	    nval = is_up ? *actions[ract - 1].toggle : !!(act & 0x40);
	  act &= 0x3f;
	} else
	  nval = *actions[act - 1].toggle ^ true;
	if(nval != *actions[act - 1].toggle) {
	  *actions[act - 1].toggle = nval;
	  if(actions[act - 1].pref)
	    set_config_bool(actions[act - 1].pref, nval);
#ifdef USE_QT
	  toggle_menu(act - 1);
#endif
	}
      } else if(is_up)
	return;
      actions[act - 1].f();
      return;
    }
    // multi-key/joy:  0x10000 + <chord#>
    if(act >= 0x10000) {
      for(auto k: _chords[act - 0x10000])
	// FIXME: these should be inserted at front, instead of back
	queue_action(is_up ? -k : k, mod);
      return;
    }
    // plain key: 0x200 + <keyid> (_keys[] array index)
    if(act < 0x400) {
      dokey(act & 0x7f, mod, !is_up);
      return;
    }
    // plain joy button: 0x400 + 0x100 * <joy> + <but>
    // plain joy dir: KV_AX + 0x1000 * <dir#> + 0x800 + 0x100 * <joy>
    bool is_joy2 = !!(act & 0x100);
    if(act < 0x800)
      aqjoy_but(is_joy2, act & 7, !is_up);
    else
      aqjoy_dial(is_joy2, act >> 12, act & 0xff);
    if(just_one)
      return;
  }
}

static void add_sdl_hotkeys()
{
  for(unsigned i = 0; i < _num_actions; i++)
    if(actions[i].def_binding) {
      bind_key(actions[i].def_binding, (actid_t)i + 1);
    }
  aqjoy_init();
  const value_t *conf = config_val("keys");
  // config reader already ensures this is valid, if present
  if(conf) {
    for(pair_t *p = conf->o; p; p = p->next) {
      const char *key = p->name, *fn = p->v.s;
      if(p->v.type == VDT_NULL)
	fn = NULL;
      actid_t act = parse_action(fn);
      if(!act && fn && *fn) {
	fprintf(stderr, "warning: ignoring binding %s; action '%s' not found\n", key, fn);
	continue;
      }
      if(!bind_key(key, act))
	// this errr should never happen since it's validated by config read
	fprintf(stderr, "warning: ignoring binding %s: key error\n", key);
    }
  }
  const value_t *c = config_val("controllers");
  if(c && c->type != VDT_ARRAY) {
    c = NULL;
    fputs("warning: ignoring controllers pref; not an array\n", stderr);
  }
  map_controllers(c ? c->a : NULL);
}

void init_gui_config(void)
{
  // Checkbox defaults
  for(unsigned i = 0; i < _num_actions; i++) {
    if(actions[i].toggle && actions[i].pref) {
      add_default_type(actions[i].pref, *actions[i].toggle ? VDT_TRUE : VDT_FALSE);
      *actions[i].toggle = config_bool(actions[i].pref);
    }
    if(!strcasecmp(actions[i].name, ASK_TAPE))
      _asktape = i;
    else if(!strcasecmp(actions[i].name, RESET_ACT))
      _reset_act = i;
  }
  add_default_num("snd_quiet", 0);
  _vol_shift = config_num("snd_quiet");
  if(_vol_shift < 0 || _vol_shift > 15)
    _vol_shift = 0;
  add_sdl_hotkeys();
}

void ud_rom(romdesc &real, const romdesc &tmp, const char *def)
{
  if((real.where == NULL) == !tmp.buf.size() &&
     (!real.where || tmp.buf == real.where))
    return;
  _doreset = true;
  if(!tmp.where) {
    real.where = def;
    real.buf.clear();
    if(real.pref)
      set_config_str(real.pref, def);
  } else if(real.pref) {
    set_config_str(real.pref, tmp.buf.c_str());
    real.where = config_str(real.pref);
  } else {
    real.buf = tmp.where;
    real.where = real.buf.c_str();
  }
}

// Dummy functions for non-gui
#if !defined(USE_IMGUI) && !defined(USE_QT)
void askbasic() {}
void askroms() {}
void bind_key() {}
void toggle_menu() {}
void show_about() {}
void askrom(_U romdesc *rom) {}
void asktape(void){}

extern float turbo_rate;

void win_title(_U const char *s)
{
  if (_turbo) {
    char title[K];
    sprintf(title, "%s (turbo %.2fx)", s, turbo_rate);
    SDL_SetWindowTitle(_window, title);
  } else {
    SDL_SetWindowTitle(_window, s);
  }
}
#endif
#ifndef USE_QT
void set_fullscreen()
{
  SDL_SetWindowFullscreen(_window, _fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
#endif

static pthread_t _evthread;
static pthread_mutex_t _evlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _evsig = PTHREAD_COND_INITIALIZER;
static volatile bool _evshutdown = false;

static void *sdl_events(_U void *parm)
{
  pthread_mutex_lock(&_evlock);
  while(!_evshutdown) {
    SDL_Event event;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    while(!_evshutdown && SDL_PollEvent(&event)) {
      if(event.type == SDL_QUIT) {
	quit();
	break;
      }
      // FIXME: if capturing, left/right mouse click terminate capture
#ifdef USE_IMGUI
      if(!_capturing && imgui_event(&event))
	continue;
#endif
      if(sdl_event(&event, queue_action))
	continue;
      // process key events
      if ((event.type == SDL_KEYUP || event.type == SDL_KEYDOWN) &&
	  event.key.keysym.sym <= 128)
	dokey(event.key.keysym.sym, event.key.keysym.mod, event.type == SDL_KEYDOWN);
    }
    if(_evshutdown)
      break;
    sdl_event(NULL, queue_action); // process stick events
#if !defined(USE_IMGUI) && !defined(USE_QT)
    // not compatible with capturing, but capturing only done in GUI
    exec_actions(false);
#endif
    // loop every 1/60th of a second
    t.tv_nsec += 1000000000UL/60;
    if(t.tv_nsec >= 1000000000L) {
      t.tv_nsec -= 1000000000L;
      t.tv_sec++;
    }
    pthread_cond_timedwait(&_evsig, &_evlock, &t);
  }
  pthread_mutex_unlock(&_evlock);
  return NULL;
}

void start_sdl_events(void)
{
  static bool running = false;
  if(running || _batchmode)
    return;
  running = true;
  if(pthread_create(&_evthread, NULL, sdl_events, NULL)) {
    perror("sdl event thread");
    exit(1);
  }
}

void stop_sdl_events(void)
{
  // this is never called before start_(), so no need to check validity
  _evshutdown = true; // kill thread next opportunity
  if(!_batchmode && pthread_self() != _evthread) {
    // wake thread if sleeping; will then exit seeing _evshutdown
    pthread_mutex_lock(&_evlock);
    pthread_cond_signal(&_evsig);
    pthread_mutex_unlock(&_evlock);
    // wait for thread to completely finish
    pthread_join(_evthread, NULL);
  }
}

/* Support for bindkey dialog */
#if defined(USE_QT) || defined(USE_IMGUI)
/* each row 1/2 char to right, unless left key 1.5-char wide */
const char * const aqkeys[] = { /* Primary labels */
  /* top row, 13 keys (RST is blue on white; all others white on blue) */
  "RST", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=",
  /* row 2, 13 keys */
  "CTL", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "/", "<-",
  /* row 3, 12 keys */ /* SHIFT is 1.5 spaces wide */
  "SHIFT", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", ";",
  /* row 4, 11 keys */ /* SPACE & RTN are 1.5 spaces wide */
  "SPACE", "Z", "X", "C", "V", "B", "N", "M", ".", ",", "RTN"
};

const int _row_nkeys[] = { 13, 13, 12, 11 }; // 49 total

const char aqkeys_shift[] = /* Shifted labels, upper-right */
  /* always 1 char, so it's a string rather than array of strings */
  /* row 1, 13 keys */  /* _, + are centered rather than upper-right */
  " !\"#$%^'()?_+"
  /* row 2, 13 keys */
  "           ^\\"
  /* row 3, 12 keys */
  "          @*"
  /* row 4, 11 keys */
  "        <> ";

const char * const aqkeys_ctl[] = { /* CTL labels, above key blue on black */
  /* row 1, 13 keys */
  0, "RUN", "LIST", "IF", "THEN", "GOTO", "ON", "GOSUB", "RETURN", "COPY", 0, "FOR", "NEXT",
  /* row 2, 13 keys */
  0, 0, "REM", "DIM", "RETURN", "INPUT", 0, 0, 0, 0, 0, 0, 0,
  /* row 3, 12 keys */
  0, "CSAVE", "STPLST", "READ", "DATA", "BELL", 0, "PSET", "PRESET", "POINT", "POKE", "PEEK",
  /* row 4, 11 keys */
  "CHR$", "CLOAD", "DELINE", "STOP", "LEFT$", "MID$", "RIGHT$", 0, "STR$", "VAL", 0
};
#endif
