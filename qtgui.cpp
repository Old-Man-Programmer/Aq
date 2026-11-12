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
 * All Qt-related code is in here.
 *
 * Known issues:
 *  - keybinding dialog doesn't even save size/loc between invocations
 *
 * Missing features for first pass:
 *   - working binding editor
 *     - joystick should limit growth to keep circle size in check
 *   - for game: "Load Config" checkbox, default checked
 *     (or Yes/No/Always/Never)
 */
#include <QtWidgets> // Uses K; done first to avoid conflict w/ Aq.h
extern "C" {
#include "Aq.h"
}
#include "gui_common.h"

/* The following is a mixture of C and C++ sytles.  I do not apologize, and
 * I do not subscribe to whatever you think is the proper, clean way to do
 * C++.  If you wanted me to use crap like reinterpret_cast, you'd have made
 * it easier to type.  The C++ language includes a preprocessor, whether you
 * like it or not.  I'm also not a big fan of Qt's design decisions; see my
 * associated rant in grok.  In summary, it's valid C++ code (compiles
 * without error or warning on modern compilers).  Deal with it.
 *
 * As I do not meticulously track standards any more, it's possible you need
 * new(er) versions of C++.  Just use gcc and/or clang like everyone else.
 * At the time of writing, I am using g++/gcc-12 and clang++/llvm-14.
 */

// Need event filter to pass Qt events to SDL
class SDLEvWindow : public QMainWindow {
   protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};
static SDLEvWindow *_win;
// Main widget advertises current size as preferred size
// Now using QOpenGLWidget
class MainWidget : public QOpenGLWidget, protected QOpenGLFunctions {
   public:
    MainWidget() : QOpenGLWidget(), QOpenGLFunctions() {};
    QSize sizeHint() const override { QWidget::sizeHint(); return size(); };
   protected:
    void initializeGL() override;
    // void resizeGL(int w, int h) override;
    void paintGL() override;
   private:
    QTimer *timer;
};
static MainWidget *_cw;

/* Change title without C++ */
void win_title(const char *t)
{
  _win->setWindowTitle(t);
  /* FIXME: I also set icon title in grok; do I need that here? */
  // _win->setWindowIconText(t);
}

// Yeah, I just copied this from grok.  Whatever.  I wrote that, too.
#if 1 // I was going to use markdown, but Qt's markdown support sucks
static const char about_message[] =
  "<div style=\"white-space: pre;\">\n"
  "Mattel <b>Aq</b>uarius Emulator\n"
  "Version " VERSION "\n"
  "Compiled " __DATE__ "\n\n"
  "Author: Steve Baker &lt;steve.baker.llc@gmail.com&gt;\n"
  /* FIXME:  the homepage should be more specific */
  "Homepage: <a href=\"https://oldmanprogrammer.net\">https://oldmanprogrammer.net</a>\n"
  "With help from\nThomas J. Moore &lt;darktjm@gmail.com&gt;\n"
  // FIXME: add repository link & issue tracker link
  "</div>";
#endif

void show_about()
{
#if 0
  // about() only supports Qt::auto-text, so I have to convert to HTML
  QTextDocument d;
  d.setMarkdown(about_message);
  auto h = d.toHtml();
  // style="whitespace: pre" worked for HTML version:
  h.replace("pre-wrap", "pre");
  // But it still wraps lines.  Why?
  // Removing the superfluous span does't help
  h.replace(QRegularExpression("(<a [^>]*>)<span [^>]*>(.*?)</span>(</a)"), "\\1\\2\\3");;
  // Removing the superfluous per-pargraph style doesn't help
  h.replace(QRegularExpression("<p [^>]*>"), "<p style=\"white-space: pre;\">");
  fputs(h.toUtf8(), stderr);
#else
  // Since I can't convert md to HTML reasonably well, just use HTML
  QString h(about_message);
#endif
  QMessageBox::about(_win, "About Aq", h);
}

static QMenu *_cmenu; // Context menu

/* Local popups are mostly declared in gui_common.h now */

