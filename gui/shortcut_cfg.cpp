#include "gui.hpp"

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

// FL_ALT is the same host modifier on both platforms; only its displayed
// name differs. Legacy saved "Option+" values remain valid on Windows.
#ifdef __APPLE__
# define EP128EMU_SHORTCUT_ALT_LABEL "Option+"
#else
# define EP128EMU_SHORTCUT_ALT_LABEL "Alt+"
#endif

static const char *names[EP_SC_COUNT] = {
  "Cycle display mode",
  "Pause / Resume",
  "Reset",
  "Screenshot",
  "Force reset",
  "Quick snapshot save",
  "Quick snapshot load",
  "Next frame",
  "Previous frame",
  "Oldest frame",
  "Latest frame",
  "Record demo — Start",
  "Record audio — Start",
  "Record audio — Stop",
  "Record video — Start",
  "Record video — Stop",
  "Replace floppy — Drive A",
  "Speed — 100%",
  "Speed — No limit",
  "Tape motor — Force on/off",
  "Enter TR-DOS (48 BASIC K prompt)"
};

static const char *defaults[EP_SC_COUNT] = {
  "F9",
  "F10",
  "F11",
  "F12",
  "Ctrl+F11",
  "Ctrl+F9",
  "Ctrl+F10",
  "Right",
  "Left",
  "Home",
  "End",
  EP128EMU_SHORTCUT_ALT_LABEL "M",
  EP128EMU_SHORTCUT_ALT_LABEL "A",
  EP128EMU_SHORTCUT_ALT_LABEL "Shift+A",
  EP128EMU_SHORTCUT_ALT_LABEL "V",
  EP128EMU_SHORTCUT_ALT_LABEL "Shift+V",
  EP128EMU_SHORTCUT_ALT_LABEL "H",
#ifdef __APPLE__
  EP128EMU_SHORTCUT_ALT_LABEL "E",
#else
  "Alt+E",
#endif
  EP128EMU_SHORTCUT_ALT_LABEL "W",
  EP128EMU_SHORTCUT_ALT_LABEL "Shift+T",
// Mac default Option+Shift+D, Windows default Ctrl+Shift+D.
#ifdef __APPLE__
  EP128EMU_SHORTCUT_ALT_LABEL "Shift+D"
#else
  "Ctrl+Shift+D"
#endif
};

static const char *configKeys[EP_SC_COUNT] = {
  "gui.shortcut.displayCycle",
  "gui.shortcut.pause",
  "gui.shortcut.reset",
  "gui.shortcut.screenshot",
  "gui.shortcut.forceReset",
  "gui.shortcut.quickSave",
  "gui.shortcut.quickLoad",
  "gui.shortcut.nextFrame",
  "gui.shortcut.previousFrame",
  "gui.shortcut.oldestFrame",
  "gui.shortcut.latestFrame",
  "gui.shortcut.demoStart",
  "gui.shortcut.audioStart",
  "gui.shortcut.audioStop",
  "gui.shortcut.videoStart",
  "gui.shortcut.videoStop",
  "gui.shortcut.replaceDriveA",
  "gui.shortcut.speed100",
  "gui.shortcut.speedNoLimit",
  "gui.shortcut.tapeForceToggle",
  "gui.shortcut.enterTRDOS"
};

static const char *menuPaths[EP_SC_COUNT] = {
  "Options/Display/Cycle display mode",
  "Machine/Toggle pause",
  "Machine/Reset/Reset",
  "File/Save screenshot",
  "Machine/Reset/Force reset",
  "File/Quick snapshot/Save",
  "File/Quick snapshot/Load",
  "Machine/Frame Navigation/1 frame forward",
  "Machine/Frame Navigation/1 frame backward",
  "Machine/Frame Navigation/Oldest frame",
  "Machine/Frame Navigation/Latest frame",
  "File/Record demo",
  "File/Record audio/Start...",
  "File/Record audio/Stop",
  "File/Record video/Start...",
  "File/Record video/Stop",
  "Options/Disk/Replace floppy/Drive A",
  "Machine/Speed/100%",
  "Machine/Speed/No limit",
  "Machine/Tape/Force motor on",
  "Machine/Beta Disk (experimental)/Enter TR-DOS"
};

