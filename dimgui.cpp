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
 * All IMGUI-related code is in here.
 *
 * TODO:
 *   - some sizes (e.g. button widths) hard-coded/not font-responsive
 *   - for game: "Load Config" checkbox, default checked
 *     (or Yes/No/Always/Never)
 *   - bindkey dialog
 *     - RST key is same as RESET action
 *     - keyboard: shifted symbols
 *       - tab interface should have good default size automatically
 *       - each tab should grow to fit total size
 *       - joystick should limit growth to keep circle size in check
 *       - switching tabs should not resize dialog
 *   - change displayed bound menu action key when rebound via config or GUI
 */
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_stdlib.h"
// Note: sdl.h was renamed to sdl2.h in 1.89.3.  Very annoying.
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "ImGuiFileDialog.h"
/* imgui_markdown.h has warnings, but I don't care enough to fix them */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#ifndef __clang__ // clang just warns that this warning group doesn't exist
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "imgui_markdown.h"
#pragma GCC diagnostic pop
#include "imgui_internal.h" // Needed for GetFocusID().  Why is that private?
                            // Also for drawing underlines
                            // Also to disable nav highlight on ImageButton
#include <GL/gl.h>
#include <regex>
extern "C" {
#include "Aq.h"
#include <fontconfig/fontconfig.h>
}
#include "gui_common.h"

using namespace ImGui;

static ImGuiID _rwinid = 0; // IMGUI ID for rendering widget (for focus detect)
static value_t *_imgui_state = NULL;
static const char *state_val(const char *key);

void imgui_glinit(void)
{
  // Decide GL+GLSL versions
  // From example_sdl_opengl3
  // Why is this not just in the impl file?
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
#define glsl_version "#version 100"
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
  // GL 3.2 Core + GLSL 150
#define glsl_version "#version 150"
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  // GL 3.0 + GLSL 130
#define glsl_version "#version 130"
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
}

static pthread_mutex_t imgui_lock = PTHREAD_MUTEX_INITIALIZER;

// In imgui-1.89.3, this was a structure.  Changed to pointer because static
// structure init doesn't correclty set "automatic" context (set by construtor
// executed at program startup, before context even exists).  This could've
// been designed better (e.g. init to NULL and if NULL, auto-init on use)
// c8ad25caa6bfcb786085a76e67a7f31e61a80cb7
static ImGuiFileDialog *_rdlg, *_tdlg, *_bdlg;
static ImGuiFileDialog *fdialog(const char *dir);

static ImFont *_bold, *_italic, *_small;
static ImFont *load_font(const char *name, float scale = 1.0f, bool fc_only = false);

void imgui_init(int scale)
{
  pthread_mutex_lock(&imgui_lock);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  CreateContext();
  ImGuiIO& io = GetIO();
  io.FontGlobalScale = (double)scale / 3;  // * 3 / 4?  1.0, since I added font pref?
  // From imgui.h, to change:
  // For correct scaling, prefer to reload font + rebuild ImFontAtlas +
  // call style.ScaleAllSizes().
  // On the other hand, for temporary (local to window) change:
  io.FontAllowUserScaling = true; // allow ctrl+wheel scaling of any window
  // Actually, I should probably disable this, since it's not really
  // configurable, and is globally active.
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
  // In particular, while I would like gamepad navigation, it's annoying
  // when you're actually using the gamepad for a game and the menu suddenly
  // gets activated.
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
  io.IniFilename = NULL;

  // Load persistent state
  if(_config_file) {
    // XDG_STATE would be most appropriate, but nobody uses that.
    // XDG_CACHE would be more appropriate, but I don't care.
    const char *cf = find_xdg_file(XDG_CONFIG, "Aq", "imgui.json");
    if(cf) {
      _imgui_state = json_decode_file(cf);
      // I don't like persistent locations, but whatever.
      const char *winstate = state_val("winstate");
      if(winstate && winstate[0])
	LoadIniSettingsFromMemory(winstate);
    }
  }

  _rdlg = fdialog("roms");
  _tdlg = fdialog("tapes");
  _bdlg = fdialog("bas");

  // Setup Dear ImGui style
  StyleColorsDark();
  //StyleColorsClassic();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForOpenGL(_window, _gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Fonts
  FcInit();
  add_default_str("imfont", "Sans");
  const char *font = config_str("imfont");
  if(!font || !*font || !load_font(font)) {
    io.Fonts->AddFontDefault();
    ImFontConfig cfg;
    cfg.SizePixels = io.Fonts->ConfigData[0].SizePixels * 0.7;
    _small = io.Fonts->AddFontDefault(&cfg);
  } else
    _small = load_font(font, 0.7);
  add_default_type("imbold", VDT_NULL);
  const char *ofont = config_str("imbold");
  if(ofont && *ofont)
    _bold = load_font(ofont);
  else if(font && *font) {
    // FIXME: there is probably a way in fontconfig to just add the
    // bold attrbute without tacking it to the font name
    char buf[strlen(font) + 6];
    strcpy(buf, font);
    strcat(buf, ":bold");
    _bold = load_font(buf, 1.0f, true);
  }
  add_default_type("imitalic", VDT_NULL);
  if((ofont = config_str("imitalic")) && *ofont)
    _italic = load_font(ofont);
  else if(font && *font) {
    // FIXME: there is probably a way in fontconfig to just add the
    // italic attrbute without tacking it to the font name
    char buf[strlen(font) + 8];
    strcpy(buf, font);
    strcat(buf, ":italic");
    _italic = load_font(buf, 1.0f, true);
  }
  pthread_mutex_unlock(&imgui_lock);
}

static const char *state_val(const char *key)
{
  const value_t *v = json_objval(_imgui_state, key);
  if(v && v->type == VDT_STRING)
    return v->s;
  else
    return NULL;
}

static void write_imgui_state(void)
{
  if(!_imgui_state)
    return;
  // XDG_STATE would be most appropriate, but nobody uses that.
  // XDG_CACHE would be more appropriate, but I don't care.
  mkpath(find_xdg_file(XDG_CONFIG, "Aq", NULL));
  FILE *f = fopen(find_xdg_file(XDG_CONFIG, "Aq/imgui.json", NULL), "w");
  if(!f)
    return;
  char *fv = json_pretty(_imgui_state);
  fputs(fv, f);
  fclose(f);
  free(fv);
}

static void set_imgui_state(const char *key, const char *val)
{
  value_t v = { .type = VDT_STRING, .s = (char *)val };
  if(!_imgui_state) {
    _imgui_state = (value_t *)malloc(sizeof(*_imgui_state));
    _imgui_state->type = VDT_OBJECT;
    _imgui_state->o = NULL;
  }
  json_set_objval(_imgui_state, key, &v);
  write_imgui_state();
}