/* Copied from grok: resize main window to fit current SDL win size */
static void adj_mainwin(void)
{
  QLayout *l = _win->layout();
  l->setSizeConstraint(QLayout::SetFixedSize);

  _win->adjustSize();

  /* Now that the window is the correct size, make it resizable again */
  l->setSizeConstraint(QLayout::SetDefaultConstraint);
  _win->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

void toggle_menu()
{
  _win->menuBar()->setVisible(_show_menubar);
  adj_mainwin();
}

void set_fullscreen()
{
  if(_fullscreen)
    _win->showFullScreen();
  else
    _win->showNormal();
}

static void load_state(void);
static pthread_mutex_t _qtlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _qtsig = PTHREAD_COND_INITIALIZER;
static bool _started;

static QList<QAction *> _qactions;

void toggle_menu(int act)
{
    _qactions[act]->setChecked(*actions[act].toggle);
}

static void *qt_events(void *parm)
{
  int *wh = (int *)parm;
  int w = wh[0], h = wh[1];
  auto _qtapp = new QApplication(_argc, _argv);
  load_state();
  _win = new SDLEvWindow();
  win_title("Aquarius");
  auto menubar = _win->menuBar();
  _cmenu = new QMenu(_win);
  QMenu *menu = NULL;
  const char *curm = NULL;
  for(unsigned i = 0; actions[i].parent; i++) {
    if(!menu || strcmp(curm, actions[i].parent)) {
      curm = actions[i].parent;
      menu = _cmenu->addMenu(actions[i].parent);
      menubar->addMenu(menu);
      menu->setTearOffEnabled(true);
    }
    QAction *act = new QAction(actions[i].name, _win);
    _qactions.push_back(act);
    if(actions[i].toggle) {
      QObject::connect(act, &QAction::triggered, [=](bool ch){
	  queue_action((i + 1) | 0x100 | (ch ? 0x40 : 0), 0);
      });
      act->setCheckable(true);
      act->setChecked(*actions[i].toggle);
    } else
      QObject::connect(act, &QAction::triggered, [=]{
	  queue_action(i + 1, 0);
      });
    menu->addAction(act);
    if(actions[i].def_binding) {
      // Too bad there's no way to deactivate these in global menu
      act->setShortcut(QKeySequence(actions[i].def_binding));
      // act->setShortcutContext(Qt::ApplicationShortcut);
    }
  }
  menubar->setVisible(_show_menubar);
  _cw = new MainWidget();
  _win->setCentralWidget(_cw);
  _cw->installEventFilter(_win);
  _cw->setFocusPolicy(Qt::StrongFocus);
  _cw->resize(w, h);
  adj_mainwin();
  _win->show();
  pthread_mutex_lock(&_qtlock);
  _started = true;
  pthread_cond_signal(&_qtsig);
  pthread_mutex_unlock(&_qtlock);
  // action queue must be executed in the same thread
  QTimer *timer = new QTimer(_win);
  QObject::connect(timer, &QTimer::timeout, []{exec_actions(false);});
  timer->start(10);
  _qtapp->exec();
  quit();
  // not reached
  return NULL;
}

void MainWidget::initializeGL()
{
  gl_init();
  // Update every so often.
  // 30hz is good enough for NTSC interlaced, but 60 won't crush the system.
  // Then again, I could also just call update() in the main loop when screen updates
  timer = new QTimer(_win);
  connect(timer, SIGNAL(timeout(void)), SLOT(update(void)));
  timer->start(16);
}

void MainWidget::paintGL()
{
  gl_render(width(), height());
}

/* Called from C to set up Qt window */
/* Actually, window is created in qt_events(), since Qt doesn't
 * like creating objects in a different thread */
void qt_init(int w, int h)
{
  pthread_t thr;
  int wh[2] = { w, h };
  pthread_create(&thr, NULL, qt_events, wh);
  pthread_mutex_lock(&_qtlock);
  while(!_started && pthread_cond_wait(&_qtsig, &_qtlock));
  pthread_mutex_unlock(&_qtlock);
}

/* Here's the hard part: passing Qt events down */
/* Right now, we only care about keyboard and resize events */
/* There is actually no reasonable way to map Qt to SDL key events, since Qt
 * only provides high-level codes, but SDL is lower-level.  For example,
 * how do you know Qt::Key_Greater is shift-<> or shift-.? or something else? */

/* Note: these tables are hand-generated and probably need more testing */
static unsigned int /*SDL_Scancode*/ qt2sdl[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  /* This is just ASCII */
  SDL_SCANCODE_SPACE,SDL_SCANCODE_1/*Exclam*/,SDL_SCANCODE_APOSTROPHE/*QuoteDbl*/,SDL_SCANCODE_3/*NumberSign*/,
  SDL_SCANCODE_4/*Dollar*/,SDL_SCANCODE_5/*Percent*/,SDL_SCANCODE_7/*Ampersand*/,SDL_SCANCODE_APOSTROPHE,
  SDL_SCANCODE_9/*ParenLeft*/,SDL_SCANCODE_0/*ParenRight*/,SDL_SCANCODE_8/*Asterisk*/,SDL_SCANCODE_EQUALS/*Plus*/,
  SDL_SCANCODE_COMMA,SDL_SCANCODE_MINUS,SDL_SCANCODE_PERIOD,SDL_SCANCODE_SLASH,
  SDL_SCANCODE_0,SDL_SCANCODE_1,SDL_SCANCODE_2,SDL_SCANCODE_3,
  SDL_SCANCODE_4,SDL_SCANCODE_5,SDL_SCANCODE_6,SDL_SCANCODE_7,
  SDL_SCANCODE_8,SDL_SCANCODE_9,SDL_SCANCODE_SEMICOLON/*Colon*/,SDL_SCANCODE_SEMICOLON,
  /* this is hard: < and > could be NONUSBACKSLASH key */
  SDL_SCANCODE_COMMA/*Less*/,SDL_SCANCODE_EQUALS,SDL_SCANCODE_PERIOD/*Greater*/,SDL_SCANCODE_SLASH/*Question*/,
  SDL_SCANCODE_2/*At*/,SDL_SCANCODE_A,SDL_SCANCODE_B,SDL_SCANCODE_C,
  SDL_SCANCODE_D,SDL_SCANCODE_E,SDL_SCANCODE_F,SDL_SCANCODE_G,
  SDL_SCANCODE_H,SDL_SCANCODE_I,SDL_SCANCODE_J,SDL_SCANCODE_K,
  SDL_SCANCODE_L,SDL_SCANCODE_M,SDL_SCANCODE_N,SDL_SCANCODE_O,
  SDL_SCANCODE_P,SDL_SCANCODE_Q,SDL_SCANCODE_R,SDL_SCANCODE_S,
  SDL_SCANCODE_T,SDL_SCANCODE_U,SDL_SCANCODE_V,SDL_SCANCODE_W,
  SDL_SCANCODE_X,SDL_SCANCODE_Y,SDL_SCANCODE_Z,SDL_SCANCODE_LEFTBRACKET,
  SDL_SCANCODE_BACKSLASH,SDL_SCANCODE_RIGHTBRACKET,SDL_SCANCODE_6/*AsciiCircum*/,SDL_SCANCODE_MINUS/*Underscore*/,
  0/*QuoteLeft*/,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,SDL_SCANCODE_LEFTBRACKET/*BraceLeft*/,
  SDL_SCANCODE_BACKSLASH/*Bar*/,SDL_SCANCODE_RIGHTBRACKET/*BraceRight*/,SDL_SCANCODE_GRAVE,0,
  /* this is just Latin-1 */
  /* but there's no way to translate this high-level crap to SDL */
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0/*nobreakspace*/,0/*exclamdown*/,0/*cent*/,0/*sterling*/,
  0/*currency*/,0/*yen*/,0/*brokenbar*/,0/*section*/,
  0/*diaeresis*/,0/*copyright*/,0/*ordfeminine*/,0/*guillemotleft*/,
  0/*notsign*/,0/*hyphen*/,0/*registered*/,0/*macron*/,
  0/*degree*/,0/*plusminus*/,0/*twosuperior*/,0/*threesuperior*/,
  0/*acute*/,0/*mu*/,0/*paragraph*/,0/*periodcentered*/,
  0/*cedilla*/,0/*onesuperior*/,0/*masculine*/,0/*guillemotright*/,
  0/*onequarter*/,0/*onehalf*/,0/*threequarters*/,0/*questiondown*/,
  0/*Agrave*/,0/*Aacute*/,0/*Acircumflex*/,0/*Atilde*/,
  0/*Adiaeresis*/,0/*Aring*/,0/*AE*/,0/*Ccedilla*/,
  0/*Egrave*/,0/*Eacute*/,0/*Ecircumflex*/,0/*Ediaeresis*/,
  0/*Igrave*/,0/*Iacute*/,0/*Icircumflex*/,0/*Idiaeresis*/,
  0/*ETH*/,0/*Ntilde*/,0/*Ograve*/,0/*Oacute*/,
  0/*Ocircumflex*/,0/*Otilde*/,0/*Odiaeresis*/,0/*multiply*/,
  0/*Ooblique*/,0/*Ugrave*/,0/*Uacute*/,0/*Ucircumflex*/,
  0/*Udiaeresis*/,0/*Yacute*/,0/*THORN*/,0/*ssharp*/,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0/*division*/,
  0,0,0,0,
  0,0,0,0/*ydiaeresis*/
}, qt2sdlkp[256] = {
  /* Qt gives keypad defs using a modifier for some reason */
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  SDL_SCANCODE_KP_SPACE,SDL_SCANCODE_KP_EXCLAM,0,SDL_SCANCODE_KP_HASH,
  0,SDL_SCANCODE_KP_PERCENT,SDL_SCANCODE_KP_AMPERSAND,0,
  SDL_SCANCODE_KP_LEFTPAREN,SDL_SCANCODE_KP_RIGHTPAREN,SDL_SCANCODE_KP_MULTIPLY,SDL_SCANCODE_KP_PLUS,
  SDL_SCANCODE_KP_COMMA,SDL_SCANCODE_KP_MINUS,SDL_SCANCODE_KP_PERIOD,SDL_SCANCODE_KP_DIVIDE,
  SDL_SCANCODE_KP_0,SDL_SCANCODE_KP_1,SDL_SCANCODE_KP_2,SDL_SCANCODE_KP_3,
  SDL_SCANCODE_KP_4,SDL_SCANCODE_KP_5,SDL_SCANCODE_KP_6,SDL_SCANCODE_KP_7,
  SDL_SCANCODE_KP_8,SDL_SCANCODE_KP_9,SDL_SCANCODE_KP_COLON,0,
  SDL_SCANCODE_KP_LESS,SDL_SCANCODE_KP_EQUALS,SDL_SCANCODE_KP_GREATER,0,
  SDL_SCANCODE_KP_AT,SDL_SCANCODE_KP_A,SDL_SCANCODE_KP_B,SDL_SCANCODE_KP_C,
  SDL_SCANCODE_KP_E,SDL_SCANCODE_KP_F,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,0,
  0,0,0,SDL_SCANCODE_KP_LEFTBRACE,
  SDL_SCANCODE_KP_VERTICALBAR,SDL_SCANCODE_KP_RIGHTBRACE,0,0,
  /* nothing in upper half except divide.  O, but for working array designators */
  /* [0xf7]=SDL_SCANCODE_KP_DIVIDE  (even gcc barfs on this) */
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,SDL_SCANCODE_KP_DIVIDE,0,0,0,0,0,0,0,0
}, qt2sdl2[256] = { /* 0x01000000+ */
  SDL_SCANCODE_ESCAPE,SDL_SCANCODE_TAB,0 /*Backtab*/,SDL_SCANCODE_BACKSPACE,
  SDL_SCANCODE_RETURN,SDL_SCANCODE_KP_ENTER,SDL_SCANCODE_INSERT,SDL_SCANCODE_DELETE,
  SDL_SCANCODE_PAUSE,SDL_SCANCODE_PRINTSCREEN,SDL_SCANCODE_SYSREQ,SDL_SCANCODE_NUMLOCKCLEAR,
  0,0,0,0,
  SDL_SCANCODE_HOME,SDL_SCANCODE_END,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,
  SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_PAGEUP,SDL_SCANCODE_PAGEDOWN,
  0,0,0,0,
  0,0,0,0,
  /* Qt doesn't have L vs R shift/alt/ctrl/meta */
  SDL_SCANCODE_LSHIFT,SDL_SCANCODE_LCTRL,SDL_SCANCODE_LGUI,SDL_SCANCODE_LALT,
  SDL_SCANCODE_CAPSLOCK,SDL_SCANCODE_CLEAR,SDL_SCANCODE_SCROLLLOCK,0,
  0,0,0,0,
  0,0,0,0,
  SDL_SCANCODE_F1,SDL_SCANCODE_F2,SDL_SCANCODE_F3,SDL_SCANCODE_F4,
  SDL_SCANCODE_F5,SDL_SCANCODE_F6,SDL_SCANCODE_F7,SDL_SCANCODE_F8,
  SDL_SCANCODE_F9,SDL_SCANCODE_F10,SDL_SCANCODE_F11,SDL_SCANCODE_F12,
  SDL_SCANCODE_F13,SDL_SCANCODE_F14,SDL_SCANCODE_F15,SDL_SCANCODE_F16,
  SDL_SCANCODE_F17,SDL_SCANCODE_F18,SDL_SCANCODE_F19,SDL_SCANCODE_F20,
  SDL_SCANCODE_F21,SDL_SCANCODE_F22,SDL_SCANCODE_F23,SDL_SCANCODE_F24,
  0 /*F25*/,0 /*F26*/,0 /*F27*/,0 /*F28*/,
  0 /*F29*/,0 /*F30*/,0 /*F31*/,0 /*F32*/,
  /* super/hyper: no idea what to do with these, vs. meta */
  /* so I'm mapping super to alt and hyper to gui */
  0 /*F33*/,0 /*F34*/,0 /*F35*/,SDL_SCANCODE_LALT,
  SDL_SCANCODE_RALT,SDL_SCANCODE_MENU,SDL_SCANCODE_LGUI,SDL_SCANCODE_RGUI,
  SDL_SCANCODE_HELP,0 /*Direction_L*/,0,0,
  0,0,0,0,
  0/*Direction_R*/,SDL_SCANCODE_AC_BACK,SDL_SCANCODE_AC_FORWARD,SDL_SCANCODE_AC_STOP,
  SDL_SCANCODE_AC_REFRESH,0,0,0,
  0,0,0,0,
  0,0,0,0,
  SDL_SCANCODE_VOLUMEDOWN,SDL_SCANCODE_MUTE,SDL_SCANCODE_VOLUMEUP,0/*BassBoost*/,
  0/*BassUp*/,0/*BassDown*/,0/*TrebleUp*/,0/*TrebleDown*/,
  SDL_SCANCODE_AUDIOPLAY,0/*MediaStop*/,SDL_SCANCODE_AUDIOPREV,SDL_SCANCODE_AUDIONEXT,
  0/*MediaRecord*/,0/*MediaPause*/,0/*MediaTogglePlayPause*/,0,
  0,0,0,0,
  0,0,0,0,
  SDL_SCANCODE_AC_HOME,SDL_SCANCODE_AC_BOOKMARKS,SDL_SCANCODE_AC_SEARCH,SDL_SCANCODE_SLEEP,
  SDL_SCANCODE_WWW/*OpenUrl*/,0,0,0,
  0,0,0,0,
  0,0,0,0,
  SDL_SCANCODE_MAIL,SDL_SCANCODE_MEDIASELECT,SDL_SCANCODE_COMPUTER,SDL_SCANCODE_CALCULATOR,
  0/*Launch2*/,0/*Launch3*/,0/*Launch4*/,0/*Launch5*/,
  0/*Launch6*/,0/*Launch7*/,0/*Launch8*/,0/*Launch9*/,
  0/*LaunchA*/,0/*LaunchB*/,0/*LaunchC*/,0/*LaunchD*/,
  0/*LaunchE*/,0/*LaunchF*/,SDL_SCANCODE_BRIGHTNESSUP,SDL_SCANCODE_BRIGHTNESSDOWN,
  SDL_SCANCODE_KBDILLUMTOGGLE,SDL_SCANCODE_KBDILLUMUP,SDL_SCANCODE_KBDILLUMDOWN,0/*PowerOff*/,
  0/*WakeUp*/,SDL_SCANCODE_EJECT,0/*ScreenSaver*/,SDL_SCANCODE_WWW,
  0/*Memo*/,0/*LightBulb*/,0/*Shop*/,0/*History*/,
  0/*AddFavorite*/,0/*HotLinks*/,0/*BrightnessAdjust*/,0/*Finance*/,
  0/*Community*/,SDL_SCANCODE_AUDIOREWIND,SDL_SCANCODE_AUDIOFASTFORWARD/*BackForward*/,0/*SDL_SCANCODE_SOFTLEFT*/,
  0/*SDL_SCANCODE_SOFTRIGHT*/,0/*Book*/,0/*CD*/,SDL_SCANCODE_CALCULATOR,
  0/*ToDoList*/,0/*ClearGrab*/,0/*Close*/,0/*Copy*/,
  0/*Cut*/,0/*Display*/,0/*DOS*/,0/*Documents*/,
  0/*Excel*/,0/*Explorer*/,0/*Game*/,0/*Go*/,
  0/*iTouch*/,0/*LogOff*/,0/*Market*/,0/*Meeting*/,
  0/*MenuKB*/,0/*MenuPB*/,0/*MySites*/,0/*News*/,
  0/*OfficeHome*/,0/*Option*/,0/*Paste*/,0/*Phone*/,
  0/*Calendar*/,0/*Reply*/,0/*Reload*/,0/*RotateWindows*/,
  0/*RotationPB*/,0/*RotationKB*/,0/*Save*/,0/*Send*/,
  0/*Spell*/,0/*SplitScreen*/,0/*Support*/,0/*TaskPane*/,
  0/*Terminal*/,0/*Tools*/,0/*Travel*/,0/*Video*/,
  0/*Word*/,0/*Xfer*/,0/*ZoomIn*/,0/*ZoomOut*/,
  0/*Away*/,0/*Messenger*/,0/*WebCam*/,0/*MailForward*/,
  0/*Pictures*/,0/*Music*/,0/*Battery*/,0/*Bluetooth*/
  /* There are more than 256 of these: */
  /*WLAN*//*UWB*//*AudioForward*//*AudioRepeat*/
  /*AudioRandomPlay*//*Subtitle*//*AudioCycleTrack*//*Time*/
  /*Hibernate*//*View*//*TopMenu*//*PowerDown*/
  /*Suspend*//*ContrastAdjust*//*LaunchG*//*LaunchH*/
  /*TouchpadToggle*//*TouchpadOn*//*TouchpadOff*//*MicMute*/
  /*Red*//*Green*//*Yellow*//*Blue*/
  /*ChannelUp*//*ChannelDown*//*Guide*//*Info*/
  /*Settings*//*MicVolumeUp*//*MicVolumeDown*//*skip*/
  /*New*//*Open*//*Find*//*Undo*/
  /*Redo*/
};

/* unmapped:
 * RCTRL RSHIFT (maybe RALT RGUI) MOD
 * NONUSBACKSLASH (<> or `~ or \|)
 * APPLICATION WinMenu/Compose key
 * POWER EXECUTE SELECT STOP AGAIN UNDO CUT PASTE FIND
 * EQUALSAS400 INTERNATIONAL[1..9] LANG[1..9]
 * ALTERASE CANCEL PRIOR RETURN2 SEPARATOR OUT OPER CLEARAGAIN CRSEL EXSEL
 * KP_00 KP_000 THOUSANDSSEPARATOR DECIMALSEPARATOR CURRENCYUNIT
 * CURRENCYSUBUNIT KP_XOR KP_POWER KP_DBLAMPERSAND KP_DBLVERTICALBAR
 * KP_MEMSTORE KP_MEMRECALL KP_MEMCLEAR KP_MEMADD KP_MEMSUBTRACT
 * KP_MEMMULTIPLY KP_MEMDIVIDE KP_PLUSMINUS KP_CLEAR KP_CLEARENTRY
 * KP_BINARY KP_OCTAL KP_DECIMAL KP_HEXADECIMAL
 * should support, but don't:
 * KP_TAB KP_BACKSPACE
 * AUDIOMUTE (use MUTE)
 * DISPLAYSWITCH
 * APP1 APP2
 * CALL ENDCALL (in Qt group 0x11)
 */
/* 0x010011xx group:
 * AltGr(03) may be useful; sysrq is 20
 * Rest is language-specific shifters (and Codeinput for kp code entry, I guess)
 * 0x010012xx group: dead keys
 * 0x0101xxxx group: ?  Select Yes No Execute
 * 0x0102xxxx group: ?  - Cancel Printer - Sleep Play Zoom - - - Exit
 * 0x1100000x group: phone keys:
 *   Context[1..4] Call Hangup Flip ToggleCallHangup VoiceDial LastNumberRedial
 * 0x1100002x group: camera?  Camera CameraFocus
 */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers" // stupid warning
static void kbd_event(Uint32 type, Uint8 state, QEvent *event)
{
  QKeyEvent *qev = static_cast<QKeyEvent *>(event);
  SDL_KeyboardEvent ev = { .type = type, .state = state };
  ev.keysym.mod = KMOD_NONE;
  /* Qt doesn't distinguish L from R, so only set L */
  if(qev->modifiers() & Qt::ShiftModifier)
    ev.keysym.mod |= KMOD_LSHIFT;
  if(qev->modifiers() & Qt::ControlModifier)
    ev.keysym.mod |= KMOD_LCTRL;
  if(qev->modifiers() & Qt::AltModifier)
    ev.keysym.mod |= KMOD_LALT;
  if(qev->modifiers() & Qt::MetaModifier)
    ev.keysym.mod |= KMOD_LGUI;
  /* Qt has no way of querying Numlock (KMOD_NUM) or CapsLock (CAPS) */
  if(qev->modifiers() & Qt::GroupSwitchModifier)
    ev.keysym.mod |= KMOD_MODE; /* I guess */
  int key = qev->key();
  if(qev->key() < 256) {
    if(qev->modifiers() & Qt::KeypadModifier)
      ev.keysym.scancode = (SDL_Scancode)qt2sdlkp[key];
    else
      ev.keysym.scancode = (SDL_Scancode)qt2sdl[key];
  } else if((qev->key() & 0xffffff00) == 0x01000000)
      ev.keysym.scancode = (SDL_Scancode)qt2sdl2[key&0xff];
  if(ev.keysym.scancode) {
    ev.keysym.sym = SDL_GetKeyFromScancode(ev.keysym.scancode);
    QString keys = qev->text();
    // For some reason, capslock doesn't even show up as a shift modifier.
    // So, if the "text" version of the key is upper-case, and no
    // shift modifier is present, set one.
    if(!ev.keysym.mod && keys.size() == 1 && keys[0].isUpper())
      ev.keysym.mod = KMOD_LSHIFT;
    // presumably SDL's smart enough to lock; otherwise I'll have to
    // lock in sdl_events()
    SDL_PushEvent((SDL_Event *)&ev);
  } else
    fprintf(stderr, "Unknown key pressed %08x %08x\n", qev->key(), (unsigned)qev->modifiers());
}

bool SDLEvWindow::eventFilter(QObject *obj, QEvent *event)
{
  if(obj != _cw)
    return QWidget::eventFilter(obj, event);
  switch(event->type()) {
   case QEvent::KeyPress:
    kbd_event(SDL_KEYDOWN, SDL_PRESSED, event);
    break;
   case QEvent::KeyRelease:
    kbd_event(SDL_KEYUP, SDL_PRESSED, event);
    break;
    /* Aq doesn't care about anything else, but does SDL? */
    /* I used to pass down window events, but that seems pointless now */
   case QEvent::ContextMenu:
    _cmenu->popup(static_cast<QContextMenuEvent *>(event)->globalPos());
    break;
    // FIXME: if _capturing, left/right mouse click terminate capture
   default:
    //	fprintf(stderr, "got event %d\n", event->type());
    return QWidget::eventFilter(obj, event);
  }
  return true;
}

/******** state file *********/

static value_t *_qtgui_state = NULL;

static void load_state()
{
  // Load persistent state (unless -n)
  if(!_qtgui_state && _config_file) {
    // XDG_STATE would be most appropriate, but nobody uses that.
    // XDG_CACHE would be more appropriate, but I don't care.
    const char *cf = find_xdg_file(XDG_CONFIG, "Aq", "qtgui.json");
    if(cf)
      _qtgui_state = json_decode_file(cf);
  }
}

static void write_state(void)
{
  if(!_qtgui_state)
    return;
  // XDG_STATE would be most appropriate, but nobody uses that.
  // XDG_CACHE would be more appropriate, but I don't care.
  mkpath(find_xdg_file(XDG_CONFIG, "Aq", NULL));
  FILE *f = fopen(find_xdg_file(XDG_CONFIG, "Aq/qtgui.json", NULL), "w");
  if(!f)
    return;
  char *fv = json_pretty(_qtgui_state);
  fputs(fv, f);
  fclose(f);
  free(fv);
}

static void set_state(const char *key, const QByteArray &val)
{
  value_t v = { .type = VDT_STRING, .s = (char *)(const char *)val.toBase64() };
  if(!_qtgui_state) {
    _qtgui_state = (value_t *)malloc(sizeof(*_qtgui_state));
    _qtgui_state->type = VDT_OBJECT;
    _qtgui_state->o = NULL;
  }
  json_set_objval(_qtgui_state, key, &v);
  write_state();
}

static QByteArray read_state(const char *key)
{
  const value_t *v = json_objval(_qtgui_state, key);
  if(v && v->type == VDT_STRING)
    return QByteArray::fromBase64(v->s);
  return QByteArray();
}

/*********  File popups *********/

static QFileDialog *fdialog(const char *dir, const char *title, const char *filt)
{
  QFileDialog *dlg = new QFileDialog(_win, title);

  // Apparently sidebarurls and history only work in non-native mode
  // Should I make this a configurable preference?
  // This has to be set before anything else is done.
  dlg->setOption(QFileDialog::DontUseNativeDialog);

  // User
  int slen = strlen(dir);
  char buf[slen + 4];
  memcpy(buf, "Aq/", 3);
  strcpy(buf + 3, dir);
  // System
  char sbuf[slen + sizeof(SYS_DIR) + 1];
  memcpy(sbuf, SYS_DIR, sizeof(SYS_DIR) - 1);
  sbuf[sizeof(SYS_DIR) - 1] = '/';
  strcpy(sbuf + sizeof(SYS_DIR), dir);

  // And also no way to set handy reference names, but I guess that's OK
#if 1
  // Side panel; label is last path element
  dlg->setSidebarUrls({
    QUrl::fromLocalFile("."),  // Here
    QUrl::fromLocalFile(dir), // <dir>
    QUrl::fromLocalFile(find_xdg_file(XDG_DATA, buf, NULL)), // User
    QUrl::fromLocalFile(sbuf) // System
  });
#else
  // dir popdown; full path displayed but below "Recent Locations" at bottom
  dlg->setHistory({".", dir, find_xdg_file(1, buf, NULL), sbuf});
#endif
  dlg->setNameFilters({filt, "All (*)"});
  dlg->setOption(QFileDialog::HideNameFilterDetails);
  dlg->setFileMode(QFileDialog::AnyFile);

  // reuse User buffer above (Aq/ prefix)
  dlg->restoreGeometry(read_state(buf));
  return dlg;
}

// FIXME: add RESET option (add checkbox: reset immediately)
//   Probably just always pop up a "RESET Now?  Yes/No/Always/Never" (unsaved)
//   Either that, or add a checkbox in the ROM menu
void askrom(romdesc *rom)
{
  static QFileDialog *romreq = NULL;
  // if not there, Qt defaults to cur dir
  if(!romreq)
    romreq = fdialog("roms", "ROM File",
		     "ROMs (*.[bB][iI][nN] *[rR][oO][mM])");
  if(!romreq->restoreState(rom->what)) {
    // pref is non-NULL iff rom is required
    romreq->setFileMode(rom->pref ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    // restoreState apparently does nothing
    // It's supposed to restore history, sidebar URLS, splitters, dialog size
    // Where did current ROM come from?
    char *wdbuf = NULL;
    const char *wd = file_from(rom->where, "roms", &wdbuf);
    romreq->setDirectory(wd);
    if(wdbuf)
      free(wdbuf);
  }

  QString cap(rom->what);
  cap += " ROM File";
  romreq->setWindowTitle(cap);
  romreq->selectFile(rom->where ? rom->where : "");
  if(romreq->exec()) {
    auto sel = romreq->selectedFiles();
    if(!sel.count())
      rom->where = NULL;
    else {
      /* Mostly copied from grok's qstrdup */
      rom->buf = sel[0].toStdString();
      if(!rom->buf.size())
	rom->where = NULL;
      else
	rom->where = rom->buf.c_str();
      if(rom->pref)
	set_config_str(rom->pref, rom->where);
    }
  }
  // This saves a state, but is it too late?  Should I place this higher?
  // For some reason, restoring state doesn't work
  set_state(rom->what, romreq->saveState());
  set_state("Aq/roms", romreq->saveGeometry());
}

static void addrom(QGridLayout *g, int r, romdesc *rom, const char *def,
		   const char *chk = NULL, bool *bv = NULL)
{
  QString l(rom->what);
  int c = 0;
  g->addWidget(new QLabel(l + " ROM"), r, c++, Qt::AlignLeft);
  auto te = new QLineEdit(rom->what);
  te->setPlaceholderText(def);
  te->setText(rom->where ? rom->where : "");
  QObject::connect(te, &QLineEdit::textEdited, [=](const QString &e){
    rom->buf = e.toStdString();
  });
  g->addWidget(te, r, c++);
  auto b = new QPushButton("...");
  QObject::connect(b, &QPushButton::pressed, [=]{
    askrom(rom);
    te->setText(rom->where ? rom->where : "");
  });
  g->addWidget(b, r, c++);
  if(chk) {
    auto cb = new QCheckBox(chk);
    cb->setChecked(*bv);
#define do_cb(w) QObject::connect(w, &QCheckBox::stateChanged, [=](int st)
    do_cb(cb) {
      *bv = st != Qt::Unchecked;
    });
    g->addWidget(cb, r, c++);
  }
}

void askroms(void)
{
  // I don't feel like making this static, as I would have to track too much.
  auto dlg = new QDialog(_win);
  dlg->setWindowTitle("Select ROMs");
  auto v = new QVBoxLayout(dlg);
  auto g = new QGridLayout();
  v->addLayout(g);
  int r = 0;
  const char *grom_tmp = _gamerom_desc.where, *mrom_tmp = _mainrom_desc.where,
             *crom_tmp = _charrom_desc.where, *ebrom_tmp = _ebrom_desc.where;
  romdesc grom = { .what = _gamerom_desc.what, .where = grom_tmp,
                   .buf = _gamerom_desc.where ? _gamerom_desc.where : "" };
  romdesc mrom = { .what = _mainrom_desc.what, .where = mrom_tmp,
                   .buf = _mainrom_desc.where };
  romdesc crom = { .what = _charrom_desc.what, .where = crom_tmp,
                   .buf = _charrom_desc.where };
  romdesc ebrom = { .what = _ebrom_desc.what, .where = ebrom_tmp,
                    .buf = _ebrom_desc.where };
  bool save_gr = false, use_eb = _extendedbasic;
  addrom(g, r++, &grom, "None if blank", "Save", &save_gr);
  addrom(g, r++, &mrom, DEFAULT_MAINROM);
  addrom(g, r++, &crom, DEFAULT_CHARROM);
  addrom(g, r++, &ebrom, DEFAULT_EBROM, "Use", &use_eb);
  g->setColumnStretch(1, 1);
#define db(x) QDialogButtonBox::x
  auto bb = new QDialogButtonBox(db(Save)|db(Cancel));
  v->addWidget(bb);
  QObject::connect(bb, &db(accepted), dlg, &QDialog::accept);
  QObject::connect(bb, &db(rejected), dlg, &QDialog::reject);
  if(dlg->exec() == QDialog::Accepted) {
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
  delete dlg;
}

/* Mostly copied from grok */
static char *qstrdup(QString &str, char *&buf)
{
  const QByteArray bytes(str.toLocal8Bit());
  size_t len = bytes.size();
  if(!len)
    return NULL;
  if(buf)
    buf = (char *)realloc(buf, len + 1);
  else
    buf = (char *)malloc(len + 1);
  memcpy(buf, bytes.data(), len);
  buf[len] = 0;
  return buf;
}



void asktape()
{
  static QFileDialog *fdlg = NULL;
  static char *buf = NULL;
  if(!fdlg) {
    fdlg = fdialog("tapes", "Tape File",
		   "Tapes (tape *.tape *.[cC][aA][qQ])");
    if(!fdlg->restoreState(read_state("tapes"))) {
      char *wdbuf = NULL;
      const char *wd = find_xdg_file(XDG_DATA, "Aq/tapes", NULL);;
      if(_tape)
	wd = file_from(_tape, "tapes", &wdbuf);
      fdlg->setDirectory(wd);
      if(!_tape)
	mkpath(wd);
      if(wdbuf)
	free(wdbuf);
    }
  }
  fdlg->selectFile(_tape ? _tape : "");
  if(fdlg->exec()) {
    auto sel = fdlg->selectedFiles();
    // FIXME: leaks _tape
    if(!sel.count())
      _tape = NULL;
    else
      _tape = qstrdup(sel[0], buf);
  }
  set_state("tapes", fdlg->saveState());
  set_state("Aq/tapes", fdlg->saveGeometry());
  asked_tape();
}

void askbasic(void)
{
  static QFileDialog *fdlg = NULL;
  static char *buf = NULL;
  if(!fdlg) {
    fdlg = fdialog("bas", "BASIC File",
		   "BASIC (*.[bB][aA][sS] *.[tT][xX][tT])");
    if(!fdlg->restoreState("bas")) {
      // char *wdbuf = NULL;
      // const char *wd = file_from(buf, "bas", &wdbuf);
      fdlg->setDirectory(".");
      // if(wdbuf)
      //   free(wdbuf);
    }
  }
  fdlg->selectFile("");
  if(fdlg->exec()) {
    auto sel = fdlg->selectedFiles();
    if(!sel.count())
      return;
    else {
      char *basicfile = qstrdup(sel[0], buf);
      if(basicfile == NULL)
	return;
      if(_keybuf)
	free(_keybuf);
      load_basic(basicfile);
    }
  }
  set_state("bas", fdlg->saveState());
  set_state("Aq/bas", fdlg->saveGeometry());
}

/*********  Binding popup *********/

static QDialog *_bindkey_dialog;
static void create_bindkey_dialog(void);

// The main key binding dialog
void bind_key(void)
{
  create_bindkey_dialog();
  bool ok = _bindkey_dialog->exec() == QDialog::Accepted;
  // following 2 should display list of changes to config in confirm popup
  if(ok) {
    // if(!same_bindings) {
    //   // popup("Are you sure?");
    //   merge_tmp_keys_with_keys();
    //   save_config();
    // }
  } else {
    // if(!same_bindings()) {
    //   popup("Are you sure?");
    //   clear_bindings();
    //   add_sdl_bindings();
    // }
  }
  // What's the point?  bindkey always resizes to fit contents
  // Even between invocations, unlike the file dialog
  set_state("bindkey", _bindkey_dialog->saveGeometry());
}

// Actions widget
static QListWidget *_action_widget;

/* I should just give up on making this "efficient" and have just one version */
/* Also, it should probably deselect everything else if shift not selected */
static void kbclick(QWidget *w, int i, bool sclk = false);

/* A pressable label (not the Qt signal/slot way) */
class AqLabButton : public QLabel {
  public:
    AqLabButton(int _i, const char *text) : QLabel(text), i(_i) {
	setAlignment(Qt::AlignCenter|Qt::AlignVCenter);
	if(i) {
	    setForegroundRole(QPalette::Text);
	    setBackgroundRole(QPalette::Base);
	} else {
	    // RST key is inverted
	    setForegroundRole(QPalette::Base);
	    setBackgroundRole(QPalette::Text);
	}
	setAutoFillBackground(true);
    }
    void mousePressEvent(QMouseEvent *event) {
	kbclick(this, i, event->modifiers() & Qt::ShiftModifier);
    }
    int i;
};

/* A pressable generic widget (not the Qt signal/slot way) */
class AqWButton : public QWidget {
  public:
    AqWButton(int _i) : QWidget(), i(_i) {
	setForegroundRole(QPalette::Text);
	setBackgroundRole(QPalette::Base);
	setAutoFillBackground(true);
    }
    int i;
    void mousePressEvent(QMouseEvent *event) { kbclick(this, i,  event->modifiers() & Qt::ShiftModifier); }
};

static QWidget *_key_widget[NKEYS];

static void clear_kb(void), clear_joy(void), set_kbact(void);
static void clear_chordsel(void);

/* Controller dial widget (not the Qt signal/slot way) */
class AqDial : public QWidget {
   public:
    AqDial(int _i) : QWidget(), i(_i), sel(0) {}
    /* sel: 0 = none, 1 .. 16 = angle */
    int i, sel;
    void paintEvent(_U QPaintEvent *event) {
      QPainter p(this);
      p.setBrush(QBrush(Qt::black, Qt::SolidPattern));
      p.drawEllipse(rect());
      if(sel) {
	// real angle = 360 / 16 * (sel - 1) - 360 / 32
	// qt angle = real * 16:
	int angle = 360 * (sel - 1) - 360 / 2;
	p.setBrush(QBrush(Qt::white, Qt::SolidPattern));
	p.drawPie(rect(), angle, 360); // 360 == 1/16 of a circle
      }
    }
    void mousePressEvent(QMouseEvent *event) {
      /* compute direction index */
      int osel = sel;
      double x = (double)event->x() / width() - 0.5, /* * 2 */
	     y = (double)event->y() / height() - 0.5, /* * 2 */
	     r = x * x + y * y; /* r/4, actually, due to missing *2 above */
      if(r > 0.25 || r < 0.025)
	sel = 0; // Deselect if outside circle or too close to center
      else {
	/* from aqjoy.c */
	/* except y needs inverting, since it grows downward */
	y = -y;
	/* atan(y/x) == -pi/2 .. pi/2 -> -4 .. 4 */
	double th = x ? atan((double)y/x) / M_PI * 8.0 : y > 0 ? -4.0 : 4.0;
	/* correction for quadrant 2-3 */
	if(x < 0)
	  th += 8.0; // -4 .. 12
	int ith = (int)round(th) & 15;
	/* end stuff from aqjoy.c */
	sel = ith + 1;
      }
      if(osel != sel) {
	// FIXME: if bindings changed, and no Apply, "Abandon changes?"
	/* clear keyboard selection */
	if(!(event->modifiers() & Qt::ShiftModifier)) {
	  clear_kb();
	  osel = sel;
	  clear_joy();
	  sel = osel;
	  clear_chordsel();
	}
	/* clear action selection */
	_action_widget->setCurrentRow(0);
	/* update binding string */
	set_kbact();

	/* draw or erase white wedge */
	repaint();
      }
    }
};

static QWidget *_joyb_widget[12];
static AqDial *_joyd_widget[2];

static QComboBox *_chordsel;
static QLineEdit *_bindstr;
static QCheckBox *_onb, *_offb, *_prb;

static void set_actbind(void)
{
  auto it = _action_widget->currentItem();
  int act = it->data(Qt::UserRole).toInt();
  bool istog = act > 0 && actions[act].toggle != NULL;
  _onb->setDisabled(!istog);
  _offb->setDisabled(!istog);
  _prb->setDisabled(!istog);
  if(act >= 0) {
    actid_t aid = act + 1;
    if(istog) {
      if(_onb->isChecked())
	aid |= 0x140;
      else if(_offb->isChecked())
	aid |= 0x100;
      if(_prb->isChecked())
	aid |= 0x80;
    }
    char *b = bound_keys(aid);
    _bindstr->setText(b ? b : "");
    free(b);
  } else
    _bindstr->setText("");
}

static void select_keys(int);

static void create_bindkey_dialog(void)
{
  if(_bindkey_dialog)
    return;
  _bindkey_dialog = new QDialog(_win);
  _bindkey_dialog->setWindowTitle("Bind Keys");
  auto v = new QVBoxLayout(_bindkey_dialog);

  // Top: tab (kb/joy/action)
  auto tw = new QTabWidget(_bindkey_dialog);
  v->addWidget(tw);

  // The special actions list
  auto at = new QWidget;
  tw->addTab(at, "&Actions");
  auto av = new QVBoxLayout(at);
  _action_widget = new QListWidget;
  av->addWidget(_action_widget);
  auto ah = new QHBoxLayout();
  av->addLayout(ah);
  _onb = new QCheckBox("On");
  ah->addWidget(_onb);
  _onb->setDisabled(true);
  _offb = new QCheckBox("Off");
  ah->addWidget(_offb);
  _offb->setDisabled(true);
  _prb = new QCheckBox("While Pressed");
  ah->addWidget(_prb);
  _prb->setDisabled(true);
  do_cb(_onb) {
    if(st != Qt::Unchecked)
      _offb->setChecked(false);
    else if(!_offb->isChecked())
      _prb->setChecked(false);
    set_actbind();
  });
  do_cb(_offb) {
    if(st != Qt::Unchecked)
      _onb->setChecked(false);
    else if(!_onb->isChecked())
      _prb->setChecked(false);
    set_actbind();
  });
  do_cb(_prb) {
    if(st != Qt::Unchecked)
      if(!_onb->isChecked() && !_offb->isChecked())
	_onb->setChecked(true);
    set_actbind();
  });
  ah->addStretch(1);
  auto it = new QListWidgetItem("");
  it->setData(Qt::UserRole, QVariant::fromValue(-1));
  _action_widget->addItem(it);
  _action_widget->setCurrentRow(0);
  const char *curm = NULL;
  QString mlab, lab;
  for(unsigned i = 0; i < _num_actions; i++) {
    if(actions[i].parent) {
      if(!curm || strcmp(curm, actions[i].parent)) {
	curm = actions[i].parent;
	mlab = QString(curm) + " / "; // currently, there is no full hierarchy
	mlab.replace('&', "");
      }
      lab = mlab + actions[i].name;
    } else
      lab = actions[i].name;
    lab.replace('&', ""); // FIXME: support double &
    it = new QListWidgetItem(lab);
    it->setData(Qt::UserRole, QVariant::fromValue(i));
    _action_widget->addItem(it);
  }

  // Colors
  QPalette kbp;
  kbp.setColor(QPalette::Window, Qt::black);
  kbp.setColor(QPalette::WindowText, Qt::blue);
  kbp.setColor(QPalette::Base, Qt::blue);
  kbp.setColor(QPalette::Text, Qt::white);
  kbp.setColor(QPalette::BrightText, Qt::yellow);
  kbp.setColor(QPalette::AlternateBase, Qt::darkYellow);

  // Joysticks
  auto gpt = new QWidget;
  tw->addTab(gpt, "&Controllers");
  auto hb = new QHBoxLayout(gpt);
  gpt->setPalette(kbp);
  gpt->setAutoFillBackground(true);
  hb->addStretch(1);
  for(int j = 0; j < 2; j++) {
    auto gpw = new QWidget(gpt);
    gpw->setForegroundRole(QPalette::Window);
    gpw->setBackgroundRole(QPalette::AlternateBase);
    gpw->setAutoFillBackground(true);
    hb->addWidget(gpw, 1);
    hb->addStretch(1);
    auto gr = new QGridLayout(gpw);
    for(int b = 0; b < 6; b++) {
      char l[2] = { (char)(b + '1'), 0 };
      auto w = _joyb_widget[b + j * 6] = new AqLabButton(-(6 * j + b + 1), l);
      gr->addWidget(w, b / 3, b % 3);
    }
    _joyd_widget[j] = new AqDial(j);
    gr->addWidget(_joyd_widget[j], 3, 0, 3, 3);
  }

  // The keyboard image
  auto aqkb = new QWidget;
  tw->addTab(aqkb, "&Keyboard");
  aqkb->setPalette(kbp);
  aqkb->setAutoFillBackground(true);
  // Height: each key occupies 2 grid spaces (key/CTL-label)
  // Width: each key spans 2-3 grid spaces (1/2 char each)
  auto tg = new QGridLayout(aqkb);
  int minw = 0, minh = 0;
  for(int r = 0, i = 0; r < 4; r++)
    for(int c = 0; c < _row_nkeys[r]; c++, i++) {
      int adj = !c && r > 1 ? 1 : 0, adj2 = i == NKEYS ? 1 : adj;
      if(aqkeys_ctl[i]) {
	auto l = new QLabel(aqkeys_ctl[i]);
	l->setAlignment(Qt::AlignCenter);
	QFont f = l->font();
	f.setPointSizeF(f.pointSizeF() * .7);
	l->setFont(f);
	tg->addWidget(l, r * 2, c * 2 + r - adj, 1, 2 + adj2);
#if 0 // This doesn't work
	l->ensurePolished();
	l->updateGeometry();
	if(l->minimumWidth() > (2 + adj2) * minw)
	  minw = (l->minimumWidth() + adj2 + 1) / (2 + adj2);
#endif
      }
      QWidget *b = aqkeys_shift[i] != ' ' ?
	(QWidget *)(new AqWButton(i)) :
	(QWidget *)(new AqLabButton(i, aqkeys[i]));
#if 0 // This doesn't work
      b->ensurePolished();
      b->updateGeometry();
      if(b->minimumWidth() > (2 + adj2) * minw)
	minw = (b->minimumWidth() + adj2 + 1) / (2 + adj2);
      if(b->minimumHeight() > minh * 2)
	minh = (b->minimumHeight() + 1) / 2;
#endif
      _key_widget[i] = b;
      tg->addWidget(b, r * 2 + 1, c * 2 + r - adj, 1, 2 + adj2);
      if(aqkeys_shift[i] != ' ') {
	auto g = new QGridLayout(b);
	auto l = new QLabel(aqkeys[i]);
	auto sl = new QLabel(QString(aqkeys_shift[i]));
	sl->setAlignment(Qt::AlignCenter|Qt::AlignTop);
	if(i == 11 || i == 12) {
	  // These 2 are different from the rest:
	  // Height: 2 (shifted is above unshifted)
	  // Width: 1
	  l->setAlignment(Qt::AlignCenter|Qt::AlignBottom);
	  g->addWidget(l, 1, 0);
	  g->addWidget(sl, 0, 0);
	} else {
	  // Height: 3 (shifted is 1/2-char up)
	  //         each char spans 2 cells
	  // Width: 2 (left = main, right = shifted)
	  l->setAlignment(Qt::AlignCenter|Qt::AlignVCenter);
	  g->addWidget(l, 1, 0, 2, 1);
	  g->addWidget(sl, 0, 1, 2, 1);
	}
      }
    }
  // This doesn't force expansion until equality; it just ensures
  // equality once there's room to grow.
  // So there is code below to force minimum size equality as well.
  for(int r = 0; r < tg->rowCount(); r++)
    tg->setRowStretch(r, 1);
  for(int c = 0; c < tg->columnCount(); c++)
    tg->setColumnStretch(c, 1);

  // Below the tab..
  auto bl = new QHBoxLayout;
  v->addLayout(bl);
  bl->addWidget(new QLabel("Chords"));
  _chordsel = new QComboBox();
  QObject::connect(_chordsel,
		   QOverload<int>::of(&QComboBox::currentIndexChanged),
		   select_keys);
  bl->addWidget(_chordsel, 1);
  clear_chordsel();
  bl = new QHBoxLayout;
  v->addLayout(bl);
  bl->addWidget(new QLabel("Binding"));
  // FIXME: intelligent editing:
  //   deleting part of mod deletes all of mod
  //     in wxvbam, I disallowed editing the text, and instead deleted
  //     the last binding if backspace is entered, and otherwise add
  //     whatever was pressed immediately
  //   deleting a binding should also delete separator, if present
  //   double-clicking on text should select an entire binding
  _bindstr = new QLineEdit;
  bl->addWidget(_bindstr, 1);
  // FIXME:  pop up grab dialog to request an SDL event group
  // dialog:
  //  <text> == captured events so far; can scroll if necessary
  //  4 rows of 3 checkboxes each: ignore mod, ignore lmod, ignore rmod
  //  Cancel/Add/Find buttons @ bottom
  //    Add appends to _bindstr; Find selects current binding, if any
  //    Should do a find anyway, and warn if rebinding
  bl->addWidget(new QPushButton("Key..."));
  auto dbb = new QDialogButtonBox(db(Ok)|db(Cancel));
  v->addWidget(dbb);
  QObject::connect(dbb, &db(accepted), _bindkey_dialog, &QDialog::accept);
  QObject::connect(dbb, &db(rejected), _bindkey_dialog, &QDialog::reject);

  _bindkey_dialog->restoreGeometry(read_state("bindkey")); // this does nothing

  // This needs to be delayed until _bindstr exists; it triggers immediately
  QObject::connect(_action_widget, &QListWidget::currentRowChanged,
		   [=] {
    // FIXME: if bindings changed, and no Apply, "Abandon changes?"
    set_actbind();
    clear_joy();
    clear_kb();
    clear_chordsel();
  });
  /* This ensures all keys start at the same size, relatively */
  /* FIXME: There has to be a better way to do this. */
  // realize dialog to compute column widths
  //    tg->activate(); // 18x86 (???)
  _bindkey_dialog->show(); // 30x86
  for(int c = 0; c < tg->columnCount(); c++) {
    int colw = tg->cellRect(0, c).width();
    if(colw > minw)
      minw = colw;
  }
  for(int r = 0; r < 3; r++) {
    int rowh = tg->cellRect(r * 2 + 1, 0).height();
    if(rowh > minh)
      minh = rowh;
  }
  //    fprintf(stderr, "%dx%d\n", minw, minh);
  // resize; on my machine, this is fast enough I don't even notice
  // in fact, it just looks like it pops up correctly sized
  for(int c = 0; c < tg->columnCount(); c++)
    tg->setColumnMinimumWidth(c, minw);
  for(int r = 0; r < 3; r++)
    tg->setRowMinimumHeight(r * 2 + 1, minh);
}