static std::string trim(const std::string& s)
{
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char) s[a]))
    a++;
  size_t b = s.size();
  while (b > a && std::isspace((unsigned char) s[b - 1]))
    b--;
  return s.substr(a, b - a);
}

static std::string upper(const std::string& s)
{
  std::string r(s);
  for (size_t i = 0; i < r.size(); i++)
    r[i] = char(std::toupper((unsigned char) r[i]));
  return r;
}

static std::string keyName(int key)
{
  if (key >= (FL_F + 1) && key <= (FL_F + 24)) {
    char b[16];
    std::snprintf(b, sizeof(b), "F%d", key - FL_F);
    return std::string(b);
  }
  if (key >= 'a' && key <= 'z')
    return std::string(1, char(key - 'a' + 'A'));
  if (key >= '0' && key <= '9')
    return std::string(1, char(key));
  if (key >= (FL_KP + '0') && key <= (FL_KP + '9')) {
    char b[16];
    std::snprintf(b, sizeof(b), "KP%d", key - (FL_KP + '0'));
    return std::string(b);
  }
  switch (key) {
  case FL_Page_Up:   return "PageUp";
  case FL_Page_Down: return "PageDown";
  case FL_Left:      return "Left";
  case FL_Right:     return "Right";
  case FL_Home:      return "Home";
  case FL_End:       return "End";
  case FL_Insert:    return "Insert";
  case FL_Delete:    return "Delete";
  case FL_Escape:    return "Escape";
  case FL_Tab:       return "Tab";
  case ' ':          return "Space";
  case ',':          return "Comma";
  case '.':          return "Period";
  default:            return "";
  }
}

static bool reservedMacShortcut(int key, int modifiers, std::string& error)
{
#ifdef __APPLE__
  if ((modifiers & FL_CTRL) && !(modifiers & (FL_ALT | FL_SHIFT)) &&
      key == (FL_F + 8)) {
    error = "Ctrl+F8 is reserved by macOS keyboard navigation and may never reach ep128emu.";
    return true;
  }
#else
  (void) key;
  (void) modifiers;
  (void) error;
#endif
  return false;
}

} // namespace

const char *ep128emuAppShortcutName(int n)
{
  return (n >= 0 && n < EP_SC_COUNT ? names[n] : "");
}

const char *ep128emuAppShortcutDefault(int n)
{
  return (n >= 0 && n < EP_SC_COUNT ? defaults[n] : "");
}

const char *ep128emuAppShortcutConfigKey(int n)
{
  return (n >= 0 && n < EP_SC_COUNT ? configKeys[n] : "");
}

const char *ep128emuAppShortcutMenuPath(int n)
{
  return (n >= 0 && n < EP_SC_COUNT ? menuPaths[n] : "");
}

bool ep128emuParseAppShortcut(const std::string& src, int& key, int& modifiers,
                              std::string& canonical, std::string& error)
{
  std::string s = trim(src);
  key = 0;
  modifiers = 0;
  canonical.clear();
  error.clear();

  if (s.empty()) {
    error = "Shortcut cannot be empty.";
    return false;
  }

  size_t pos = 0;
  std::string keyToken;
  while (pos <= s.size()) {
    size_t next = s.find('+', pos);
    std::string token = trim(s.substr(pos, (next == std::string::npos ?
                                           s.size() : next) - pos));
    if (token.empty()) {
      error = "Invalid empty shortcut component.";
      return false;
    }
    std::string u = upper(token);
    if (next != std::string::npos) {
      if (u == "SHIFT")
        modifiers |= FL_SHIFT;
      else if (u == "CTRL" || u == "CONTROL")
        modifiers |= FL_CTRL;
      else if (u == "ALT" || u == "OPTION")
        modifiers |= FL_ALT;
      else if (u == "CMD" || u == "COMMAND" || u == "META") {
        error = "Command/Meta shortcuts are intentionally unsupported because macOS may consume them before ep128emu.";
        return false;
      }
      else {
        error = "Unknown modifier: " + token;
        return false;
      }
      pos = next + 1;
      continue;
    }
    keyToken = token;
    break;
  }

  std::string u = upper(keyToken);
  if (u.size() >= 2 && u[0] == 'F') {
    int n = std::atoi(u.c_str() + 1);
    if (n >= 1 && n <= 24)
      key = FL_F + n;
  }
  if (!key && u.size() == 1) {
    char c = u[0];
    if (c >= 'A' && c <= 'Z')
      key = int(c - 'A' + 'a');
    else if (c >= '0' && c <= '9')
      key = int(c);
  }
  if (!key && u.size() == 3 && u[0] == 'K' && u[1] == 'P' &&
      u[2] >= '0' && u[2] <= '9')
    key = FL_KP + u[2];
  if (!key) {
    if (u == "PAGEUP") key = FL_Page_Up;
    else if (u == "PAGEDOWN") key = FL_Page_Down;
    else if (u == "LEFT") key = FL_Left;
    else if (u == "RIGHT") key = FL_Right;
    else if (u == "HOME") key = FL_Home;
    else if (u == "END") key = FL_End;
    else if (u == "INSERT") key = FL_Insert;
    else if (u == "DELETE") key = FL_Delete;
    else if (u == "ESC" || u == "ESCAPE") key = FL_Escape;
    else if (u == "TAB") key = FL_Tab;
    else if (u == "SPACE") key = ' ';
    else if (u == "COMMA") key = ',';
    else if (u == "PERIOD" || u == "DOT") key = '.';
  }

  if (!key) {
    error = "Unsupported key: " + keyToken;
    return false;
  }

  return ep128emuFormatAppShortcut(key, modifiers, canonical, error);
}