static struct menukey {
  char pkey, key;
  bool pactive : 1, active : 1;
} *_menu_hot;
static int _num_menus, _cur_menu = -1;

bool _tmp_show_menubar = false;

// Returns true if GUI consumed the event
bool imgui_event(SDL_Event *event)
{
  pthread_mutex_lock(&imgui_lock);
  ImGui_ImplSDL2_ProcessEvent(event);
  pthread_mutex_unlock(&imgui_lock);
  // Technically, some mouse events are also consumed by the GUI
  // Modal dialogs definitely take focus, as they should
  // I expect menus to take focus if active, since they support key nav
  // QUIT is global.  I should process above instead, but let z80.c do it.
  if(_menu_hot && (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)) {
    if((event->key.keysym.mod & KMOD_ALT) &&
       !(event->key.keysym.mod & (KMOD_SHIFT|KMOD_GUI|KMOD_CTRL))) {
      char c = toupper(event->key.keysym.sym);
      for(int i = 0; i < _num_menus; i++)
	if(_menu_hot[i].pkey == c) {
	  if(event->type == SDL_KEYDOWN) {
	    _tmp_show_menubar = true;
	    _menu_hot[i].pactive = true;
	  }
	  return true;
	}
    }
    static char mpress = 0;
    if(!(event->key.keysym.mod & (KMOD_SHIFT|KMOD_GUI|KMOD_CTRL|KMOD_ALT))) {
      char c = toupper(event->key.keysym.sym);
      if(_cur_menu >= 0) {
	for(int i = _cur_menu; i < _num_menus; i++)
	  if(_menu_hot[i].key == c &&
	     _menu_hot[i].pkey == _menu_hot[_cur_menu].pkey) {
	    if(event->type == SDL_KEYDOWN)
	      _menu_hot[i].active = true;
	    mpress = c;
	    return true;
	  }
      } else if(c && c == mpress) {
	mpress = 0;
	if(event->type == SDL_KEYUP)
	  return true;
      }
    }
  }
  return event->type != SDL_QUIT && GetFocusID() != _rwinid;
}

static void MDLink(MarkdownLinkCallbackData data)
{
  if(!data.isImage) {
    string url(data.link, data.linkLength);
    SDL_OpenURL(url.c_str());
  }
}

static ImFont *load_font(const char *name, _U float scale, bool fc_only)
{
  // FIXME:  20 is a random number.
#define fontf() fc_only ? NULL :GetIO().Fonts->AddFontFromFileTTF(name, 20);
#if 0
  // This does not fail gracefully.  It assert()s/kills on failure.
  ImFont *ret = fontf();
  if(ret)
    return ret;
#endif
  // Instead, load from file as last resort.
  // I suppose I should at least support a -<size> suffix, but I won't
  auto pat = FcNameParse((const FcChar8 *)name);
  if(!pat)
    return fontf();
  // I have no idea what to do with pat, so I'll just
  // copy what fc-match and xft do:
  FcConfigSubstitute(NULL, pat, FcMatchPattern);
  FcDefaultSubstitute(pat);
  FcResult fcr;
  auto match = FcFontMatch(NULL, pat, &fcr);
  FcPatternDestroy(pat);
  if(!match)
    return fontf();
  FcChar8 *fname;
  if(FcPatternGetString(match, FC_FILE, 0, &fname) != FcResultMatch) {
    FcPatternDestroy(match);
    return fontf(); // can't open font w/o filename
  }
  ImFontConfig cfg; // default constructor
  fcr = FcPatternGetInteger(match, FC_INDEX, 0, &cfg.FontNo);
  double dsize;
  fcr = FcPatternGetDouble(match, FC_PIXEL_SIZE, 0, &dsize);
  if(fcr != FcResultMatch) {
    float vdpi;
    SDL_GetDisplayDPI(0, NULL, NULL, &vdpi);
    if(!vdpi)
      vdpi = 100;
    fcr = FcPatternGetDouble(match, FC_SIZE, 0, &dsize);
    if(fcr != FcResultMatch) {
      FcPatternDestroy(match);
      return fontf();
    }
    dsize = dsize *vdpi / 72; // 72 points per inch
  }
  dsize *= scale;
  ImFont *ret = GetIO().Fonts->AddFontFromFileTTF((const char *)fname, (float)dsize, &cfg /*, glyph_ranges*/);
  FcPatternDestroy(match);
//  if(!ret)
//    return fontf();
  return ret;
}

void MDFormat(const MarkdownFormatInfo& info, bool start)
{
  if(info.type == MarkdownFormatType::EMPHASIS && info.level == 1 &&
     _italic) {
    if(start)
      PushFont(_italic);
    else
      PopFont();
    return;
  }
  if(info.type == MarkdownFormatType::EMPHASIS && info.level > 1) {
    if(_bold) {
      if(start)
	PushFont(_bold);
      else
	PopFont();
    } else {
      if(start)
	//      PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_NavHighlight]);
	PushStyleColor(ImGuiCol_Text, ImVec4(0.4, 1, 1, 1));
      else
	PopStyleColor();
    }
  } else
    defaultMarkdownFormatCallback(info, start);
}

// Yeah, I'm pointlessly using a mostly unconfigured MD module to display this
// I'm too lazy to hand-craft the dialog
// FIXME: Esc should exit (now only Enter exits)

// Yeah, I just copied this from grok.  Whatever.  I wrote that, too.
// Well, actually, I converted to Markdown for ImGui
// Actually, it's pseudo-markdown. \n ends a paragraph.
static const char about_message[] =
  "Mattel **Aq**uarius Emulator\n"
  "Version " VERSION "\n"
  "Compiled " __DATE__ "\n\n"
//  Dammit, imgui_markdown doesn't support <>.  Needs []().
#if 0
  "Author: Steve Baker <steve.baker.llc@gmail.com>\n"
  /* FIXME:  the homepage should be more specific */
  "Homepage: <https://oldmanprogrammer.net>\n"
  "With help from\nThomas J. Moore <darktjm@gmail.com>\n"
#else
  "Author: Steve Baker <[steve.baker.llc@gmail.com](mailto:steve.baker.llc@gmail.com)>\n"
  /* FIXME:  the homepage should be more specific */
  "Homepage: <[https://oldmanprogrammer.net](https://oldmanprogrammer.net)>\n"
  "With help from\nThomas J. Moore <[darktjm@gmail.com](mailto:darktjm@gmail.com)>\n"
#endif
  // FIXME: add repository link & issue tracker link
  ;

static MarkdownConfig mdConfig = {
  .linkCallback = MDLink, .formatCallback = MDFormat
};

static bool _show_about = false;