static void clear_kb(void)
{
  _key_widget[0]->setBackgroundRole(QPalette::Text);
  _key_widget[0]->setForegroundRole(QPalette::Base);
  for(int k = 1; k < NKEYS; k++) {
    _key_widget[k]->setBackgroundRole(QPalette::Base);
    _key_widget[k]->setForegroundRole(QPalette::Text);
  }
}

static void clear_joy(void)
{
  for(int b = 0; b < 12; b++) {
    _joyb_widget[b]->setBackgroundRole(QPalette::Base);
    _joyb_widget[b]->setForegroundRole(QPalette::Text);
  }
  for(int j = 0; j < 2; j++)
    if(_joyd_widget[j]->sel) {
      _joyd_widget[j]->sel = 0;
      _joyd_widget[j]->repaint();
    }
}

static void kbclick(QWidget *w, int i, bool sclk)
{
  bool isclicked = w->backgroundRole() == QPalette::BrightText;
  // FIXME:  save bindings if changed.  Apply to global table and
  // local temporary changes
  if(!i && !isclicked)
    _action_widget->setCurrentRow(_reset_act + 1);
  else
    _action_widget->setCurrentRow(0);
  /* Disallow chording except keys w/ SHIFT/CTL */
  /* Well, technically, any keys can be chorded together */
  /* Aquarius might not recognize it by default, but it will still work */
  /* So, allow arbitrary chording w/ shift-click */
  if(!sclk || !i || _key_widget[0]->backgroundRole() == QPalette::BrightText) {
    clear_joy();
    if(i <= 0)
      clear_kb();
    else {
      if(i != KEY_SHIFT && i != KEY_CTL)
	for(int k = 0; k < NKEYS; k++)
	  if(i != k && k != KEY_SHIFT && k != KEY_CTL) {
	    _key_widget[k]->setBackgroundRole(k ? QPalette::Base : QPalette::Text);
	    _key_widget[k]->setForegroundRole(k ? QPalette::Text : QPalette::Base);
	  }
    }
  }
  if(isclicked) {
    // RST key is inverted
    w->setBackgroundRole(i ? QPalette::Base : QPalette::Text);
    w->setForegroundRole(i ? QPalette::Text : QPalette::Base);
  } else {
    w->setBackgroundRole(QPalette::BrightText);
    w->setForegroundRole(QPalette::Base);
  }
  set_kbact();
}