bool ep128emuFormatAppShortcut(int key, int modifiers,
                               std::string& canonical, std::string& error)
{
  canonical.clear();
  error.clear();

  if (key >= 'A' && key <= 'Z')
    key = key - 'A' + 'a';

  std::string kn = keyName(key);
  if (!key || kn.empty()) {
    error = "Unsupported key.";
    return false;
  }

  modifiers &= (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);
  if (modifiers & FL_META) {
    error = "Command/Meta shortcuts are intentionally unsupported because macOS may consume them before ep128emu.";
    return false;
  }

  if (reservedMacShortcut(key, modifiers, error))
    return false;

  if (modifiers & FL_CTRL)
    canonical += "Ctrl+";
  if (modifiers & FL_ALT)
    canonical += EP128EMU_SHORTCUT_ALT_LABEL;
  if (modifiers & FL_SHIFT)
    canonical += "Shift+";
  canonical += kn;
  return true;
}

Fl_Shortcut ep128emuMakeFLTKShortcut(int key, int modifiers)
{
  return Fl_Shortcut(key | modifiers);
}

Ep128EmuGUI_ShortcutInput::Ep128EmuGUI_ShortcutInput(int x, int y,
                                                       int w, int h)
  : Fl_Input(x, y, w, h)
{
  textsize(12);
  tooltip("Click this field, then press the desired key or key combination.");
}

int Ep128EmuGUI_ShortcutInput::handle(int event)
{
  if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
    int key = Fl::event_key();

    // Modifier-only key events are part of building a combination; wait for
    // the actual non-modifier key before replacing the displayed shortcut.
    if (key == FL_Shift_L || key == FL_Shift_R ||
        key == FL_Control_L || key == FL_Control_R ||
        key == FL_Alt_L || key == FL_Alt_R ||
        key == FL_Meta_L || key == FL_Meta_R)
      return 1;

    int modifiers = Fl::event_state() &
                    (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);
    std::string canonical, error;
    if (ep128emuFormatAppShortcut(key, modifiers, canonical, error)) {
      value(canonical.c_str());
      position(int(canonical.size()), int(canonical.size()));
      redraw();
    }
    else {
      fl_beep(FL_BEEP_ERROR);
    }
    return 1;
  }
  if (event == FL_KEYUP)
    return 1;
  return Fl_Input::handle(event);
}