void show_about(void)
{
  // This should just be OpenPopup(), but it has to be done
  // below to get the ID right
  _show_about = true;
}

static void process_about(int w, int h)
{
  ImVec2 center = GetMainViewport()->GetCenter();
  SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImVec2 dim(w, h); // It'll shrink to fit, so maximize to avoid scroll/wrap
  SetNextWindowSize(dim, ImGuiCond_Appearing); // needs initial size or it will be too narrow
  if(_show_about) {
    // This has to be done here to get the ID right
    OpenPopup("About Aq");
    _show_about = false;
  }
  if(BeginPopupModal("About Aq", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    Markdown(about_message, strlen(about_message), mdConfig);
    Dummy(ImVec2(0, 10));
    ImVec2 rem = GetContentRegionAvail();
    if(rem.x > 120)
      Indent((rem.x - 120) / 2);
    // SetKeyboardFocusHere(); // causes flashing every time mouse moves
    static bool pressed_enter = false, pressed_esc = false;
    if(Button("OK", ImVec2(120, 0))) {
      CloseCurrentPopup();
      pressed_enter = pressed_esc = false;
    } else {
      // since the focus methods don't work, and don't do esc anyway,
      // handle ok/esc manually
      bool enter = IsKeyPressed(ImGuiKey_Enter);
      bool esc = IsKeyPressed(ImGuiKey_Escape);
      if(enter)
	pressed_enter = true;
      if(esc)
	pressed_esc = true;
      if((pressed_enter && !IsKeyDown(ImGuiKey_Enter)) ||
	 (pressed_esc && !IsKeyDown(ImGuiKey_Escape))) {
	pressed_enter = pressed_esc = false;
	CloseCurrentPopup();
      }
    }
    // SetItemDefaultFocus(); // does nothing
    EndPopup();
  }
}

/*********  File popups *********/

static ImGuiFileDialog *fdialog(const char *dir)
{
  ImGuiFileDialog *ret = new ImGuiFileDialog;
  // set bookmarks.  4 standard locations + persistent user entries
  const char *bm = state_val(dir);
  if(bm && *bm)
    ret->DeserializeBookmarks(bm);
  ret->AddBookmark("Here", ".");
  ret->AddBookmark(dir, dir);
  int slen = strlen(dir);
  char buf[slen + 4];
  memcpy(buf, "Aq/", 3);
  strcpy(buf + 3, dir);
  ret->AddBookmark("User", find_xdg_file(XDG_CONFIG, buf, NULL));
  char sbuf[slen + sizeof(SYS_DIR) + 1];
  memcpy(sbuf, SYS_DIR, sizeof(SYS_DIR) - 1);
  sbuf[sizeof(SYS_DIR) - 1] = '/';
  strcpy(sbuf + sizeof(SYS_DIR), dir);
  ret->AddBookmark("System", sbuf);
  return ret;
}

static void finish_fdialog(ImGuiFileDialog *dlg, const char *dir)
{
  // FIXME: retain path (probably need one per file type)
  // filePath = dlg->GetCurrentPath();
  // filter = dlg->GetCurrentFilter();
  string save = dlg->SerializeBookmarks(true);
  const char *oldb = state_val(dir);
  if((save.size() && !oldb) || (oldb && strcmp(oldb, save.c_str())))
    set_imgui_state(dir, save.c_str());
  dlg->Close();
}

// FIXME: file name is mandatory
bool _askrom_noreset = false;

void askrom(romdesc *rom)
{
  char *wdbuf = NULL;
  // Where did current ROM come from?
  // if not there, Qt defaults to cur dir
  const char *wd = file_from(rom->where, "roms", &wdbuf);
  string cap(rom->what), fname(rom->where ? rom->where : "");
  cap += " ROM File";
  // v0.6.5 case-insensitive ignores last item in group, so repeat 1st
  // easy to fix/patch, but I prefer keeping it pristine
  _rdlg->OpenDialog("askrom", cap, "ROMs{.rom,.bin,.rom},All{(())}", wd, fname, 1, rom,
		    // What an obnoxious flag naming convention
		    ImGuiFileDialogFlags_Modal|
		    ImGuiFileDialogFlags_CaseInsensitiveExtention);
  if(wdbuf)
    free(wdbuf);
}

static void process_askrom(int w, int h)
{
  if(_rdlg && _rdlg->Display("askrom", ImGuiWindowFlags_NoCollapse,
			     ImVec2(w*0.6, h*0.6))) {
    if(_rdlg->IsOk()) {
      romdesc *rom = (romdesc *)_rdlg->GetUserDatas();
      auto sel = _rdlg->GetFilePathName();
      if(!rom->where != !sel.size() || (rom->where && sel != rom->where)) {
	if(rom->pref) {
	  set_config_str(rom->pref, sel.c_str());
	  rom->where = config_str(rom->pref);
	} else {
	  rom->buf = sel;
	  if(sel.size())
	    rom->where = rom->buf.c_str();
	  else
	    rom->where = NULL;
	}
      }
      if(!_askrom_noreset)
	_doreset = true;
    }
    _askrom_noreset = false;
    finish_fdialog(_rdlg, "roms");
  }
}

void asktape()
{
  char *wdbuf = NULL;
  const char *wd = find_xdg_file(XDG_DATA, "Aq/tapes", NULL);;
  if(_tape)
    wd = file_from(_tape, "tapes", &wdbuf);
  if(!_tdlg)
    _tdlg = new ImGuiFileDialog;
  // regex pointlessly needs double paren around it.
  // regex parser doesn't support internal parens.  No way to really fix this.
  // (should be ..|\.(tape|caq)$)
  _tdlg->OpenDialog("asktape", "Tape File", "Tapes{((^tape$|\\.tape$|\\.caq$|\\.TAPE$|\\.CAQ$))},All{(())}", wd, _tape ? _tape : "", 1, &_tape,
		    // What an obnoxious flag naming convention
		    ImGuiFileDialogFlags_Modal|
		    ImGuiFileDialogFlags_CaseInsensitiveExtention);
  if(wdbuf)
    free(wdbuf);
}

static void process_asktape(int w, int h)
{
  if(_tdlg && _tdlg->Display("asktape", ImGuiWindowFlags_NoCollapse,
			     ImVec2(w*0.6, h*0.6))) {
    if(_tdlg->IsOk()) {
      auto sel = _tdlg->GetFilePathName();
      if(!_tape != !sel.size() || (_tape && sel != _tape))
	// FIXME: leaks _tape
	_tape = strdup(sel.c_str());
    }
    finish_fdialog(_tdlg, "tapes");
    asked_tape();
  }
}

void askbasic(void)
{
  if(!_bdlg)
    _bdlg = new ImGuiFileDialog;
  // v0.6.5 case-insensitive ignores last item in group, so repeat 1st
  // easy to fix/patch, but I prefer keeping it pristine
  _bdlg->OpenDialog("askbas", "BASIC File", "BASIC{.bas,.txt,.bas},All{(())}", ".", "", 1, &_tape,
		    // What an obnoxious flag naming convention
		    ImGuiFileDialogFlags_Modal|
		    ImGuiFileDialogFlags_CaseInsensitiveExtention);
}

static void process_askbasic(int w, int h)
{
  if(_bdlg && _bdlg->Display("askbas", ImGuiWindowFlags_NoCollapse,
			     ImVec2(w*0.6, h*0.6))) {
    if(_bdlg->IsOk()) {
      auto sel = _bdlg->GetFilePathName();
      if(sel.size())
	load_basic(sel.c_str());
    }
    finish_fdialog(_bdlg, "bas");
  }
}

static bool _show_romdlg;

void askroms(void)
{
  // This should just be OpenPopup(), but it has to be done
  // below to get the ID right
  // Well, actually, the file dialog quits the popup, so this isn't a popup
  _show_romdlg = true;
}

static void dorom(romdesc *rom, const char *def,
		  const char *chk = NULL, bool *bv = NULL)
{
  // For some reason, there is a wide gap between the text entry and the button.
  // For that reason, I'm abandoning Table.
#define USE_TABLES 0
#if USE_TABLES
  TableNextRow();
  TableNextColumn();
#else
  auto rem = GetContentRegionAvail();
  unsigned rw = rem.x > 0 ? (unsigned)rem.x : 0;
#endif
  AlignTextToFramePadding();
  Text("%s ROM", rom->what);
#if USE_TABLES
  TableNextColumn();
#else
  SameLine();
  // check lab len instead of x for smoother resize
  // otherwise, there is a noticable jump  of all text widgets to left before
  // re-stabilizing at the correct x.
  // rendered text len would be better, but strlen is probably good enough
  // at least until internationalization
  static unsigned max_ll = 0;
  unsigned ll = strlen(rom->what);
  if(ll > max_ll)
    max_ll = ll;
  static unsigned text_size = 0, prev_w = 0;
  unsigned x = floor(GetCursorPosX());
  if(ll == max_ll)
    text_size = x;
  Dummy(ImVec2(text_size - x, 0));
  SameLine();
#endif
  // blank label not supported; otherwise absorbs next widget into focus group
  // Maybe adding a dummy after it would work as well, but a space works.
  string lab(" ##");
  lab += rom->what;
#if !USE_TABLES
  static unsigned itsize = 200;
  SetNextItemWidth(itsize);
#endif
  InputTextWithHint(lab.c_str(), def, &rom->buf);
#if USE_TABLES
  TableNextColumn();
#else
  SameLine();
#endif
  lab = "..." + lab.substr(1);
  // This button won't respond until the text entry box is selected if the
  // text entry's label is blank or just a hashval.
  if(Button(lab.c_str())) {
    _askrom_noreset = true;
    askrom(rom); // ??? this pops down the dialog if it's a poup.
  }
#if USE_TABLES
  TableNextColumn();
#else
  SameLine();
  unsigned cbw = floor(GetCursorPosX());
#endif
  if(chk)
    Checkbox(chk, bv);
#if USE_TABLES
  else
    // If you really wanted me to use this more, you
    // should've named it something shorter
    // e.g. TextRaw
    TextUnformatted("");
#else
  if(chk)
    SameLine();
  static unsigned mcb;
  x = floor(GetCursorPosX());
  NewLine();
  cbw = x - cbw;
  if(rw != prev_w || cbw > mcb)
    mcb = cbw;
  if(cbw == mcb && rw != x)
    itsize += (int)rw - x;
  prev_w = rw;
#endif
}

static void process_romdlg(int w, int h)
{
  static ImVec2 dim;
  if(!dim.x)
    dim = ImVec2(w*2/3,h/3);
  static const char *grom_tmp, *mrom_tmp, *crom_tmp, *ebrom_tmp;
  static romdesc grom = { .where = grom_tmp };
  static romdesc mrom = { .where = mrom_tmp };
  static romdesc crom = { .where = crom_tmp };
  static romdesc ebrom = { .where = ebrom_tmp };
  static bool showing = false;
  static bool save_gr, use_eb;
  if(_show_romdlg) {
    // This has to be done here to get the ID right
    // OpenPopup("Select ROMs");
    // but file dialog closes this, so it's a window now.
    if(!grom.what) {
      grom.what = _gamerom_desc.what;
      mrom.what = _mainrom_desc.what;
      crom.what = _charrom_desc.what;
      ebrom.what = _ebrom_desc.what;
    }
    grom.buf = _gamerom_desc.where ? _gamerom_desc.where : "";
    save_gr = false;
    mrom.buf = _mainrom_desc.where;
    crom.buf = _charrom_desc.where;
    ebrom.buf = _ebrom_desc.where;
    use_eb = _extendedbasic;
    _show_romdlg = false;
    showing = true;
  }
  if(showing) {
    ImVec2 center = GetMainViewport()->GetCenter();
    SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    SetNextWindowSizeConstraints(ImVec2(w/3,dim.y), ImVec2(w,dim.y));
    SetNextWindowSize(dim, ImGuiCond_Appearing);
  }
  // This can't be a popup.
  // Modal is an "internal only" flag
  if(showing && Begin("Select ROMs", NULL, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoCollapse)) {
#if USE_TABLES
    if(BeginTable("table1", 4)) {
      TableSetupColumn("lab", ImGuiTableColumnFlags_WidthFixed);
      TableSetupColumn("input", ImGuiTableColumnFlags_WidthStretch);
      TableSetupColumn("but", ImGuiTableColumnFlags_WidthFixed);
      TableSetupColumn("check", ImGuiTableColumnFlags_WidthFixed);
#endif
      dorom(&grom, "None if blank", "Save", &save_gr);
      dorom(&mrom, DEFAULT_MAINROM);
      dorom(&crom, DEFAULT_CHARROM);
      dorom(&ebrom, DEFAULT_EBROM, "Use", &use_eb);
#if USE_TABLES
      EndTable();
    }
#endif
    Separator();
    auto rem = GetContentRegionAvail();
    // Probably not good to make Enter accept by default.
    static bool /* pressed_enter = false, */ pressed_esc = false;
    if(Button("Cancel", ImVec2(120, 0)))
      // CloseCurrentPopup();
      showing = false;
    SameLine();
    rem.x -= GetCursorPosX();
    if(rem.x > 120)
      Indent(rem.x);
    // SetKeyboardFocusHere();
    if(Button("OK/Reset", ImVec2(120,0))) {
      // CloseCurrentPopup();
      showing = false;
      ud_rom(_gamerom_desc, grom, NULL);
      if(save_gr)
	set_config_str("cartridge", _gamerom_desc.where);
      ud_rom(_mainrom_desc, mrom, DEFAULT_MAINROM);
      ud_rom(_charrom_desc, crom, DEFAULT_CHARROM);
      ud_rom(_ebrom_desc, ebrom, DEFAULT_EBROM);
      if(use_eb != _extendedbasic)
	_doreset = true;
      _extendedbasic = use_eb;
      set_config_bool("use_ebasic", use_eb);
    }
    // SetItemDefaultFocus();
    if(IsWindowFocused()) {
      // bool enter = IsKeyPressed(ImGuiKey_Enter);
      bool esc = IsKeyPressed(ImGuiKey_Escape);
      // if(enter)
      //   pressed_enter = true;
      if(esc)
	pressed_esc = true;
      if(/*(pressed_enter && !IsKeyDown(ImGuiKey_Enter)) || */
	 (pressed_esc && !IsKeyDown(ImGuiKey_Escape))) {
	/* pressed_enter = */ pressed_esc = false;
	// CloseCurrentPopup();
	showing = false;
      }
    }
    rem = GetContentRegionAvail();
    if(rem.y != 0) {
      dim.y -= rem.y;
      SetWindowSize(dim);
    }
    End();
  }
}

/********* Main window ***********/

void win_title(const char *t)
{
  SDL_SetWindowTitle(_window, t);
}

static void copy_noamp(char *d, const char *s)
{
  do {
    if(*s == '&' && s[1])
      s++;
    *d++ = *s;
  } while(*s++);
}

static bool calc_ul(const char *namp, const char *raw,
		    ImVec2 &start, ImVec2 &end, double scale)
{
  // yet more internals, just to draw the underline
  auto dc = GetCurrentWindow()->DC;
  const ImGuiMenuColumns* offsets = &dc.MenuColumns;
  const char *keyoff = strchr(raw, '&'), *nae = namp + (keyoff - raw);;
  while(keyoff && keyoff[1] == '&') {
    nae--;
    keyoff = strchr(keyoff + 2, '&');
  }
  if(!keyoff)
    return false;
  start = ImVec2(dc.CursorPos.x + offsets->OffsetLabel, dc.CursorPos.y);
  start += CalcTextSize(namp, nae, true);
  start.y += GetFont()->Descent * scale;
  end = start;
  ImVec2 key_size = CalcTextSize(keyoff + 1, keyoff +2, true);
  end.x += key_size.x;
  return true;
}

static void popup_menu(bool horiz)
{
  if(!_menu_hot) {
    for(_num_menus = 0; actions[_num_menus].parent; _num_menus++);
    _menu_hot = (menukey *)malloc(_num_menus * sizeof(*_menu_hot));
    const char *prev = NULL;
    char hkey = 0;
    for(int i = 0; i < _num_menus; i++) {
      const char *p = actions[i].parent;
      if(!prev || strcmp(p, prev)) {
	prev = p;
	const char *h;
	while(1) {
	  h = strchr(p, '&');
	  if(h && h[1] != '&')
	    break;
	  h += 2;
	}
	if(h)
	  hkey = toupper(h[1]);
      }
      _menu_hot[i].pkey = hkey;
      const char *h;
      while(1) {
	h = strchr(p, '&');
	if(h && h[1] != '&')
	  break;
	h += 2;
      }
      _menu_hot[i].key = h ? toupper(h[1]) : 0;
      _menu_hot[i].pactive = false;
      _menu_hot[i].active = false;
    }
  }
  const char *cur = NULL;
  bool is_act = false, any_act = false;
  char mbuf[16], ibuf[64];
  auto col = GetColorU32(ImGuiCol_Text);
  double scale = GetCurrentWindow()->FontWindowScale;
  auto dl = GetWindowDrawList();
  for(int i = 0; i < _num_menus; i++) {
    const char *p = actions[i].parent;
    if(!cur || strcmp(cur, p)) {
      if(is_act)
	EndMenu();
      copy_noamp(mbuf, cur = p);
      ImVec2 start, end;
      calc_ul(mbuf, p, start, end, scale);
      if(horiz) {
	float off = IM_FLOOR(GetStyle().ItemSpacing.x * 0.5f);
	start.x += off;
	end.x += off;
      }
      if(_menu_hot[i].pactive) {
        OpenPopup(mbuf);
	_menu_hot[i].pactive = false;
      }
      is_act = BeginMenu(mbuf);
      if(is_act) {
	any_act = true;
	_cur_menu = i;
      }
      dl->AddLine(start, end, col, scale);
    }
    if(!is_act)
      continue;
    copy_noamp(ibuf, actions[i].name);
    ImVec2 start, end;
    bool draw_ul = calc_ul(ibuf, actions[i].name, start, end, scale);
    bool act = actions[i].toggle ?
      MenuItem(ibuf, actions[i].def_binding, actions[i].toggle) :
      MenuItem(ibuf, actions[i].def_binding);
    if(!act && _menu_hot[i].active) {
      _menu_hot[i].active = false;
      act = true;
      CloseCurrentPopup();
      if(actions[i].toggle)
	*actions[i].toggle ^= true;
    }
    if(act) {
      actions[i].f();
      if(actions[i].toggle && actions[i].pref)
	set_config_bool(actions[i].pref, *actions[i].toggle);
    } else if(draw_ul)
      GetWindowDrawList()->AddLine(start, end, col, scale);

  }
  if(is_act)
    EndMenu();
  if(!any_act) {
    _tmp_show_menubar = false;
    _cur_menu = -1;
  }
}

void toggle_menu()
{
}

static void process_bindkey(int w, int h);

void imgui_update(unsigned w, unsigned h, unsigned long texid)
{
  pthread_mutex_lock(&imgui_lock);
  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  NewFrame();

  SetNextWindowPos(ImVec2(0, 0));
  SetNextWindowSize(ImVec2(w, h));
  uint32_t bg, fg;
  color(_bordercolor, &fg, &bg);
  bg = (bg & 0xff00ff00) + ((bg & 0xff0000) >> 16) +
       ((bg & 0xff) << 16);
  PushStyleColor(ImGuiCol_WindowBg, bg);
  if(Begin("mainwin", NULL,
	   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
	   (_tmp_show_menubar || _show_menubar ? ImGuiWindowFlags_MenuBar : 0))) {
    if((_tmp_show_menubar || _show_menubar) && BeginMenuBar()) {
      popup_menu(true);
      EndMenuBar();
    }
    exec_actions(true);
    // FIXME: aspect correct?  4:3/narrow pixels?  Or square (5:3)?
    auto dim = GetContentRegionAvail();
    auto pos = GetCursorScreenPos();
    if(dim.x > dim.y * 4 / 3) {
      // Dummy(ImVec2((dim.x - dim.y * 4 / 3) / 2, 0));
      SameLine((dim.x - dim.y * 4 / 3) / 2);
      dim.x = dim.y * 4 / 3;
    } else if(dim.y > dim.x * 3 / 4) {
      Dummy(ImVec2(0, (dim.y - dim.x * 3 / 4) / 2));
      dim.y = dim.x * 3 / 4;
    }
    // I don't want borders.  It's possible to remove the frame border:
#if 0
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // And, using an internal, undocumented feature, the nav border:
    GetCurrentWindow()->DC.NavHideHighlightOneFrame = true;
    // There really ought to be a way to disable this in the real API.
    ImageButton((ImTextureID)texid, dim);
    PopStyleVar();
#else
    // But in the end, I decided not to use ImageButton().  Instead,
    // I use an InvisibleButton with the image overlayed.  This should
    // avoid any automatic graphical modification entirely.
    // Besides, I can make it span the entire window.
    Image((ImTextureID)texid, dim);
    SetCursorScreenPos(pos);
    InvisibleButton("##rwin", ImVec2(-FLT_MIN, -FLT_MIN));
#endif
    _rwinid = GetItemID(); // shouldn't change after first time
    if(BeginPopupContextItem()) {
      popup_menu(false);
      EndPopup();
    }
  }
  PopStyleColor();
  End();

  // Dialogs must be handled separately
  process_askrom(w, h);
  process_asktape(w, h);
  process_askbasic(w, h);
  process_about(w, h);
  process_romdlg(w, h);
  process_bindkey(w, h);

  // Cpu Zone : prepare
  Render();

  // GPU Zone : Rendering
  ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());

  // Save config changes
  ImGuiIO& io = GetIO();
  if(io.WantSaveIniSettings) {
    set_imgui_state("winstate", SaveIniSettingsToMemory());
    io.WantSaveIniSettings = false;
  }

  pthread_mutex_unlock(&imgui_lock);
}