// callback for chord selection
static void select_keys(_U int idx)
{
  bool is_other;
  int aid = _chordsel->currentData().toInt(&is_other);
  if(!is_other || aid < 0)
    return;
  for(int i = 1; i < NKEYS; i++) {
    _key_widget[i]->setBackgroundRole(QPalette::Base);
    _key_widget[i]->setForegroundRole(QPalette::Text);
  }
  for(int i = 0; i < 12; i++) {
    _joyb_widget[i]->setBackgroundRole(QPalette::Base);
    _joyb_widget[i]->setForegroundRole(QPalette::Text);
  }
  for(int i = 0; i < 2; i++)
    _joyd_widget[i]->sel = 0;
  const vector<actid_t> a1 = { aid };
  for(auto k : aid >= 0x10000 ? _chords[aid & 0xffff] : a1) {
    if(k < 0)
      k = -k;
    // plain key: 0x200 + <keyid> (_keys[] array index)
    if(k < 0x400) {
      char *s = action_to_string(k);
      s[0] = toupper(s[0]);
      for(unsigned i = 1; i < NKEYS; i++)
	if(!strcmp(aqkeys[i], s)) {
	  _key_widget[i]->setBackgroundRole(QPalette::BrightText);
	  _key_widget[i]->setForegroundRole(QPalette::Base);
	  break;
	}
      free(s);
      continue;
    }
    // plain joy button: 0x400 + 0x100 * <joy> + <but>
    if(k < 0x800) {
      unsigned b = k & 0xf;
      if(k & 0x100)
	b += 6;
      _joyb_widget[b]->setBackgroundRole(QPalette::BrightText);
      _joyb_widget[b]->setForegroundRole(QPalette::Base);
    } else
      // plain joy dir: KV_AX + 0x1000 * <dir#> + 0x800 + 0x100 * <joy>
      _joyd_widget[k & 0x100 ? 1 : 0]->sel = (k >> 12) & 0xf;
  }
  // In case of change, but I should probably track change instead
  for(int i = 0; i < 2; i++)
    _joyd_widget[i]->repaint();
}

