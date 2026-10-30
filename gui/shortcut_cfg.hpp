#ifndef EP128EMU_SHORTCUT_CFG_HPP
#define EP128EMU_SHORTCUT_CFG_HPP

#include <string>
#include <FL/Enumerations.H>
#include <FL/Fl_Input.H>

class Ep128EmuGUI;
class Fl_Double_Window;
class Fl_Widget;

enum Ep128EmuAppShortcutID {
  EP_SC_DISPLAY_CYCLE = 0,
  EP_SC_PAUSE,
  EP_SC_RESET,
  EP_SC_SCREENSHOT,
  EP_SC_FORCE_RESET,
  EP_SC_QUICK_SAVE,
  EP_SC_QUICK_LOAD,
  EP_SC_NEXT_FRAME,
  EP_SC_PREVIOUS_FRAME,
  EP_SC_OLDEST_FRAME,
  EP_SC_LATEST_FRAME,
  EP_SC_DEMO_START,
  EP_SC_AUDIO_START,
  EP_SC_AUDIO_STOP,
  EP_SC_VIDEO_START,
  EP_SC_VIDEO_STOP,
  EP_SC_REPLACE_DRIVE_A,
  EP_SC_SPEED_100,
  EP_SC_SPEED_NOLIMIT,
  EP_SC_TAPE_FORCE_TOGGLE,
  EP_SC_TRDOS_ENTER,
  EP_SC_COUNT
};

const char *ep128emuAppShortcutName(int n);
const char *ep128emuAppShortcutDefault(int n);
const char *ep128emuAppShortcutConfigKey(int n);
const char *ep128emuAppShortcutMenuPath(int n);

bool ep128emuParseAppShortcut(const std::string& s, int& key, int& modifiers,
                              std::string& canonical, std::string& error);
bool ep128emuFormatAppShortcut(int key, int modifiers,
                               std::string& canonical, std::string& error);
Fl_Shortcut ep128emuMakeFLTKShortcut(int key, int modifiers);

// Pause-local frame-step keys are host shortcuts only while actually paused.
// Key-up routing must follow the route chosen for key-down, otherwise a key
// can become logically stuck in the emulated machine across a Pause change.
inline bool ep128emuPauseLocalFrameStepConsumesEvent(
    bool isKeyPress, bool isPaused, bool keyDownWasHostShortcut)
{
  return (isKeyPress ? isPaused : keyDownWasHostShortcut);
}

// Return -1 for coarse Previous, +1 for coarse Next, or 0 for no match.
inline int ep128emuPauseLocalFrameJumpDirection(
    int keyCode, int modifiers,
    int previousKey, int nextKey,
    bool previousEnabled, bool nextEnabled,
    bool previousPhysicalDown, bool nextPhysicalDown)
{
  if (modifiers != FL_SHIFT)
    return 0;
  if (previousEnabled &&
      (keyCode == previousKey ||
       (previousPhysicalDown && !nextPhysicalDown)))
    return -1;
  if (nextEnabled &&
      (keyCode == nextKey ||
       (nextPhysicalDown && !previousPhysicalDown)))
    return 1;
  return 0;
}

class Ep128EmuGUI_ShortcutInput : public Fl_Input {
 public:
  Ep128EmuGUI_ShortcutInput(int x, int y, int w, int h);
  virtual int handle(int event);
};

class Ep128EmuGUI_ShortcutConfigWindow {
 private:
  Ep128EmuGUI& gui;
  Fl_Double_Window *window;
  Ep128EmuGUI_ShortcutInput *inputs[EP_SC_COUNT];

  static void cbApply(Fl_Widget *, void *v);
  static void cbOK(Fl_Widget *, void *v);
  static void cbCancel(Fl_Widget *, void *v);
  static void cbDefaults(Fl_Widget *, void *v);

  bool apply();
  void loadValues();

 public:
  Ep128EmuGUI_ShortcutConfigWindow(Ep128EmuGUI& gui_);
  ~Ep128EmuGUI_ShortcutConfigWindow();

  void show();
  void hide();
  bool shown() const;
};

#endif