void imgui_end(void)
{
  pthread_mutex_lock(&imgui_lock);
  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  DestroyContext();
  pthread_mutex_unlock(&imgui_lock);
}

/*********  Binding popup *********/

static bool _show_bindkey = false;
void bind_key(void)
{
  _show_bindkey = true;
}

static void select_keys(vector<actid_t> &acts, uint64_t &key_sel,
			uint16_t &but_sel, uint16_t &dir_sel);

static void process_bindkey(int w, int h)
{
  ImVec2 center = GetMainViewport()->GetCenter();
  SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImVec2 dim(w*2/3, h/2);
  SetNextWindowSize(dim, ImGuiCond_Appearing);
  static int act_sel;               // act tab state
  static bool onb, offb, prb;       // more act tab state
  static uint16_t but_sel, dir_sel; // joy tab state
  static uint64_t _U kb_sel;           // kb tab state
  if(_show_bindkey) {
    // This has to be done here to get the ID right
    OpenPopup("Bind Keys");
    _show_bindkey = false;
    act_sel = -1;
    but_sel = dir_sel = 0;
    kb_sel = 0;
  }
  // colors
  // not sure of the endianness, so just convert from ImVec4.
  static ImU32 black = ColorConvertFloat4ToU32(ImVec4(0,0,0,1)),
               blue = ColorConvertFloat4ToU32(ImVec4(0, 0, 1, 1)),
               white = ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)),
               yellow = ColorConvertFloat4ToU32(ImVec4(1, 1, 0, 1)),
               dyellow = ColorConvertFloat4ToU32(ImVec4(0.5, 0.5, 0, 1));
  // the width of the font
  auto em = CalcTextSize("M", NULL, true);
  float mw = em.x * 4 * 14, mh = GetTextLineHeightWithSpacing() * (8 + 2.1);
  SetNextWindowSizeConstraints(ImVec2(mw,mh), ImVec2(FLT_MAX, FLT_MAX));
  if(BeginPopupModal("Bind Keys", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    int prev_act = act_sel;
    bool prev_onb = onb, prev_offb = offb, prev_prb = prb;
    uint16_t prev_but = but_sel, prev_dir = dir_sel;
    uint64_t prev_kb = kb_sel;
    static vector<string> chords;
    static vector<vector<actid_t>> chord_acts;
    static int chord_sel = 0;
    bool allow_chord = GetIO().KeyShift;
    if(BeginTabBar("keytab")) {
      if(BeginTabItem("Actions")) { // &A
	if(BeginListBox("##actlist", ImVec2(-FLT_MIN, 5 * GetTextLineHeightWithSpacing()))) {
	  if(Selectable("##blank", act_sel < 0))
	    act_sel = -1;
	  SetItemDefaultFocus();
	  static const char **act_labels;
	  if(!act_labels) {
	    char mbuf[16], ibuf[64];
	    const char *cur = NULL;
	    unsigned cur_len = 0;
	    act_labels = (const char **)malloc(_num_actions * sizeof(*act_labels));
	    if(!act_labels) {
	      perror("action labels");
	      exit(1);
	    }
	    for(unsigned i = 0; i < _num_actions; i++) {
	      char *t = ibuf;
	      const char *p = actions[i].parent;
	      if(p) {
		if(!cur || strcmp(cur, p)) {
		  copy_noamp(mbuf, cur = p);
		  cur_len = strlen(mbuf) + 3;
		  strcpy(mbuf + cur_len - 3, " / ");
		}
		memcpy(ibuf, mbuf, cur_len);
		t = ibuf + cur_len;
	      }
	      copy_noamp(t, actions[i].name);
	      act_labels[i] = strdup(ibuf);
	      if(!act_labels[i]) {
		perror("action labels");
		exit(1);
	      }
	    }
	  }
	  for(unsigned i = 0; i < _num_actions; i++)
	    if(Selectable(act_labels[i], act_sel == (int)i))
	      act_sel = i;
	  EndListBox();
	  BeginDisabled(act_sel < 0 || !actions[act_sel].toggle);
	  Checkbox("On", &onb);
	  SameLine();
	  if(onb)
	    offb = false;
	  Checkbox("Off", &offb);
	  if(offb)
	    onb = false;
	  if(!onb && !offb)
	    prb = false;
	  SameLine();
	  Checkbox("While Pressed", &prb);
	  EndDisabled();
	  if(prb && !onb && !offb)
	    onb = true;
	}
	EndTabItem();
      }
      if(BeginTabItem("Controllers")) { // &J
	if(BeginTable("ctab", 5)) {
	  TableSetupColumn("pad1", ImGuiTableColumnFlags_WidthStretch);
	  TableSetupColumn("joy1", ImGuiTableColumnFlags_WidthFixed);
	  TableSetupColumn("pad2", ImGuiTableColumnFlags_WidthStretch);
	  TableSetupColumn("joy2", ImGuiTableColumnFlags_WidthFixed);
	  TableSetupColumn("pad3", ImGuiTableColumnFlags_WidthStretch);
	  TableNextRow();
	  for(unsigned j = 0, m = 1; j < 2; j++, m <<= 8 - 6) {
	    PushID(j);
	    TableSetColumnIndex(j * 2 + 1);
	    TableSetBgColor(ImGuiTableBgTarget_CellBg, dyellow);
	    char bname[4] = "   ";
	    for(unsigned b = 0; b < 6; b++, m <<= 1) {
	      PushStyleColor(ImGuiCol_Button, (but_sel & m) ? yellow : blue);
	      PushStyleColor(ImGuiCol_Text, (but_sel & m) ? blue : white);
	      bname[1] = '1' + b;
	      if(Button(bname, ImVec2(em.x * 3, 0))) {
		// FIXME: drop keys unless left-clicked
		but_sel ^= m;
	      }
	      if((b + 1) % 3)
		SameLine();
	      PopStyleColor(2);
	    }
	    Spacing();
	    auto rem = GetContentRegionAvail();
	    float r = rem.x / 2;
	    bool act = InvisibleButton("##dial", ImVec2(rem.x, rem.x));
	    auto c = GetItemRectMin(); // , br = GetItemRectMax();
	    // auto c = ul;
	    c.x += r;
	    c.y += r;
	    auto dl = GetWindowDrawList();
	    dl->AddCircleFilled(c, r, black);
	    uint8_t dir = dir_sel >> (j ? 8 : 0);
	    if(act) {
	      // FIXME: drop keys unless left-clicked
	      /* compute direction index */
	      // uint8_t odir = dir;
	      auto mpos = GetMousePos();
	      double x = (mpos.x - c.x) / r, y = (mpos.y - c.y) / r;
	      double cr = x * x + y * y;
	      if(cr > 1 || cr < 0.1)
		dir = 0; // Deselect if outside circle or too close to center
	      else {
		/* from aqjoy.c */
		/* atan(y/x) == -pi/2 .. pi/2 -> -4 .. 4 */
		double th = x ? atan((double)y/x) / M_PI * 8.0 : y > 0 ? -4.0 : 4.0;
		/* correction for quadrant 2-3 */
		if(x < 0)
		  th += 8.0; // -4 .. 12
		int ith = (int)round(th) & 15;
		/* end stuff from aqjoy.c */
		dir = ith + 1;
	      }
	      if(j)
		dir_sel = (dir_sel & 0x00ff) | ((uint16_t)dir << 8);
	      else
		dir_sel = (dir_sel & 0xff00) | dir;
	    }
	    if(dir) {
	      float a = 2*M_PI / 16 * (dir - 1) - 2*M_PI / 32;
	      dl->PathClear();
	      dl->PathArcTo(c, r, a, a + (2 * M_PI / 16));
	      dl->PathLineTo(c);
	      // Luckily, I don't have to compute the way back to the arc
	      // The last line is automatic.
	      dl->PathFillConvex(white);
	    }
	    PopID();
	  }
	  EndTable();
	}
	EndTabItem();
      }
      if(BeginTabItem("Keyboard")) { // &K
	uint64_t m = 1;
	for(int r = 0, i = 0; r < 4; r++) {
	  int adj = r > 1 ? 1 : 0;
	  Dummy(ImVec2((r - adj) * em.x, 0));
	  SameLine();
	  PushID(r);
	  if(!BeginTable("krow", _row_nkeys[r]))
	    continue;
	  TableNextRow();
	  PushStyleColor(ImGuiCol_Text, blue);
	  PushFont(_small);
	  for(int c = 0; c < _row_nkeys[r]; c++, i++) {
	    TableNextColumn();
	    if(aqkeys_ctl[i])
	      TextUnformatted(aqkeys_ctl[i]);
	  }
	  PopFont();
	  PopStyleColor();
	  i -= _row_nkeys[r];
	  for(int c = 0; c < _row_nkeys[r]; c++, i++, m <<= 1) {
	    TableNextColumn();
	    PushStyleColor(ImGuiCol_Button, (kb_sel & m) ? yellow : i ? blue : white);
	    PushStyleColor(ImGuiCol_Text, !i || (kb_sel & m) ? blue : white);
	    // FIXME: aqkeys_shift should be next to it and up 1/2 char
	    // unless it's i == 11 or i == 12, where they are on top of each other
	    if(Button(aqkeys[i], ImVec2(em.x * (4 + (c ? 0 : adj * 2)), 0)))
	      kb_sel ^= m;
	    PopStyleColor(2);
	  }
	  EndTable();
	  PopID();
	}
	EndTabItem();
      }
      EndTabBar();
    }
    TextUnformatted("Chords");
    SameLine();
    if(!chords.size())
      BeginDisabled();
    if(BeginCombo(" ##ch", chords.size() ? chords[chord_sel].c_str() :
		                           "No key selected")) {
      for(int i = 0; i < (int)chords.size(); i++)
	if(Selectable(chords[i].c_str(), i == chord_sel) && i != chord_sel) {
	  select_keys(chord_acts[i], kb_sel, but_sel, dir_sel);
	  allow_chord = true;
	}
      EndCombo();
    }
    if(!chords.size())
      EndDisabled();
    // FIXME: parse & save changes before switching selection
    // If there's a parse error, pop up dialog and keep old selection
    // Apply immediately to system, but and save in temp. changes log
    if((kb_sel ^ prev_kb) & 1) {
      but_sel = dir_sel = 0;
      kb_sel &= 1ULL;
      prev_kb = kb_sel;
      act_sel = kb_sel ? _reset_act : -1;
    }
    if(act_sel != prev_act) {
      chords.clear();
      chord_acts.clear();
      but_sel = dir_sel = 0;
      kb_sel = 0;
      if(act_sel == _reset_act)
	kb_sel = 1ULL;
    } else if(but_sel != prev_but || dir_sel != prev_dir || kb_sel != prev_kb) {
      chords.clear();
      chord_acts.clear();
      act_sel = -1;
      kb_sel &= ~1ULL;
      if(!allow_chord) {
	uint64_t newk = kb_sel ^ prev_kb;
	// keyboard changes retain shift/ctl for chording.
	// technically, the Aquarius only supports one or the other, though
	if(newk != (1ULL<<KEY_SHIFT) && newk != (1ULL<<KEY_CTL))
	  kb_sel &= ~prev_kb | (1ULL << KEY_SHIFT) | (1ULL << KEY_CTL);
	but_sel &= ~prev_but;
	if((prev_dir & 0xff) == (dir_sel & 0xff))
	  dir_sel &= 0xff00;
	if((prev_dir >> 8) == (dir_sel >> 8))
	  dir_sel &= 0xff;
      }
    }
    TextUnformatted("Binding");
    SameLine();
    static string bind;
    if(act_sel != prev_act || onb != prev_onb || offb != prev_offb ||
       prb != prev_prb || but_sel != prev_but || dir_sel != prev_dir ||
       kb_sel != prev_kb) {
      actid_t aid = 0;
      vector<actid_t> chord;
      if(act_sel >= 0) {
	aid = act_sel + 1;
	if(actions[act_sel].toggle) {
	  if(onb)
	    aid |= 0x140;
	  else if(offb)
	    aid |= 0x100;
	  if(prb)
	    aid |= 0x80;
	}
	char *b = bound_keys(aid);
	bind = b ? b : "";
	free(b);
      } else if(kb_sel > 1 || but_sel || dir_sel) {
	int kbs = 0, buts = 0;
	uint64_t kbm = 0;
	uint16_t butm = 0;
	if(kb_sel)
	  for(kbm = 1; ++kbs, !(kb_sel & kbm); kbm <<= 1);
	if(but_sel)
	  for(butm = 1; ++buts, !(but_sel & butm); butm <<= 1);
	if(kb_sel && !but_sel && !dir_sel && !(kb_sel ^ (1ULL << (kbs - 1))))
	  aid = parse_action(aqkeys[kbs - 1]);
	else if(but_sel && !kb_sel && !dir_sel && !(but_sel ^ (1U << (buts - 1))))
	  aid = JC_BUT(buts >= 8 ? 2 : 1, buts & 7);
	else if(!but_sel && !kb_sel && (dir_sel & 0xff) && !(dir_sel & 0xff00))
	  aid = JC_AX(1, (dir_sel & 0xff) - 1);
	else if(!but_sel && !kb_sel && !(dir_sel & 0xff) && (dir_sel & 0xff00))
	  aid = JC_AX(2, (dir_sel >> 8) - 1);
	else {
	  while(kbm && kbm <= kb_sel) {
	    chord.push_back(parse_action(aqkeys[kbs - 1]));
	    for(kbm <<= 1, ++kbs; kbm < kb_sel && !(kbm & kb_sel); kbm <<= 1, ++kbs);
	  }
	  while(butm && butm <= but_sel) {
	    chord.push_back(JC_BUT(buts >= 8 ? 2 : 1, buts & 7));
	    for(butm <<= 1, ++buts; butm < but_sel && !(butm & but_sel); butm <<= 1, ++buts);
	  }
	  if(dir_sel & 0xff)
	    chord.push_back(JC_AX(1, (dir_sel & 0xff) - 1));
	  if(dir_sel & 0xff00)
	    chord.push_back(JC_AX(2, (dir_sel >> 8) - 1));
	  int cid = chord_id_of(chord);
	  if(cid >= 0)
	    aid = 0x10000 + cid;
	}
	if(aid || chord.size()) {
	  if(!chord.size())
	    chord = {aid};
	  chord_acts = possible_chords(chord);
	  for(auto c : chord_acts) {
	    if(c == chord)
	      chord_sel = chords.size();
	    string e;
	    for(auto k : c) {
	      char *s = action_to_string(k);
	      if(e.size())
		e += '+';
	      e += s;
	      free(s);
	    }
	    chords.push_back(e);
	  }
	}
      }
      if(aid) {
	char *b = bound_keys(aid);
	bind = b ? b : "";
	free(b);
      } else
	bind = "";
    }
    // FIXME: stretch to right side - buttons
    // FIXME: intelligent editing:
    //   deleting part of mod deletes all of mod
    //     in wxvbam, I disallowed editing the text, and instead deleted
    //     the last binding if backspace is entered, and otherwise add
    //     whatever was pressed immediately
    //   deleting a binding should also delete separator, if present
    //   double-clicking on text should select an entire binding
    InputText(" ##binding", &bind);
    SameLine();
    if(Button("Key..."/*, ImVec2(6 * em.x, 0)*/)) {
      // FIXME:  pop up grab dialog to request an SDL event group
      // dialog:
      //  <text> == captured events so far; can scroll if necessary
      //  4 rows of 3 checkboxes each: ignore mod, ignore lmod, ignore rmod
      //  Cancel/Add/Find buttons @ bottom
      //    Add appends to _bindstr; Find selects current binding, if any
      //    Should do a find anyway, and warn if rebinding
    }
    // following should grab an SDL key and either say "not found" or
    // update entire dialog to the binding
    // same dialog as above, but different function
    // maybe merge into "Key..." button that has Add/Find buttons
    // following 2 should display list of changes to config in confirm popup
    // Also, flush right.  Or Do cancel on left, flush ok right.
    // I have no idea what the various UI standards want these days, and I'm
    // not sure I care.  I won't make this platform-specific, although the qt
    // version is automatically platform-specific (and awful -- who decided
    // flushing both buttons right was a good idea?)
    if(Button("OK"/*, ImVec2(6 * em.x, 0)*/)) {
      // if(!same_bindings) {
      //   // popup("Are you sure?");
      //   merge_tmp_keys_with_keys();
      //   save_config();
      // }
      CloseCurrentPopup();
    }
    SameLine();
    if(Button("Cancel"/*, ImVec2(6 * em.x, 0)*/)) {
      // if(!same_bindings()) {
      //   popup("Are you sure?");
      //   clear_bindings();
      //   add_sdl_bindings();
      // }
      CloseCurrentPopup();
    }
    EndPopup();
  }
}

static void select_keys(vector<actid_t> &acts, uint64_t &kb_sel,
			uint16_t &but_sel, uint16_t &dir_sel)
{
  kb_sel = 0;
  but_sel = dir_sel = 0;
  for(auto k : acts) {
    if(k < 0)
      k = -k;
    // plain key: 0x200 + <keyid> (_keys[] array index)
    if(k < 0x400) {
      char *s = action_to_string(k);
      s[0] = toupper(s[0]);
      for(unsigned i = 1; i < NKEYS; i++)
	if(!strcmp(aqkeys[i], s)) {
	  kb_sel |= 1ULL << i;
	  break;
	}
      free(s);
      continue;
    }
    // plain joy button: 0x400 + 0x100 * <joy> + <but>
    // plain joy dir: KV_AX + 0x1000 * <dir#> + 0x800 + 0x100 * <joy>
    uint16_t val = (k < 0x800 ? k : k >> 12) & 0xf;
    if(k & 0x100)
      val <<= 8;
    if(k <  0x800)
      but_sel |= 1 << val;
    else
      dir_sel |= val;
  }
}