static void set_kbact(void)
{
  int ksel = -1, bsel = -1;
  bool joyd1 = _joyd_widget[0]->sel != 0, joyd2 = _joyd_widget[1]->sel != 0;
  for(int k = 1; k < NKEYS; k++)
    if(_key_widget[k]->backgroundRole() == QPalette::BrightText) {
      ksel = k;
      break;
    }
  for(int b = 0; b < 12; b++)
    if(_joyb_widget[b]->backgroundRole() == QPalette::BrightText) {
      bsel = b;
      break;
    }
  actid_t aid = 0;
  if(ksel >= 0 && bsel < 0 && !joyd1 && !joyd2) {
    aid = parse_action(aqkeys[ksel]);
    for(int k = ksel + 1; k < NKEYS; k++)
      if(_key_widget[k]->backgroundRole() == QPalette::BrightText) {
	aid = 0;
	break;
      }
  } else if(ksel < 0 && bsel >= 0 && !joyd1 && !joyd2) {
    aid = JC_BUT(bsel >= 6 ? 2 : 1, bsel % 6 + 1);
    for(int b = bsel + 1; b < 12; b++)
      if(_joyb_widget[b]->backgroundRole() == QPalette::BrightText) {
	aid = 0;
	break;
      }
  } else if(ksel < 0 && bsel < 0 && joyd1 && !joyd2) {
    aid = JC_AX(1, _joyd_widget[0]->sel - 1);
  } else if(ksel < 0 && bsel < 0 && !joyd1 && joyd2) {
    aid = JC_AX(2, _joyd_widget[1]->sel - 1);
  }
  set_actbind();
  if(ksel < 0 && bsel < 0 && !joyd1 && !joyd2) {
    clear_chordsel();
    return;
  }
  vector<actid_t> chord;
  if(ksel >= 0)
    for(int i = ksel; i < NKEYS; i++)
      if(_key_widget[i]->backgroundRole() == QPalette::BrightText)
	chord.push_back(parse_action(aqkeys[i]));
  if(bsel >= 0)
    for(int i = bsel; i < 12; i++)
      if(_joyb_widget[i]->backgroundRole() == QPalette::BrightText)
	chord.push_back(JC_BUT(i >= 6 ? 2 : 1, i % 6 + 1));
  for(int i = 0; i < 2; i++)
    if(_joyd_widget[i]->sel)
      chord.push_back(JC_AX(i + 1, _joyd_widget[i]->sel - 1));
  if(!aid) {
    int cid = chord_id_of(chord);
    if(cid >= 0)
      aid = 0x10000 + cid;
  }
  if(aid) {
    char *b = bound_keys(aid);
    _bindstr->setText(b ? b : "");
    free(b);
  }
  auto chord_acts = possible_chords(chord);
  int chord_sel = 0;
  _chordsel->clear();
  vector<int> acts;
  for(auto &c : chord_acts) {
    if(c == chord) {
      chord_sel = _chordsel->count();
      aid = -1;
    } else
      aid = c.size() > 1 ? chord_id_of(c) + 0x10000 : c[0];
    QString e;
    for(auto k : c) {
      char *s = action_to_string(k);
      if(e.size())
	e += '+';
      e += s;
      free(s);
    }
    // all but chord must have a valid chord id, since they're bound
    // it's easier to convert chord id to chord than to parse string
    // however, setting it here complcates things:
    // adding an element immediately triggers an event.
    _chordsel->addItem(e/*, QVariant(aid)*/);
    // so save it until after widget is fully populated
    acts.push_back(aid);
  }
  if(acts.size() > 1) {
    int i = 0;
    for(auto aid : acts)
      _chordsel->setItemData(i++, QVariant(aid));
  }
  _chordsel->setDisabled(false);
  _chordsel->setCurrentIndex(chord_sel);
}

static void clear_chordsel(void)
{
  _chordsel->clear();
  _chordsel->addItem("No key selected");
  _chordsel->setDisabled(true);
}