Ep128EmuGUI_ShortcutConfigWindow::Ep128EmuGUI_ShortcutConfigWindow(Ep128EmuGUI& gui_)
  : gui(gui_), window((Fl_Double_Window *) 0)
{
  for (int i = 0; i < EP_SC_COUNT; i++)
    inputs[i] = (Ep128EmuGUI_ShortcutInput *) 0;

  window = new Fl_Double_Window(650, 545, "Application shortcuts");
  window->color(48);
  window->begin();

#ifdef __APPLE__
  const char *modifierHelp =
      "Supported modifiers: Ctrl, Option, Shift. Command is intentionally disabled.";
#else
  const char *modifierHelp =
      "Supported modifiers: Ctrl, Alt, Shift. Command/Meta is intentionally disabled.";
#endif
  Fl_Box *note = new Fl_Box(15, 10, 620, 40);
  note->copy_label((std::string(
      "Click a shortcut field, then press the desired key or key combination.\n")
      + modifierHelp).c_str());
  note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  note->labelsize(12);

  Fl_Scroll *scroll = new Fl_Scroll(10, 55, 630, 420);
  scroll->box(FL_THIN_DOWN_BOX);
  scroll->begin();
  for (int i = 0; i < EP_SC_COUNT; i++) {
    int y = 65 + i * 27;
    Fl_Box *label = new Fl_Box(20, y, 235, 23, ep128emuAppShortcutName(i));
    label->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    label->labelsize(12);
    inputs[i] = new Ep128EmuGUI_ShortcutInput(265, y, 330, 23);
  }
  scroll->end();

  Fl_Button *defaultsButton = new Fl_Button(15, 495, 130, 30, "Restore Defaults");
  defaultsButton->callback(&cbDefaults, this);
  Fl_Button *applyButton = new Fl_Button(330, 495, 90, 30, "Apply");
  applyButton->callback(&cbApply, this);
  Fl_Button *cancelButton = new Fl_Button(430, 495, 90, 30, "Cancel");
  cancelButton->callback(&cbCancel, this);
  Fl_Button *okButton = new Fl_Button(530, 495, 90, 30, "OK");
  okButton->callback(&cbOK, this);

  window->end();
  window->set_non_modal();
  window->callback(&cbCancel, this);
}

Ep128EmuGUI_ShortcutConfigWindow::~Ep128EmuGUI_ShortcutConfigWindow()
{
  delete window;
}

void Ep128EmuGUI_ShortcutConfigWindow::loadValues()
{
  for (int i = 0; i < EP_SC_COUNT; i++)
    inputs[i]->value(gui.appShortcutStrings[i].c_str());
}

bool Ep128EmuGUI_ShortcutConfigWindow::apply()
{
  std::string values[EP_SC_COUNT];
  for (int i = 0; i < EP_SC_COUNT; i++)
    values[i] = inputs[i]->value();

  std::string error;
  if (!gui.setApplicationShortcuts(values, error)) {
    gui.errorMessage(error.c_str());
    return false;
  }
  loadValues();
  return true;
}

void Ep128EmuGUI_ShortcutConfigWindow::cbApply(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI_ShortcutConfigWindow *>(v)->apply();
}

void Ep128EmuGUI_ShortcutConfigWindow::cbOK(Fl_Widget *, void *v)
{
  Ep128EmuGUI_ShortcutConfigWindow *w =
      reinterpret_cast<Ep128EmuGUI_ShortcutConfigWindow *>(v);
  if (w->apply())
    w->window->hide();
}

void Ep128EmuGUI_ShortcutConfigWindow::cbCancel(Fl_Widget *, void *v)
{
  Ep128EmuGUI_ShortcutConfigWindow *w =
      reinterpret_cast<Ep128EmuGUI_ShortcutConfigWindow *>(v);
  w->loadValues();
  w->window->hide();
}

void Ep128EmuGUI_ShortcutConfigWindow::cbDefaults(Fl_Widget *, void *v)
{
  Ep128EmuGUI_ShortcutConfigWindow *w =
      reinterpret_cast<Ep128EmuGUI_ShortcutConfigWindow *>(v);
  for (int i = 0; i < EP_SC_COUNT; i++)
    w->inputs[i]->value(ep128emuAppShortcutDefault(i));
}

void Ep128EmuGUI_ShortcutConfigWindow::show()
{
  loadValues();
  if (!window->shown())
    gui.positionAuxiliaryWindow(window);
  window->show();
}

void Ep128EmuGUI_ShortcutConfigWindow::hide()
{
  window->hide();
}

bool Ep128EmuGUI_ShortcutConfigWindow::shown() const
{
  return bool(window->shown());
}
