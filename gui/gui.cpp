
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2017 Istvan Varga <istvanv@users.sourceforge.net>
// https://github.com/istvan-v/ep128emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include "gui.hpp"
#include "titlebar_fields.hpp"
#include "sdd_shutdown_guard.hpp"
#include "archive_drop.hpp"
#include "archive_standard128_roms.hpp"
#ifdef WIN32
# include "win_native_drop.hpp"
# include "win_archive_extract.hpp"
# include "legacy_alt_shortcut_state.hpp"
# include "enterprise_autorun_cadence.hpp"
# include <mmsystem.h>  // winmm PlaySoundW for accepted EF04B relay WAV
# include <FL/platform.H>  // FLTK 1.4 native Win32 MSG, via supported header
#endif
#include <FL/fl_ask.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Window.H>
#include "guicolor.hpp"
#include "ep128vm.hpp"
#include "zx128vm.hpp"
#include "betadisk.hpp"
#if defined(__APPLE__) || defined(WIN32)
#  include "speccydos_trial_media.hpp"
#  include "betadisk_trial_media.hpp"
#  include "betadisk_bundled_rom.hpp"
#  include "betadisk_trdos_keyboard.hpp"
#endif
#include "cpc464vm.hpp"
#include "tvc64vm.hpp"
#ifdef __APPLE__
#  include "macos_activity.hpp"
#endif
#include "pngwrite.hpp"
#include <FL/Fl_Gl_Window.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>    // fl_fopen: UTF-8 names on Windows, macOS, Linux

#include <typeinfo>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <cstdlib>
#ifndef WIN32
# include <sys/stat.h>
#endif

#ifdef __APPLE__
#  define EP128EMU_ALT_KEY_TEXT  "⌥"
#else
#  define EP128EMU_ALT_KEY_TEXT  "Alt+"
#endif

#ifdef WIN32
#  include "win32_alt_firstpress.hpp"
// Diagnostics are opt-in; never record ordinary typed input or user filenames.
// FLTK/Win32 event and callback path is the missing evidence for Parallels
// every-second left Alt+T/U. The log stays in the Windows TEMP directory.
static void ep128emuWindowsGuiTrace(const char *stage, int eventCode,
                                    int keyCode, int modifiers)
{
  const char *enabled = std::getenv("EP128EMU_INPUT_TRACE");
  if (!enabled || std::strcmp(enabled, "1") != 0)
    return;
  wchar_t tmp[MAX_PATH + 1] = { 0 };
  DWORD n = GetTempPathW(MAX_PATH, tmp);
  if (n == 0 || n >= MAX_PATH)
    return;
  const std::wstring fileName(std::wstring(tmp) + L"ep128emu-0.43-input.log");
  std::FILE *f = _wfopen(fileName.c_str(), L"a");
  if (!f)
    return;
  std::fprintf(f,
               "ms=%lu stage=%s thread=%lu event=%d key=0x%X mod=0x%X "
               "winmsg=0x%X wparam=0x%llX lparam=0x%llX "
               "leftAlt=%d rightAlt=%d foreground=%p focus=%p\n",
               (unsigned long) GetTickCount(), stage,
               (unsigned long) GetCurrentThreadId(),
               eventCode, keyCode, modifiers,
               (unsigned int) fl_msg.message,
               (unsigned long long) uintptr_t(fl_msg.wParam),
               (unsigned long long) uintptr_t(fl_msg.lParam),
               int((GetKeyState(VK_LMENU) & 0x8000) != 0),
               int((GetKeyState(VK_RMENU) & 0x8000) != 0),
               (void *) GetForegroundWindow(), (void *) GetFocus());
  std::fclose(f);
}

// The accepted 0.25 Win64 Alt+U/T fix recovered the FIRST WM_SYSKEYDOWN:
// Windows supplied the correct native VK but FLTK reported FL_Alt_L.
// Apply the same narrow misdecode guard to all letters, digits and
// layout-derived comma/period. Never remap actual Alt or AltGr events.
static int ep128emuWindowsRecoverFirstAltKey(int eventCode, int keyCode)
{
  if (eventCode != FL_KEYDOWN && eventCode != FL_SHORTCUT)
    return keyCode;
  const HKL activeLayout = GetKeyboardLayout(0);
  const int result = ep128emuWin32RecoverFirstAltKey(
      keyCode, FL_Alt_L, unsigned(fl_msg.message),
      unsigned(fl_msg.wParam), (GetKeyState(VK_LMENU) & 0x8000) != 0,
      (GetKeyState(VK_RMENU) & 0x8000) != 0,
      (GetKeyState(VK_SHIFT) & 0x8000) != 0,
      int(VkKeyScanExW(L',', activeLayout)),
      int(VkKeyScanExW(L'.', activeLayout)));
  if (result != keyCode)
    ep128emuWindowsGuiTrace("ALT-FIRST-VK-RECOVERED", eventCode,
                            result, Fl::event_state());
  return result;
}
#endif

struct Ep128EmuArchiveProgramDialogState {
  Fl_Window *window;
  Fl_Hold_Browser *browser;
  int selectedLine;
};

static void ep128emuArchiveProgramRunCallback(Fl_Widget *, void *userData)
{
  Ep128EmuArchiveProgramDialogState *state =
      reinterpret_cast<Ep128EmuArchiveProgramDialogState *>(userData);
  if (!state || !state->window || !state->browser)
    return;
  int n = state->browser->value();
  if (n < 1)
    return;
  state->selectedLine = n;
  state->window->hide();
}

static void ep128emuArchiveProgramCancelCallback(Fl_Widget *, void *userData)
{
  Ep128EmuArchiveProgramDialogState *state =
      reinterpret_cast<Ep128EmuArchiveProgramDialogState *>(userData);
  if (!state || !state->window)
    return;
  state->selectedLine = 0;
  state->window->hide();
}

struct Ep128EmuArchiveRunDialogState {
  Fl_Window *window;
  int choice;                 // 0 cancel, 1 current, 2 standard 128K
};

static void ep128emuArchiveRunChoiceCallback(Fl_Widget *, void *userData)
{
  Ep128EmuArchiveRunDialogState *state =
      reinterpret_cast<Ep128EmuArchiveRunDialogState *>(userData);
  if (!state || !state->window)
    return;
  state->window->hide();
}

static void ep128emuArchiveRunCurrentCallback(Fl_Widget *w, void *userData)
{
  Ep128EmuArchiveRunDialogState *state =
      reinterpret_cast<Ep128EmuArchiveRunDialogState *>(userData);
  if (!state)
    return;
  state->choice = 1;
  ep128emuArchiveRunChoiceCallback(w, userData);
}

static void ep128emuArchiveRunStandardCallback(Fl_Widget *w, void *userData)
{
  Ep128EmuArchiveRunDialogState *state =
      reinterpret_cast<Ep128EmuArchiveRunDialogState *>(userData);
  if (!state)
    return;
  state->choice = 2;
  ep128emuArchiveRunChoiceCallback(w, userData);
}

static void ep128emuArchiveRunCancelCallback(Fl_Widget *w, void *userData)
{
  Ep128EmuArchiveRunDialogState *state =
      reinterpret_cast<Ep128EmuArchiveRunDialogState *>(userData);
  if (!state)
    return;
  state->choice = 0;
  ep128emuArchiveRunChoiceCallback(w, userData);
}

static std::string ep128emuJoinPath(const std::string& directory,
                                    const char *fileName)
{
  std::string s(directory);
  if (!s.empty() && s[s.size() - 1] != '/' && s[s.size() - 1] != '\\')
    s += '/';
  s += fileName;
  return s;
}

static bool ep128emuReadableFile(const std::string& fileName)
{
  std::FILE *f = Ep128Emu::fileOpen(fileName.c_str(), "rb");
  if (!f)
    return false;
  std::fclose(f);
  return true;
}

// T084B/T084C temporary Standard128K is independent from the last GUI ROM
// browse location. Resolve a COMPLETE ROM set from installed/configured roots;
// both availability and apply must use this identical owner selection.
static bool ep128emuFindArchiveStandard128KROMs(
    const std::string& guiROMDirectory,
    const Ep128Emu::EmulatorConfiguration& cfg,
    Ep128EmuArchiveStandard128::ROMSet& result)
{
  std::vector<std::string> dirs;
  dirs.push_back(guiROMDirectory);
  for (size_t i=0;i<68;i++) {
    if (cfg.memory.rom[i].file.empty()) continue;
    std::string parent, base;
    Ep128Emu::splitPath(cfg.memory.rom[i].file,parent,base);
    if (!parent.empty()) dirs.push_back(parent);
  }
  // Newer installation layout on all desktop platforms.
  dirs.push_back(ep128emuJoinPath(
      Ep128Emu::getEp128EmuHomeDirectory(),"roms"));
#ifdef __APPLE__
  // Legacy macOS distributions kept the firmware in Documents/ep128emu/roms.
  // This folder is an optional lookup only: nothing is written into it.
  if (const char *home=std::getenv("HOME")) {
    if (*home) dirs.push_back(std::string(home)+"/Documents/ep128emu/roms");
  }
#endif
  return Ep128EmuArchiveStandard128::resolve(
      dirs,ep128emuReadableFile,result);
}

struct Ep128EmuEnterpriseKey {
  int code;
  bool shift;
};

static bool ep128emuEnterpriseKeyForChar(char c, Ep128EmuEnterpriseKey& k)
{
  if (c >= 'a' && c <= 'z')
    c = char(c - 'a' + 'A');
  k.shift = false;
  switch (c) {
  case 'N': k.code = 0x00; return true;
  case 'B': k.code = 0x02; return true;
  case 'C': k.code = 0x03; return true;
  case 'V': k.code = 0x04; return true;
  case 'X': k.code = 0x05; return true;
  case 'Z': k.code = 0x06; return true;
  case 'H': k.code = 0x08; return true;
  case 'G': k.code = 0x0A; return true;
  case 'D': k.code = 0x0B; return true;
  case 'F': k.code = 0x0C; return true;
  case 'S': k.code = 0x0D; return true;
  case 'A': k.code = 0x0E; return true;
  case 'U': k.code = 0x10; return true;
  case 'Q': k.code = 0x11; return true;
  case 'Y': k.code = 0x12; return true;
  case 'R': k.code = 0x13; return true;
  case 'T': k.code = 0x14; return true;
  case 'E': k.code = 0x15; return true;
  case 'W': k.code = 0x16; return true;
  case '7': k.code = 0x18; return true;
  case '1': k.code = 0x19; return true;
  case '6': k.code = 0x1A; return true;
  case '4': k.code = 0x1B; return true;
  case '5': k.code = 0x1C; return true;
  case '3': k.code = 0x1D; return true;
  case '2': k.code = 0x1E; return true;
  case '8': k.code = 0x28; return true;
  case '9': k.code = 0x2A; return true;
  case '-': k.code = 0x2B; return true;
  case '0': k.code = 0x2C; return true;
  case 'J': k.code = 0x30; return true;
  case 'K': k.code = 0x32; return true;
  case ';': k.code = 0x33; return true;
  case 'L': k.code = 0x34; return true;
  case ':': k.code = 0x35; return true;
  case '\r':
  case '\n': k.code = 0x3E; return true;
  case 'M': k.code = 0x40; return true;
  case ',': k.code = 0x42; return true;
  case '/': k.code = 0x43; return true;
  case '.': k.code = 0x44; return true;
  case ' ': k.code = 0x46; return true;
  case 'I': k.code = 0x48; return true;
  case 'O': k.code = 0x4A; return true;
  case '@': k.code = 0x4B; return true;
  case 'P': k.code = 0x4C; return true;
  case '[': k.code = 0x4D; return true;
  case '"': k.code = 0x1E; k.shift = true; return true;
  default:
    break;
  }
  return false;
}

static int ep128emuFindMenuIndexByCallback(Fl_Menu_Bar *menuBar,
                                            Fl_Callback *callback)
{
  if (!menuBar || !callback || !menuBar->menu())
    return -1;
  const Fl_Menu_Item *items = menuBar->menu();
  const int n = menuBar->size();
  for (int i = 0; i < n; i++) {
    if (items[i].callback() == callback)
      return i;
  }
  return -1;
}

static std::string ep128emuMenuShortcutLabel(int key, int modifiers)
{
  const char *s = fl_shortcut_label(
      unsigned(ep128emuMakeFLTKShortcut(key, modifiers)));
  return (s ? std::string(s) : std::string(""));
}

static std::string ep128emuTimelineMenuLabel(const char *baseLabel,
                                              int key, int modifiers)
{
  std::string label(baseLabel);
  std::string shortcut;
#ifdef __APPLE__
  if (modifiers == 0 && key == FL_Home)
    shortcut = "Fn + ← / Home";
  else if (modifiers == 0 && key == FL_End)
    shortcut = "Fn + → / End";
  else
#endif
    shortcut = ep128emuMenuShortcutLabel(key, modifiers);

  if (!shortcut.empty())
    label += " (" + shortcut + ")";
  return label;
}

static std::string ep128emuCoarseTimelineMenuLabel(const char *baseLabel,
                                                    int key, int modifiers)
{
  std::string label(baseLabel);
  if (modifiers != 0)
    return label;
  std::string shortcut;
#ifdef __APPLE__
  if (key == FL_Left)
    shortcut = "⇧ ←";
  else if (key == FL_Right)
    shortcut = "⇧ →";
  else
#endif
    shortcut = ep128emuMenuShortcutLabel(key, FL_SHIFT);
  if (!shortcut.empty())
    label += " (" + shortcut + ")";
  return label;
}

static std::string ep128emuLowerExtension(const std::string& fileName)
{
  size_t slashPos = fileName.find_last_of("/\\");
  size_t dotPos = fileName.find_last_of('.');
  if (dotPos == std::string::npos ||
      (slashPos != std::string::npos && dotPos < slashPos))
    return std::string("");
  std::string ext = fileName.substr(dotPos);
  Ep128Emu::stringToLowerCase(ext);
  return ext;
}

static bool ep128emuFileHasRuntimeMemoryState(const Ep128Emu::File& f)
{
  return (f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_MEMORY_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZXMEM_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZX_SNA_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZX_Z80_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CPCMEM_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CPC_SNA_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_TVCMEM_STATE));
}

static int ep128emuMachineType(Ep128Emu::VirtualMachine& vm)
{
  if (typeid(vm) == typeid(Ep128::Ep128VM))
    return 0;
  if (typeid(vm) == typeid(ZX128::ZX128VM))
    return 1;
  if (typeid(vm) == typeid(CPC464::CPC464VM))
    return 2;
  if (typeid(vm) == typeid(TVC64::TVC64VM))
    return 3;
  return -1;
}

static const char *ep128emuMachineShortName(Ep128Emu::VirtualMachine& vm)
{
  static const char *names[4] = { "EP", "ZX", "CPC", "TVC" };
  int n = ep128emuMachineType(vm);
  return (n >= 0 && n < 4 ? names[n] : "VM");
}

// Actual VM-owned expansion, not a potentially stale GUI toggle.
static const char *ep128emuSpectrumExpansionName(
    const Ep128Emu::VirtualMachine& vm)
{
  if (typeid(vm) != typeid(ZX128::ZX128VM)) return "";
  const ZX128::ZX128VM& zx=static_cast<const ZX128::ZX128VM&>(vm);
  if (zx.getBetaDiskForTesting()) return "Beta Disk / TR-DOS";
  if (zx.getSpeccyDOSForTesting()) return "SpeccyDOS";
  return "";
}

static std::string ep128emuHomeFilePath(const char *fileName)
{
  std::string s(Ep128Emu::getEp128EmuHomeDirectory());
#ifdef WIN32
  s += '\\';
#else
  s += '/';
#endif
  s += fileName;
  return s;
}

static void ep128emuFormatElapsed(char *buf, size_t bufSize, double t)
{
  if (!buf || bufSize < 1)
    return;
  if (t < 0.0)
    t = 0.0;
  unsigned long seconds = (unsigned long) t;
  unsigned long h = seconds / 3600UL;
  unsigned long m = (seconds / 60UL) % 60UL;
  unsigned long s = seconds % 60UL;
  if (h > 0UL)
    std::snprintf(buf, bufSize, "%02lu:%02lu:%02lu", h, m, s);
  else
    std::snprintf(buf, bufSize, "%02lu:%02lu", m, s);
}

static int ep128emuRecentMenuIndex(Fl_Widget *o)
{
  Fl_Menu_ *m = dynamic_cast<Fl_Menu_ *>(o);
  if (!m || !m->mvalue() || !m->mvalue()->label())
    return -1;
  const char *p = m->mvalue()->label();
  int n = 0;
  while (*p >= '0' && *p <= '9') {
    n = (n * 10) + int(*p - '0');
    p++;
  }
  return (n >= 1 && n <= 10 ? n - 1 : -1);
}

static bool ep128emuFileExists(const std::string& fileName)
{
  std::FILE *f = Ep128Emu::fileOpen(fileName.c_str(), "rb");
  if (!f)
    return false;
  std::fclose(f);
  return true;
}

static bool ep128emuIsCPCDiskImage(const std::string& fileName)
{
  unsigned char buf[34];
  std::FILE *f = std::fopen(fileName.c_str(), "rb");
  if (!f)
    return false;
  size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  if (n < 34)
    return false;
  static const char h1[] = "MV - CPCEMU Disk-File";
  static const char h2[] = "EXTENDED CPC DSK File";
  return (std::memcmp(buf, h1, sizeof(h1) - 1) == 0 ||
          std::memcmp(buf, h2, sizeof(h2) - 1) == 0);
}

#ifdef LINUX_FLTK_VERSION
#  undef LINUX_FLTK_VERSION
#endif
#ifdef WIN32
#  define WIN32_LEAN_AND_MEAN   1
#  include <windows.h>
#else
#  include <unistd.h>
#  include <pthread.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <cerrno>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif
#if defined(__linux) || defined(__linux__)
#  include <X11/Xlib.h>
#  if defined(FL_MAJOR_VERSION) && defined(FL_MINOR_VERSION) && \
      defined(FL_PATCH_VERSION)
#    define LINUX_FLTK_VERSION  ((FL_MAJOR_VERSION * 10000)     \
                                 + (FL_MINOR_VERSION * 100) + FL_PATCH_VERSION)
#    if (LINUX_FLTK_VERSION >= 10302)
#      include <X11/XKBlib.h>
#    endif
#  endif
#endif

#include <FL/x.H>

#ifndef WIN32
extern char **environ;
#endif

#ifdef WIN32
// macOS T081K (58adb44) activates the emulator after a file drop.
// On Windows OLE DragDrop returns without activating the target window.
// Only call this from the deferred, user-initiated drop callback, after the
// OLE drop operation is complete: never from DragEnter/DragOver or the
// emulator's normal update loop. Bringing the window to the top of the Z
// order also preserves visibility if Windows foreground activation is denied.
static void ep128emuActivateWindowsDroppedFileWindow(
    Fl_Window *topLevelWindow, Fl_Window *displayWindow)
{
  // Never raise the emulator above a genuine modal file/setting dialog.
  if (!topLevelWindow || !topLevelWindow->shown() || Fl::modal())
    return;
  HWND h = reinterpret_cast<HWND>(fl_xid(topLevelWindow));
  if (!h || !IsWindow(h))
    return;
  if (IsIconic(h))
    ShowWindow(h, SW_RESTORE);
  (void) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  (void) SetForegroundWindow(h);
  // FLTK's keyboard focus and Win32 foreground ownership are different.
  // The GL child needs focus to receive guest and application shortcuts.
  if (displayWindow && displayWindow->shown())
    displayWindow->take_focus();
}

static std::wstring ep128emuFFWide(const std::string& s)
{
  if(s.empty()) return std::wstring();
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,0,0);
  if(n<=0) return std::wstring();
  std::vector<wchar_t> out(static_cast<size_t>(n),L'\0');
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,&out[0],n))
    return std::wstring();
  return std::wstring(&out[0]);
}
static std::string ep128emuFFUTF8(const std::wstring& s)
{
  if(s.empty()) return std::string();
  int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,0,0,0,0);
  if(n<=0) return std::string();
  std::vector<char> out(static_cast<size_t>(n),'\0');
  if(!WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,&out[0],n,0,0))
    return std::string();
  return std::string(&out[0]);
}
static std::wstring ep128emuFFQuote(const std::wstring& s)
{
  std::wstring out(L"\"");
  unsigned backslashes=0;
  for(size_t i=0;i<s.size();++i) {
    wchar_t c=s[i];
    if(c==L'\\') {++backslashes;continue;}
    if(c==L'\"') {
      out.append(size_t(backslashes*2U+1U),L'\\');
      out+=L'\"';backslashes=0;continue;
    }
    out.append(size_t(backslashes),L'\\');backslashes=0;out+=c;
  }
  out.append(size_t(backslashes*2U),L'\\');out+=L'\"';
  return out;
}
#endif

#ifdef WIN32
// Host-native counterpart of accepted macOS EF04B NSSound. Resolve the
// bundled 48-kHz stereo WAV beside ep128emu.exe, never via process CWD (which
// may be Program Files, FileIO override, or a Parallels shared directory).
// SND_NOSTOP prevents overlapped/retriggered clicks just like Cocoa isPlaying.
static bool ep128emuPlayWindowsRelayClick()
{
  wchar_t exePath[32768];
  DWORD len = GetModuleFileNameW(NULL, exePath, 32768);
  if (!len || len >= 32768) return false;
  std::wstring path(exePath, len);
  size_t slash = path.find_last_of(L"/\\");
  if (slash == std::wstring::npos) return false;
  path.resize(slash + 1U);
  const std::wstring exeDir(path);
  path += L"EP_rele.wav";
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    // Legacy Win7 portable layout places relay audio in resource/.
    // Keep Win64 installations (file beside EXE) and avoid a CWD lookup.
    path = exeDir + L"resource\\EP_rele.wav";
    attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
      return false;
  }
  return (PlaySoundW(path.c_str(), NULL,
          SND_FILENAME | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP) != FALSE);
}
#endif

static std::string findFFmpegExecutable()
{
#ifdef WIN32
  wchar_t modulePath[32768];
  DWORD n=GetModuleFileNameW(NULL,modulePath,32768);
  if(n==0||n>=32768) return std::string();
  std::wstring dir(modulePath,n);
  size_t slash=dir.find_last_of(L"/\\");
  if(slash==std::wstring::npos) return std::string();
  dir.resize(slash+1);
  dir+=L"ep128emu-ffmpeg.exe";
  DWORD attrs=GetFileAttributesW(dir.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_DIRECTORY))
    return std::string();
  return ep128emuFFUTF8(dir);
#elif defined(__APPLE__)
  uint32_t pathSize = 0;
  (void) _NSGetExecutablePath((char *) 0, &pathSize);
  if (pathSize > 1) {
    std::vector<char> executablePath(size_t(pathSize) + 1, '\0');
    if (_NSGetExecutablePath(&(executablePath.front()), &pathSize) == 0) {
      std::string p(&(executablePath.front()));
      size_t n = p.rfind("/Contents/MacOS/");
      if (n != std::string::npos) {
        std::string bundled =
            p.substr(0, n) + "/Contents/MacOS/ep128emu-ffmpeg";
        if (access(bundled.c_str(), X_OK) == 0)
          return bundled;
      }
    }
  }
  return std::string("");
#else
  static const char * const candidates[] = {
    "/usr/bin/ffmpeg",
    "/usr/local/bin/ffmpeg",
    (const char *) 0
  };
  for (int i = 0; candidates[i]; i++) {
    if (access(candidates[i], X_OK) == 0)
      return std::string(candidates[i]);
  }
  return std::string("");
#endif
}

static bool convertVideoCaptureToMP4(const std::string& inputFile,
                                     const std::string& outputFile,
                                     bool pixelGraphics,
                                     std::string& errorMessage)
{
  std::string ffmpegPath = findFFmpegExecutable();
  if (ffmpegPath.empty()) {
    errorMessage = "FFmpeg was not found. The temporary AVI file was preserved.";
    return false;
  }

  std::vector<std::string> args;
  args.push_back(ffmpegPath);
  args.push_back("-nostdin");
  args.push_back("-hide_banner");
  args.push_back("-loglevel");
  args.push_back("error");
#ifdef WIN32
  // MinGW minimal FFmpeg on Windows misdecodes the emulator's real
  // MSRLE palette when host CPU acceleration is enabled. The same
  // AVI is pixel-correct with -cpuflags 0 on both x86 and x64.
  args.push_back("-cpuflags");
  args.push_back("0");
#endif
  args.push_back("-y");
  args.push_back("-i");
  args.push_back(inputFile);
  args.push_back("-map");
  args.push_back("0:v:0");
  args.push_back("-map");
  args.push_back("0:a:0");
  args.push_back("-c:v");
  args.push_back("libx264");
  args.push_back("-preset");
  args.push_back("medium");
  args.push_back("-crf");
  args.push_back("18");
  args.push_back("-pix_fmt");
  args.push_back(pixelGraphics ? "yuv444p" : "yuv420p");
  args.push_back("-profile:v");
  args.push_back(pixelGraphics ? "high444" : "high");
  args.push_back("-c:a");
  args.push_back("aac");
  args.push_back("-b:a");
  args.push_back("128k");
  args.push_back("-fps_mode");
  args.push_back("passthrough");
  args.push_back("-movflags");
  args.push_back("+faststart");
  args.push_back(outputFile);

  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (size_t i = 0; i < args.size(); i++)
    argv.push_back(const_cast<char *>(args[i].c_str()));
  argv.push_back((char *) 0);

#ifdef WIN32
  std::wstring executable=ep128emuFFWide(ffmpegPath);
  std::wstring commandLine;
  for(size_t i=0;i<args.size();++i) {
    std::wstring arg=ep128emuFFWide(args[i]);
    if(arg.empty()) {
      errorMessage="Invalid UTF-8 video filename/argument. AVI preserved.";
      return false;
    }
    if(i) commandLine+=L' ';
    commandLine+=ep128emuFFQuote(arg);
  }
  std::vector<wchar_t> cmd(commandLine.begin(),commandLine.end());
  cmd.push_back(L'\0');
  STARTUPINFOW startup={};
  startup.cb=sizeof(startup);
  PROCESS_INFORMATION child={};
  if(!CreateProcessW(executable.c_str(),&cmd[0],NULL,NULL,FALSE,
                     CREATE_NO_WINDOW,NULL,NULL,&startup,&child)) {
    errorMessage="Could not start bundled FFmpeg. AVI preserved.";
    return false;
  }
  DWORD waited=WaitForSingleObject(child.hProcess,INFINITE);
  DWORD code=1;
  bool success=(waited==WAIT_OBJECT_0 &&
                GetExitCodeProcess(child.hProcess,&code) && code==0);
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  if(!success) {
    errorMessage="FFmpeg conversion failed. Temporary AVI preserved.";
    return false;
  }
  return true;
#else
  pid_t childPID = 0;
  int err = posix_spawn(&childPID, ffmpegPath.c_str(),
                        (posix_spawn_file_actions_t *) 0,
                        (posix_spawnattr_t *) 0,
                        &(argv.front()), environ);
  if (err != 0) {
    errorMessage = "Could not start FFmpeg. The temporary AVI file was preserved.";
    return false;
  }

  int status = 0;
  while (waitpid(childPID, &status, 0) < 0) {
    if (errno != EINTR) {
      errorMessage = "Could not wait for FFmpeg. The temporary AVI file was preserved.";
      return false;
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    errorMessage = "FFmpeg conversion failed. The temporary AVI file was preserved.";
    return false;
  }
  return true;
#endif
}

void Ep128EmuGUI::init_()
{
  flDisplay = (Ep128Emu::FLTKDisplay_ *) 0;
  emulatorWindow = dynamic_cast<Fl_Window *>(&display);
  flDisplay = dynamic_cast<Ep128Emu::FLTKDisplay_ *>(&display);
#ifdef WIN32
  mainThreadID = uintptr_t(GetCurrentThreadId());
#else
  mainThreadID = uintptr_t(pthread_self());
#endif
  displayMode = 0;
  pixelPerfectEnabled = false;
  snapshotRuntimeMemoryActive = false;
  pixelPerfectPreviousWidth = 768;
  pixelPerfectPreviousHeight = 576;
  pixelPerfectMenuScreenNum = -1;
  pixelPerfectMenuScreenScale = 0.0f;
  pixelPerfectMenuDisplayMode = -1;
  mouseXOffset = 0;
  mouseYOffset = 0;
  exitFlag = false;
  errorFlag = false;
  restartMachineType = -1;
  restartLoadType = 0;
  restartLoadFileName.clear();
  oldWindowWidth = -1;
  oldWindowHeight = -1;
  oldDisplayMode = 0;
  mainWindowPositionX = -32768;
  mainWindowPositionY = -32768;
  oldDemoStatus = -1;
  oldSpeedPercentage = 100;
  oldPauseFlag = true;
  runtimeHUDEnabled = true;
  showInformationalDialogs = false;
  betaDOSAutoEnterOnReset = false;
  betaDOSAutoEnterPolls = 0;
  tapeRelayClickEnabled = true;
  printerCaptureEnabled = true;
  printerOutputFormat = 0;
  printerOutputDirectory = ".";
  printerOutputFile = (std::FILE *) 0;
  printerOverflowReported = 0U;
  tapeMotorSpinUpDelayMs = 1000;
#ifdef WIN32
  largeMenuEnabled = false;   // Windows W06 baseline used Standard sizing.
#else
  largeMenuEnabled = true;
#endif
  rewindHistoryLimitMiB = 16;
  rewindCaptureUserEnabled = true;
  measuredFPS = -1.0;
  statsFrameSequence = display.getFrameSequenceNumber();
  oldAudioRecordingFlag = false;
  oldVideoRecordingFlag = false;
  statsTimer.reset();
  audioRecordTimer.reset();
  videoRecordTimer.reset();
  oldTapeSampleRate = -1L;
  oldTapeSampleSize = -1;
  oldFloppyDriveLEDState = 0U;
  oldTapeRemoteState = 0xFFU;
  oldTapeMechanicalEngagementCount = 0U;
  oldForceTapeMotorOn = false;
  oldTapeReadOnlyFlag = false;
  oldTapePosition = -2L;
  functionKeyState = 0U;
  tapeButtonState = 0;
  oldTapeButtonState = -1;
  demoRecordFileName = "";
  demoRecordFile = (Ep128Emu::File *) 0;
  quickSnapshotFileName = "";
  updateDisplayEntered = false;
  debugWindowShowFlag = false;
  debugWindowOpenFlag = false;
  browseFileWindowShowFlag = false;
  guestFileIOPickerActive.store(false, std::memory_order_relaxed);
  browseFileStatus = 1;
  browseFileWindow = (Fl_Native_File_Chooser *) 0;
  windowToShow = (Fl_Window *) 0;
  diskConfigWindow = (Ep128EmuGUI_DiskConfigWindow *) 0;
  displaySettingsWindow = (Ep128EmuGUI_DisplayConfigWindow *) 0;
  keyboardConfigWindow = (Ep128EmuGUI_KbdConfigWindow *) 0;
  soundSettingsWindow = (Ep128EmuGUI_SoundConfigWindow *) 0;
  machineConfigWindow = (Ep128EmuGUI_MachineConfigWindow *) 0;
  debugWindow = (Ep128EmuGUI_DebugWindow *) 0;
  aboutWindow = (Ep128EmuGUI_AboutWindow *) 0;
  savedSpeedPercentage = 0U;
  mouseXScale = 1.0f;
  mouseYScale = 1.0f;
  mouseXMin = 0;
  mouseYMin = 0;
  prvMouseXPos = -32768;
  prvMouseYPos = -32768;
  prvMouseButtonState = 0xFF;
  std::string defaultDir_(".");
  snapshotDirectory = defaultDir_;
  demoDirectory = defaultDir_;
  soundFileDirectory = defaultDir_;
  videoCaptureOutputFileName = "";
  videoCaptureTempFileName = "";
  activeVideoCaptureFormat = -1;
  audioRecordFormat = 0;
  appShortcutPressedMask = 0U;
  for (int i = 0; i < EP_SC_COUNT; i++) {
    appShortcutStrings[i] = ep128emuAppShortcutDefault(i);
    appShortcutKeys[i] = 0;
    appShortcutModifiers[i] = 0;
    std::string canonical, error;
    (void) ep128emuParseAppShortcut(appShortcutStrings[i],
                                    appShortcutKeys[i],
                                    appShortcutModifiers[i],
                                    canonical, error);
  }
  configDirectory = defaultDir_;
  loadFileDirectory = defaultDir_;
  tapeImageDirectory = defaultDir_;
  diskImageDirectory = defaultDir_;
  romImageDirectory = defaultDir_;
  speccyDOSMountedSDDPath.clear();
  betaDiskTemporaryDiskFile.clear();
  speccyDOSWriteProtect=false;
  betaDiskWriteProtect=false;
  betaDOSInputStep = -1;
  betaDOSInputPressed = false;
  debuggerDirectory = defaultDir_;
  screenshotDirectory = defaultDir_;
  screenshotFileName = "";
  screenshotSequence = 0U;
  for (int i = 0; i < 10; i++) {
    recentDiskImages[i].clear();
    recentTapeImages[i].clear();
    recentSnapshots[i].clear();
    recentConfigurations[i].clear();
  }
  dropPasteExpected = false;
  pendingDroppedFiles.clear();
  archiveTemporaryDirectory.clear();
  archiveAutoRunText.clear();
  archiveAutoRunPosition = 0;
  archiveAutoRunPhase = -1;
  archiveAutoRunKeyCode = -1;
  archiveAutoRunShift = false;
  archiveAutoRunPreviousSpeedPercentage = 100U;
  archiveAutoRunSpeedOverrideActive = false;
  archiveAutoRunKeyFrameStart = 0;
  guiConfig.createKey("gui.snapshotDirectory", snapshotDirectory);
  guiConfig.createKey("gui.demoDirectory", demoDirectory);
  guiConfig.createKey("gui.soundFileDirectory", soundFileDirectory);
  guiConfig.createKey("gui.configDirectory", configDirectory);
  guiConfig.createKey("gui.loadFileDirectory", loadFileDirectory);
  guiConfig.createKey("gui.tapeImageDirectory", tapeImageDirectory);
  guiConfig.createKey("gui.diskImageDirectory", diskImageDirectory);
  guiConfig.createKey("gui.romImageDirectory", romImageDirectory);
  guiConfig.createKey("gui.debuggerDirectory", debuggerDirectory);
  guiConfig.createKey("gui.screenshotDirectory", screenshotDirectory);
  guiConfig.createKey("gui.mainWindowPositionX", mainWindowPositionX);
  guiConfig.createKey("gui.mainWindowPositionY", mainWindowPositionY);
  guiConfig.createKey("gui.runtimeHUDEnabled", runtimeHUDEnabled);
  guiConfig.createKey("gui.showInformationalDialogs", showInformationalDialogs);
  guiConfig.createKey("gui.betaDOSAutoEnterOnReset", betaDOSAutoEnterOnReset);
  guiConfig.createKey("gui.tapeRelayClickEnabled", tapeRelayClickEnabled);
  guiConfig.createKey("gui.printerCaptureEnabled", printerCaptureEnabled);
  guiConfig.createKey("gui.printerOutputFormat", printerOutputFormat);
  guiConfig.createKey("gui.printerOutputDirectory", printerOutputDirectory);
  guiConfig.createKey("gui.tapeMotorSpinUpDelayMs", tapeMotorSpinUpDelayMs);
#ifdef WIN32
  guiConfig.createKey("gui.windowsLargeMenuEnabled", largeMenuEnabled);
#else
  guiConfig.createKey("gui.largeMenuEnabled", largeMenuEnabled);
#endif
  guiConfig.createKey("gui.rewindHistoryLimitMiB", rewindHistoryLimitMiB);
  guiConfig.createKey("gui.rewindCaptureUserEnabled",
                      rewindCaptureUserEnabled);
  guiConfig.createKey("gui.pixelPerfectEnabled", pixelPerfectEnabled);
  guiConfig.createKey("gui.pixelPerfectPreviousWidth", pixelPerfectPreviousWidth);
  guiConfig.createKey("gui.pixelPerfectPreviousHeight", pixelPerfectPreviousHeight);
  guiConfig.createKey("gui.audioRecordFormat", audioRecordFormat);
  for (int i = 0; i < EP_SC_COUNT; i++)
    guiConfig.createKey(ep128emuAppShortcutConfigKey(i), appShortcutStrings[i]);
  for (int i = 0; i < 10; i++) {
    char keyName[64];
    std::sprintf(keyName, "gui.recentDiskImage%d", i);
    guiConfig.createKey(keyName, recentDiskImages[i]);
    std::sprintf(keyName, "gui.recentTapeImage%d", i);
    guiConfig.createKey(keyName, recentTapeImages[i]);
    std::sprintf(keyName, "gui.recentSnapshot%d", i);
    guiConfig.createKey(keyName, recentSnapshots[i]);
    std::sprintf(keyName, "gui.recentConfiguration%d", i);
    guiConfig.createKey(keyName, recentConfigurations[i]);
  }
  browseFileWindow = new Fl_Native_File_Chooser();
  Fl::add_check(&fltkCheckCallback, (void *) this);
}

void Ep128EmuGUI::updateDisplay_windowTitle()
{
  // Machine, speed, metric and expansion use independent fixed-width fields.
  char speed[32];
  if (oldPauseFlag)
    std::snprintf(speed, sizeof(speed), "PAUSED");
  else
    std::snprintf(speed, sizeof(speed), "%d%%", int(oldSpeedPercentage));
  const char *metric = (const char *) 0;
#ifdef WIN32
  // Reserve the metric slot even with HUD off so the expansion stays put.
  metric = runtimeHUDEnabled ? runtimeStatusText.c_str() : "";
#endif
  const std::string title = Ep128EmuTitle::format(
      "ep128emu 2.1", ep128emuMachineShortName(vm), speed,
      metric, ep128emuSpectrumExpansionName(vm));
  std::snprintf(&(windowTitleBuf[0]), sizeof(windowTitleBuf),
                "%s", title.c_str());
  // copy_label() forces the native caption to update even if the source
  // buffer address is unchanged between consecutive FPS/speed refreshes.
  mainWindow->copy_label(&(windowTitleBuf[0]));
}

void Ep128EmuGUI::updateDisplay_windowMode()
{
  if (((displayMode ^ oldDisplayMode) & 2) != 0) {
    if ((displayMode & 2) != 0) {
      if ((oldDisplayMode & 2) == 0) {
        mainWindowPositionX = mainWindow->x();
        mainWindowPositionY = mainWindow->y();
      }
      mainWindow->fullscreen();
    }
    else {
      mainWindow->fullscreen_off(mainWindowPositionX, mainWindowPositionY,
                                 config.display.width, config.display.height);
      restoreMainWindowPosition();
    }
  }
  resizeWindow(config.display.width, config.display.height);
  Fl::redraw();
  Fl::flush();
  int     newWindowWidth = mainWindow->w();
  int     newWindowHeight = mainWindow->h();
  if ((displayMode & 1) == 0) {
    statusDisplayGroup->resize(newWindowWidth - 360,
                               (newWindowWidth >= 745 ?
                                0 : (newWindowHeight - 30)),
                               360, 30);
    statusDisplayGroup->show();
    mainMenuBar->resize(0, 2, 380, getMenuBarHeight());
    mainMenuBar->show();
    if (typeid(vm) != typeid(ZX128::ZX128VM)) {
      diskStatusDisplayGroup->resize(statusDisplayGroup->x() + 2,
                                     statusDisplayGroup->y(), 30, 30);
      diskStatusDisplayGroup->show();
    }
  }
  else {
    statusDisplayGroup->hide();
    mainMenuBar->hide();
    diskStatusDisplayGroup->hide();
  }
  oldWindowWidth = -1;
  oldWindowHeight = -1;
  oldDisplayMode = displayMode;
  if (pixelPerfectEnabled && (displayMode & 2) == 0)
    resizePixelPerfectWindow();
  mainWindow->cursor(!(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
  mainWindow->redraw();
  mainMenuBar->redraw();
  diskStatusDisplayGroup->redraw();
  updateRecordingStatusDisplay();
  statusDisplayGroup->redraw();
}

void Ep128EmuGUI::updateDisplay_windowSize()
{
  int     newWindowWidth = mainWindow->w();
  int     newWindowHeight = mainWindow->h();
  if (newWindowWidth != oldWindowWidth || newWindowHeight != oldWindowHeight) {
    if ((displayMode & 1) == 0) {
      int   h = newWindowHeight - (newWindowWidth >= 745 ? 30 : 60);
      emulatorWindow->resize(0, 30, newWindowWidth, h);
      if ((displayMode & 2) == 0) {
        config.display.width = newWindowWidth;
        config.display.height = h;
      }
      statusDisplayGroup->resize(newWindowWidth - 360,
                                 (newWindowWidth >= 745 ?
                                  0 : (newWindowHeight - 30)),
                                 360, 30);
      mainMenuBar->resize(0, 2, 380, getMenuBarHeight());
      diskStatusDisplayGroup->resize(statusDisplayGroup->x() + 2,
                                     statusDisplayGroup->y(), 30, 30);
    }
    else {
      emulatorWindow->resize(0, 0, newWindowWidth, newWindowHeight);
      if ((displayMode & 2) == 0) {
        config.display.width = newWindowWidth;
        config.display.height = newWindowHeight;
      }
    }
    oldWindowWidth = newWindowWidth;
    oldWindowHeight = newWindowHeight;
    mainWindow->redraw();
    mainMenuBar->redraw();
    diskStatusDisplayGroup->redraw();
    updateRecordingStatusDisplay();
    statusDisplayGroup->redraw();
  }
  {
    // update mouse coordinate translation parameters
    float w = float(emulatorWindow->w());
    float h = float(emulatorWindow->h());
    mouseXOffset = 0;
    mouseYOffset = 0;
    if (pixelPerfectEnabled) {
      int rx, ry, rw, rh;
      getPixelPerfectDisplayRect(rx, ry, rw, rh);
      mouseXOffset = rx;
      mouseYOffset = ry;
      w = float(rw);
      h = float(rh);
      mouseXScale = (384.0f / w) * float(config.mouse.sensitivityX);
      mouseYScale = (288.0f / h) * float(config.mouse.sensitivityY);
    }
    else if ((w * config.display.pixelAspectRatio / h) < 1.333333f) {
      // aspect ratio < 4:3, using full window width
      mouseXScale = 384.0f / w;
      mouseYScale = mouseXScale / config.display.pixelAspectRatio;
      mouseXScale *= float(config.mouse.sensitivityX);
      mouseYScale *= float(config.mouse.sensitivityY);
    }
    else {
      // aspect ratio >= 4:3, using full window height
      mouseYScale = 288.0f / h;
      mouseXScale = mouseYScale * config.display.pixelAspectRatio;
      mouseYScale *= float(config.mouse.sensitivityY);
      mouseXScale *= float(config.mouse.sensitivityX);
    }
    mouseXMin = int(w * (displayMode == 3 ? 24.0f : 16.0f) / 384.0f + 0.5f);
    mouseYMin = int(h * (displayMode == 3 ? 24.0f : 16.0f) / 288.0f + 0.5f);
    prvMouseXPos = -32768;
    prvMouseYPos = -32768;
  }
}

void Ep128EmuGUI::getPixelPerfectDisplayRect(int& x, int& y, int& w, int& h)
{
  float ppu = 1.0f;
  Fl_Gl_Window *glw = emulatorWindow->as_gl_window();
  if (glw && pixelPerfectEnabled) {
    ppu = glw->pixels_per_unit();
    if (ppu < 1.0f)
      ppu = 1.0f;
  }
  int physicalW = int(float(emulatorWindow->w()) * ppu + 0.5f);
  int physicalH = int(float(emulatorWindow->h()) * ppu + 0.5f);
  int scale = physicalW / 768;
  int sy = physicalH / 576;
  if (sy < scale)
    scale = sy;
  if (scale < 1)
    scale = 1;
  int targetPhysicalW = 768 * scale;
  int targetPhysicalH = 576 * scale;
  w = int(float(targetPhysicalW) / ppu + 0.5f);
  h = int(float(targetPhysicalH) / ppu + 0.5f);
  x = (emulatorWindow->w() - w) >> 1;
  y = (emulatorWindow->h() - h) >> 1;
}

void Ep128EmuGUI::getPixelPerfectWindowSize(float ppu, int& w, int& h)
{
  int screenNum = Fl::screen_num(mainWindow->x(), mainWindow->y(),
                                 mainWindow->w(), mainWindow->h());
  if (screenNum < 0)
    screenNum = 0;
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh, screenNum);

  if (ppu < 1.0f)
    ppu = 1.0f;

  int nativeFrameExtraHeight = 0;
#ifdef __APPLE__
  nativeFrameExtraHeight = ep128emuGetMacOSWindowFrameExtraHeight(mainWindow);
#endif

  w = 768;
  h = 576;
  for (int scale = 16; scale >= 1; scale--) {
    int tmpW = int(float(768 * scale) / ppu + 0.5f);
    int tmpH = int(float(576 * scale) / ppu + 0.5f);
    int chrome = ((displayMode & 1) == 0 ?
                  (tmpW >= 745 ? 30 : 60) : 0);
    if (tmpW <= sw &&
        (tmpH + chrome + nativeFrameExtraHeight) <= sh) {
      w = tmpW;
      h = tmpH;
      break;
    }
  }
}

void Ep128EmuGUI::updatePixelPerfectMenuLabel(bool force)
{
  if (!mainMenuBar)
    return;

  int screenNum = Fl::screen_num(mainWindow->x(), mainWindow->y(),
                                 mainWindow->w(), mainWindow->h());
  if (screenNum < 0)
    screenNum = 0;

  float screenScale = Fl::screen_scale(screenNum);
#ifdef __APPLE__
  screenScale = ep128emuGetMacOSBackingScale(mainWindow);
#endif
  if (screenScale < 1.0f)
    screenScale = 1.0f;

  if (!force &&
      screenNum == pixelPerfectMenuScreenNum &&
      screenScale == pixelPerfectMenuScreenScale &&
      displayMode == pixelPerfectMenuDisplayMode)
    return;

  int w = 0, h = 0;
  getPixelPerfectWindowSize(screenScale, w, h);

  const int menuIndex = ep128emuFindMenuIndexByCallback(
      mainMenuBar, &menuCallback_Options_DpyPixelPerfect);
  if (menuIndex >= 0) {
    char label[96];
    std::snprintf(label, sizeof(label),
                  "Set size to %dx%d (Pixel Perfect)", w, h);
    mainMenuBar->replace(menuIndex, label);
    mainMenuBar->redraw();
  }

  pixelPerfectMenuScreenNum = screenNum;
  pixelPerfectMenuScreenScale = screenScale;
  pixelPerfectMenuDisplayMode = displayMode;
}

void Ep128EmuGUI::resizePixelPerfectWindow()
{
  if (!pixelPerfectEnabled || (displayMode & 2) != 0)
    return;

  float ppu = 1.0f;
  Fl_Gl_Window *glw = emulatorWindow->as_gl_window();
  if (glw) {
    ppu = glw->pixels_per_unit();
    if (ppu < 1.0f)
      ppu = 1.0f;
  }

  int bestW = 0, bestH = 0;
  getPixelPerfectWindowSize(ppu, bestW, bestH);
  mainWindowPositionX = mainWindow->x();
  mainWindowPositionY = mainWindow->y();
  resizeWindow(bestW, bestH);
  restoreMainWindowPosition();
  updatePixelPerfectMenuLabel(true);
}

void Ep128EmuGUI::applyPixelPerfectDisplayState(bool remapHighRes_)
{
#ifdef __APPLE__
  Fl_Gl_Window *glw = emulatorWindow->as_gl_window();
  if (glw) {
    bool needHighRes = pixelPerfectEnabled;
    bool isHighRes = (glw->shown() && glw->pixels_per_unit() > 1.5f);
    Fl::use_high_res_GL(needHighRes ? 1 : 0);
    if (remapHighRes_ && glw->shown() && needHighRes != isHighRes) {
      glw->hide();
      Fl::check();
      glw->show();
      Fl::check();
      glw->take_focus();
    }
  }
#endif

  Ep128Emu::VideoDisplay::DisplayParameters dp(display.getDisplayParameters());
  if (dp.pixelPerfect != pixelPerfectEnabled) {
    dp.pixelPerfect = pixelPerfectEnabled;
    display.setDisplayParameters(dp);
  }

  if (pixelPerfectEnabled)
    resizePixelPerfectWindow();
  oldWindowWidth = -1;
  oldWindowHeight = -1;
  emulatorWindow->redraw();
  syncRecordingStatusState();
  updateRecordingStatusDisplay();
  updateMenu();
}

void Ep128EmuGUI::updateRuntimeStatusDisplay(
    const Ep128Emu::VMThread::VMThreadStatus& status)
{
#ifdef __APPLE__
  char statusLabel[64];
  if (status.isPaused) {
    const double usedMiB =
        double(status.rewindBufferUsageBytes) / (1024.0 * 1024.0);
    const double limitMiB =
        double(status.rewindBufferLimitBytes) / (1024.0 * 1024.0);
    std::snprintf(statusLabel, sizeof(statusLabel),
                  "FRAME %zu/%zu  %.1f/%.0f MiB",
                  status.rewindTimelinePosition,
                  status.rewindTimelineLength,
                  usedMiB, limitMiB);
  }
  else if (measuredFPS < 0.0) {
    std::snprintf(statusLabel, sizeof(statusLabel), "FPS  --.-");
  }
  else {
    std::snprintf(statusLabel, sizeof(statusLabel), "FPS %5.1f", measuredFPS);
  }
  ep128emuSetMacOSTitlebarStatus(
      mainWindow, statusLabel,
      runtimeHUDEnabled && ((displayMode & 1) == 0));
#elif defined(WIN32)
  // A native Windows title bar has no Cocoa titlebar accessory slot.
  // Show the runtime HUD in the native caption without moving the
  // emulator viewport or overlapping the tape and drive indicators.
  char statusLabel[80];
  if (status.isPaused) {
    const double usedMiB =
        double(status.rewindBufferUsageBytes) / (1024.0 * 1024.0);
    const double limitMiB =
        double(status.rewindBufferLimitBytes) / (1024.0 * 1024.0);
    std::snprintf(statusLabel, sizeof(statusLabel),
                  "[FRAME %zu / %zu]     [HISTORY %.1f / %.0f MiB]",
                  status.rewindTimelinePosition,
                  status.rewindTimelineLength, usedMiB, limitMiB);
  }
  else if (measuredFPS < 0.0) {
    std::snprintf(statusLabel, sizeof(statusLabel), "[FPS --.-]");
  }
  else {
    std::snprintf(statusLabel, sizeof(statusLabel), "[FPS %5.1f]", measuredFPS);
  }
  if (runtimeStatusText != statusLabel) {
    runtimeStatusText = statusLabel;
    updateDisplay_windowTitle();
  }
#else
  (void) status;
#endif
}

void Ep128EmuGUI::updateRecordingStatusDisplay()
{
  // Reserve left-hand space for disk, REM1/REM2 and demo P/R indicators.
  const int x0 = statusDisplayGroup->x() + 151;
  const int y0 = statusDisplayGroup->y() + 5;

  videoRecordStatusDisplay->hide();
  audioRecordStatusDisplay->hide();

  if (!oldVideoRecordingFlag && !oldAudioRecordingFlag) {
    tapeInfoDisplay->show();
    tapePositionDisplay->show();
    mainWindow->redraw();
    return;
  }

  tapeInfoDisplay->hide();

  if (oldVideoRecordingFlag && oldAudioRecordingFlag) {
    // Two compact recording timers retain the disk/REM/demo slots.
    // Hide tape position only while both recorders are active.
    tapePositionDisplay->hide();

    videoRecordStatusDisplay->resize(x0, y0, 102, 20);
    videoRecordLabelDisplay->resize(x0 + 2, y0, 43, 20);
    videoRecordTimeDisplay->resize(x0 + 44, y0, 56, 20);

    audioRecordStatusDisplay->resize(x0 + 105, y0, 102, 20);
    audioRecordLabelDisplay->resize(x0 + 107, y0, 43, 20);
    audioRecordTimeDisplay->resize(x0 + 149, y0, 56, 20);

    videoRecordStatusDisplay->show();
    audioRecordStatusDisplay->show();
  }
  else {
    tapePositionDisplay->show();
    Fl_Group *g = (oldVideoRecordingFlag ?
                   videoRecordStatusDisplay : audioRecordStatusDisplay);
    Fl_Box *l = (oldVideoRecordingFlag ?
                 videoRecordLabelDisplay : audioRecordLabelDisplay);
    Fl_Box *t = (oldVideoRecordingFlag ?
                 videoRecordTimeDisplay : audioRecordTimeDisplay);
    g->resize(x0, y0, 105, 20);
    l->resize(x0 + 2, y0, 45, 20);
    t->resize(x0 + 48, y0, 55, 20);
    g->show();
  }
  mainWindow->redraw();
}

void Ep128EmuGUI::syncRecordingStatusState()
{
  bool newAudioRecordingFlag = (config.sound.file.length() > 0);
  bool newVideoRecordingFlag = (activeVideoCaptureFormat >= 0);
  bool changed = false;

  if (newAudioRecordingFlag != oldAudioRecordingFlag) {
    oldAudioRecordingFlag = newAudioRecordingFlag;
    if (oldAudioRecordingFlag) {
      audioRecordTimer.reset();
      audioRecordTimeDisplay->label("00:00");
    }
    changed = true;
  }

  if (newVideoRecordingFlag != oldVideoRecordingFlag) {
    oldVideoRecordingFlag = newVideoRecordingFlag;
    if (oldVideoRecordingFlag) {
      videoRecordTimer.reset();
      videoRecordTimeDisplay->label("00:00");
    }
    changed = true;
  }

  if (changed)
    updateRecordingStatusDisplay();
}

void Ep128EmuGUI::updateDisplay(double t)
{
#ifdef WIN32
  uintptr_t currentThreadID = uintptr_t(GetCurrentThreadId());
#else
  uintptr_t currentThreadID = uintptr_t(pthread_self());
#endif
  if (currentThreadID != mainThreadID) {
    Ep128Emu::Timer::wait(t * 0.5);
    return;
  }
  if (browseFileWindowShowFlag | debugWindowShowFlag) {
    if (browseFileWindowShowFlag) {
      browseFileStatus = browseFileWindow->show();
      browseFileWindowShowFlag = false;
    }
    if (debugWindowShowFlag) {
      debugWindow->show();
      debugWindowShowFlag = false;
    }
  }
  if (windowToShow) {
    windowToShow->show();
    windowToShow = (Fl_Window *) 0;
  }
  if (flDisplay->haveFramesPending())
    t = t * 0.5;
  if (updateDisplayEntered) {
    // if re-entering this function:
    Fl::wait(t);
    return;
  }
  updateDisplayEntered = true;
  Ep128Emu::VMThread::VMThreadStatus  vmThreadStatus(vmThread);
  if (vmThreadStatus.threadStatus != 0) {
    exitFlag = true;
    if (vmThreadStatus.threadStatus < 0)
      errorFlag = true;
  }
  if (displayMode != oldDisplayMode) {
    updateDisplay_windowMode();
  }
  else if (mainWindow->w() != oldWindowWidth ||
           mainWindow->h() != oldWindowHeight) {
    updateDisplay_windowSize();
  }
  updatePixelPerfectMenuLabel();
  syncRecordingStatusState();
  if (vmThreadStatus.isPaused != oldPauseFlag) {
    oldPauseFlag = vmThreadStatus.isPaused;
    updateDisplay_windowTitle();
  }
  updateRuntimeStatusDisplay(vmThreadStatus);
  {
    const int nextFrameIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_NextFrame);
    if (nextFrameIndex >= 0) {
      Fl_Menu_Item *nextFrameItem = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + nextFrameIndex);
      if (oldPauseFlag &&
          vmThreadStatus.rewindTimelinePosition <
              vmThreadStatus.rewindTimelineLength)
        nextFrameItem->activate();
      else
        nextFrameItem->deactivate();
    }
  }
  {
    const int previousFrameIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_PreviousFrame);
    if (previousFrameIndex >= 0) {
      Fl_Menu_Item *previousFrameItem = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + previousFrameIndex);
      if (oldPauseFlag && vmThreadStatus.rewindFramesAvailable > 0 &&
          !oldAudioRecordingFlag && !oldVideoRecordingFlag)
        previousFrameItem->activate();
      else
        previousFrameItem->deactivate();
    }
  }
  {
    const int previous100Index = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Previous100);
    if (previous100Index >= 0) {
      Fl_Menu_Item *previous100Item = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + previous100Index);
      if (oldPauseFlag && vmThreadStatus.rewindFramesAvailable > 0 &&
          !oldAudioRecordingFlag && !oldVideoRecordingFlag)
        previous100Item->activate();
      else
        previous100Item->deactivate();
    }
  }
  {
    const int next100Index = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Next100);
    if (next100Index >= 0) {
      Fl_Menu_Item *next100Item = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + next100Index);
      if (oldPauseFlag &&
          vmThreadStatus.rewindTimelinePosition <
              vmThreadStatus.rewindTimelineLength)
        next100Item->activate();
      else
        next100Item->deactivate();
    }
  }
  {
    const int oldestFrameIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_OldestFrame);
    if (oldestFrameIndex >= 0) {
      Fl_Menu_Item *oldestFrameItem = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + oldestFrameIndex);
      if (oldPauseFlag && vmThreadStatus.rewindFramesAvailable > 0 &&
          !oldAudioRecordingFlag && !oldVideoRecordingFlag)
        oldestFrameItem->activate();
      else
        oldestFrameItem->deactivate();
    }
  }
  {
    const int latestFrameIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_LatestFrame);
    if (latestFrameIndex >= 0) {
      Fl_Menu_Item *latestFrameItem = const_cast<Fl_Menu_Item *>(
          mainMenuBar->menu() + latestFrameIndex);
      if (oldPauseFlag &&
          vmThreadStatus.rewindTimelinePosition <
              vmThreadStatus.rewindTimelineLength)
        latestFrameItem->activate();
      else
        latestFrameItem->deactivate();
    }
  }
  int   newDemoStatus = (vmThreadStatus.isRecordingDemo ?
                         2 : (vmThreadStatus.isPlayingDemo ? 1 : 0));
  if (newDemoStatus != oldDemoStatus) {
    Fl_Menu_Item& m = getMenuItem(0);   // "File/Stop demo (" EP128EMU_ALT_KEY_TEXT "K)"
    if (newDemoStatus == 0) {
      if (oldDemoStatus == 2)
        closeDemoFile(false);
      demoStatusDisplay1->hide();
      demoStatusDisplay2->hide();
      m.deactivate();
    }
    else {
      // Compact demo P/R badge follows the disk and REM indicators.
      // Keep the verbose "Demo:" label hidden to avoid overlap.
      demoStatusDisplay1->hide();
      demoStatusDisplay2->show();
      if (newDemoStatus == 1) {
        demoStatusDisplay2->labelcolor(Fl_Color(6));
        demoStatusDisplay2->label("P");
      }
      else {
        demoStatusDisplay2->labelcolor(Fl_Color(1));
        demoStatusDisplay2->label("R");
      }
      m.activate();
    }
    oldDemoStatus = newDemoStatus;
    mainWindow->redraw();
  }
  if (vmThreadStatus.tapeSampleRate != oldTapeSampleRate ||
      vmThreadStatus.tapeSampleSize != oldTapeSampleSize ||
      vmThreadStatus.tapeReadOnly != oldTapeReadOnlyFlag) {
    oldTapeSampleRate = vmThreadStatus.tapeSampleRate;
    oldTapeSampleSize = vmThreadStatus.tapeSampleSize;
    oldTapeReadOnlyFlag = vmThreadStatus.tapeReadOnly;
    if (vmThreadStatus.tapeSampleRate < 1L || vmThreadStatus.tapeSampleSize < 1) {
      tapeInfoDisplay->label("Tape: none");
      tapeInfoDisplay->tooltip("No tape image selected");
    }
    else {
      char fullInfo[128];
      char shortInfo[64];
      std::snprintf(fullInfo, sizeof(fullInfo), "Tape: %ldHz %dbit %s",
                    vmThreadStatus.tapeSampleRate, vmThreadStatus.tapeSampleSize,
                    (vmThreadStatus.tapeReadOnly ? "RO" : "RW"));
      std::snprintf(shortInfo, sizeof(shortInfo), "%gk/%db %s",
                    double(vmThreadStatus.tapeSampleRate) / 1000.0,
                    vmThreadStatus.tapeSampleSize,
                    (vmThreadStatus.tapeReadOnly ? "RO" : "RW"));
      tapeInfoDisplay->copy_label(shortInfo);
      tapeInfoDisplay->copy_tooltip(fullInfo);
    }
    mainWindow->redraw();
  }
  if (tapeButtonState != oldTapeButtonState ||
      oldForceTapeMotorOn != config.tape.forceMotorOn) {
    oldTapeButtonState = tapeButtonState;
    oldForceTapeMotorOn = config.tape.forceMotorOn;
    tapeStatusDisplay->tooltip(oldForceTapeMotorOn ?
      "Tape motor FORCE ON (REM overridden)" : "Tape Play/Record state");
    switch (tapeButtonState) {
    case 1:
      tapeStatusDisplay->labelcolor(Fl_Color(6));
      tapeStatusDisplay->label(oldForceTapeMotorOn ? "P*" : "P");
      break;
    case 2:
      tapeStatusDisplay->labelcolor(Fl_Color(1));
      tapeStatusDisplay->label(oldForceTapeMotorOn ? "R*" : "R");
      break;
    default:
      tapeStatusDisplay->labelcolor(Fl_Color(6));
      tapeStatusDisplay->label(oldForceTapeMotorOn ? "*" : "");
      break;
    }
    tapePositionDisplay->redraw();
    tapeStatusDisplay->redraw();
  }
  long  newTapePosition = long(vmThreadStatus.tapePosition * 10.0 + 0.25);
  newTapePosition = (newTapePosition >= 0L ? newTapePosition : -1L);
  if (newTapePosition != oldTapePosition) {
    oldTapePosition = newTapePosition;
    if (newTapePosition >= 0L) {
      char  tmpBuf[256];
      int   h, m, s, ds;
      ds = int(newTapePosition % 10L);
      s = int((newTapePosition / 10L) % 60L);
      m = int((newTapePosition / 600L) % 60L);
      h = int((newTapePosition / 36000L) % 100L);
      std::sprintf(&(tmpBuf[0]), "%2d:%02d:%02d.%d ", h, m, s, ds);
      tapePositionDisplay->copy_label(&(tmpBuf[0]));
    }
    else
      tapePositionDisplay->label("-:--:--.- ");
    tapePositionDisplay->redraw();
    tapeStatusDisplay->redraw();
  }
  // Use mechanical engagements, not brief electrical REM pulses.
  // The counter survives short cycles entirely between GUI polls.
  const uint32_t newRelayCount = vmThreadStatus.tapeMechanicalEngagementCount;
  if (newRelayCount > oldTapeMechanicalEngagementCount &&
      tapeRelayClickEnabled && config.vm.speedPercentage == 100U &&
      !archiveAutoRunSpeedOverrideActive &&
      !vmThreadStatus.isPaused && !vmThreadStatus.isPlayingDemo) {
#ifdef __APPLE__
    ep128emuPlayMacOSRelayClick();
#elif defined(WIN32)
    (void) ep128emuPlayWindowsRelayClick();
#endif
  }
  oldTapeMechanicalEngagementCount = newRelayCount;
  // Round LEDs show the independent DAVE REM outputs (not FORCE motor).
  if (oldTapeRemoteState != vmThreadStatus.tapeRemoteState) {
    oldTapeRemoteState = vmThreadStatus.tapeRemoteState;
    rem1StatusDisplay->color((oldTapeRemoteState & 1U) ?
                             fl_rgb_color(38, 159, 95) :
                             fl_rgb_color(225, 228, 230));
    rem2StatusDisplay->color((oldTapeRemoteState & 2U) ?
                             fl_rgb_color(38, 159, 95) :
                             fl_rgb_color(225, 228, 230));
    rem1StatusDisplay->redraw();
    rem2StatusDisplay->redraw();
  }
  uint32_t  newFloppyDriveLEDState = vmThreadStatus.floppyDriveLEDState;
  if (newFloppyDriveLEDState != oldFloppyDriveLEDState) {
    oldFloppyDriveLEDState = newFloppyDriveLEDState;
    // FLTK color (5*8*5 RGB) = 56 + 8*R(0..4) + G(0..7) + 40*B(0..4)
    static const unsigned char ledColors_[64] = {
       56,  92,  63,  87,  128,  92,  63,  87,
       56,  92,  63,  87,  128, 128, 128, 128,
      236,  92,  63,  87,  128,  92,  63,  87,
      236,  92,  63,  87,  128, 128, 128, 128,
      236, 236, 236, 236,  236, 236, 236, 236,
      236, 236, 236, 236,  236, 236, 236, 236,
      239, 239, 239, 239,  239, 239, 239, 239,
      239, 239, 239, 239,  239, 239, 239, 239
    };
    driveALEDDisplay->color(
        Fl_Color(ledColors_[newFloppyDriveLEDState & 0x0FU]));
    driveALEDDisplay->redraw();
    driveBLEDDisplay->color(
        Fl_Color(ledColors_[(newFloppyDriveLEDState >> 8) & 0x0FU]));
    driveBLEDDisplay->redraw();
    driveCLEDDisplay->color(
        Fl_Color(ledColors_[(newFloppyDriveLEDState >> 16) & 0x3FU]));
    driveCLEDDisplay->redraw();
    driveDLEDDisplay->color(
        Fl_Color(ledColors_[(newFloppyDriveLEDState >> 24) & 0x3FU]));
    driveDLEDDisplay->redraw();
  }
  double statsElapsed = statsTimer.getRealTime();
  if (statsElapsed >= 0.5) {
    uint64_t currentFrameSequence = display.getFrameSequenceNumber();
    uint64_t frameDelta = currentFrameSequence - statsFrameSequence;
    statsFrameSequence = currentFrameSequence;
    measuredFPS = double(frameDelta) / statsElapsed;
    statsTimer.reset();

    char fpsLabel[32];
    if (measuredFPS < 0.0)
      std::snprintf(fpsLabel, sizeof(fpsLabel), "FPS  --.-");
    else
      std::snprintf(fpsLabel, sizeof(fpsLabel), "FPS %5.1f", measuredFPS);
#ifdef __APPLE__
    if (!vmThreadStatus.isPaused)
      ep128emuSetMacOSTitlebarStatus(
          mainWindow, fpsLabel,
          runtimeHUDEnabled && ((displayMode & 1) == 0));
#endif

    if (oldVideoRecordingFlag) {
      char elapsed[32];
      ep128emuFormatElapsed(elapsed, sizeof(elapsed),
                            videoRecordTimer.getRealTime());
      videoRecordTimeDisplay->copy_label(elapsed);
      videoRecordTimeDisplay->redraw();
      videoRecordStatusDisplay->redraw();
    }
    if (oldAudioRecordingFlag) {
      char elapsed[32];
      ep128emuFormatElapsed(elapsed, sizeof(elapsed),
                            audioRecordTimer.getRealTime());
      audioRecordTimeDisplay->copy_label(elapsed);
      audioRecordTimeDisplay->redraw();
      audioRecordStatusDisplay->redraw();
    }

    int32_t newSpeedPercentage = int32_t(vmThreadStatus.speedPercentage + 0.5f);
    if (newSpeedPercentage != oldSpeedPercentage) {
      oldSpeedPercentage = newSpeedPercentage;
      updateDisplay_windowTitle();
    }
  }
  // A machine switch uses VMThread::quit(false) and exits this GUI before
  // spawning the requested next machine. Once threadStatus reports exit,
  // never try to re-lock that VM from this (or a nested modal) UI update.
  // The old unconditional printer poll recursively reopened the same error
  // dialog while the previous Enterprise VM was shutting down.
  if (!exitFlag)
    pollPrinterOutput();
  Fl::wait(t);
  updateDisplayEntered = false;
}

void Ep128EmuGUI::errorMessage(const char *msg)
{
  Fl::lock();
  while (errorMessageWindow->shown()) {
    Fl::unlock();
    updateDisplay();
    Fl::lock();
  }
  if (msg)
    errorMessageText->copy_label(msg);
  else
    errorMessageText->label("");
  errorMessageWindow->set_modal();
  windowToShow = errorMessageWindow;
  while (true) {
    Fl::unlock();
    try {
      updateDisplay();
    }
    catch (...) {
    }
    Fl::lock();
    if (windowToShow != errorMessageWindow && !errorMessageWindow->shown()) {
      errorMessageText->label("");
      Fl::unlock();
      break;
    }
  }
}

Fl_Menu_Item& Ep128EmuGUI::getMenuItem(int n)
{
  const char  *s = (char *) 0;
  switch (n) {
  case 0:
    s = "File/Stop demo (" EP128EMU_ALT_KEY_TEXT "K)";
    break;
  case 1:
    s = "File/Record audio/Stop";
    break;
  case 2:
    s = "File/Record video/Stop";
    break;
  case 3:
    s = "Machine/Speed/No limit";
    break;
  case 4:
    s = "Options/Process priority/Idle";
    break;
  case 5:
    s = "Options/Process priority/Below normal";
    break;
  case 6:
    s = "Options/Process priority/Normal";
    break;
  case 7:
    s = "Options/Process priority/Above normal";
    break;
  case 8:
    s = "Options/Process priority/High";
    break;
  default:
    throw Ep128Emu::Exception("internal error: invalid menu item number");
  }
  return *(const_cast<Fl_Menu_Item *>(mainMenuBar->find_item(s)));
}

int Ep128EmuGUI::getMenuItemIndex(int n)
{
  const Fl_Menu_Item  *tmp = &(getMenuItem(n));
  return int(tmp - mainMenuBar->menu());
}

int Ep128EmuGUI::getMenuBarHeight() const
{
#ifdef __APPLE__
  return (largeMenuEnabled ? 30 : 26);
#else
  return 30;
#endif
}

void Ep128EmuGUI::applyMenuSize()
{
#if defined(__APPLE__) || defined(WIN32)
  int menuTextSize = FL_NORMAL_SIZE;
  int menuLineSpacing = 4;
#ifdef __APPLE__
  if (largeMenuEnabled) {
    // Retina/HiDPI mode retained from the accepted enlarged-menu baseline.
    float menuDpiH = 96.0f;
    float menuDpiV = 96.0f;
    Fl::screen_dpi(menuDpiH, menuDpiV, 0);
    float menuDpi = (menuDpiH > menuDpiV ? menuDpiH : menuDpiV);
    menuTextSize = int((16.0f * menuDpi / 96.0f) + 0.5f);
    if (menuTextSize < 18)
      menuTextSize = 18;
    else if (menuTextSize > 22)
      menuTextSize = 22;
    menuLineSpacing = int((7.0f * menuDpi / 96.0f) + 0.5f);
    if (menuLineSpacing < 8)
      menuLineSpacing = 8;
    else if (menuLineSpacing > 12)
      menuLineSpacing = 12;
  }
#else
  // UCRT64 FLTK uses host DPI already: preserve W06 Standard sizing and
  // offer a modest, selectable enlarged Windows menu without Cocoa scaling.
  if (largeMenuEnabled) {
    menuTextSize = 18;
    menuLineSpacing = 6;
  }
#endif

  Fl::menu_linespacing(menuLineSpacing);
  mainMenuBar->textsize(menuTextSize);
  mainMenuBar->resize(mainMenuBar->x(), mainMenuBar->y(),
                      mainMenuBar->w(), getMenuBarHeight());

  const char *selectedPath =
      (largeMenuEnabled ? "Options/Display/Menu size/Large"
                        : "Options/Display/Menu size/Standard");
  const Fl_Menu_Item *selected = mainMenuBar->find_item(selectedPath);
  if (selected)
    const_cast<Fl_Menu_Item *>(selected)->setonly();

  mainMenuBar->redraw();
  if (mainWindow)
    mainWindow->redraw();
#endif
}

void Ep128EmuGUI::applyRewindHistoryLimit()
{
  if (rewindHistoryLimitMiB != 16 &&
      rewindHistoryLimitMiB != 32 &&
      rewindHistoryLimitMiB != 64)
    rewindHistoryLimitMiB = 16;

  vmThread.setRewindHistoryLimit(
      size_t(rewindHistoryLimitMiB) * size_t(1024) * size_t(1024));
  vmThread.setRewindCaptureUserEnabled(rewindCaptureUserEnabled);
  const Fl_Menu_Item *captureItem = mainMenuBar->find_item(
      "Machine/Frame Navigation/Record frame history");
  if (captureItem) {
    if (rewindCaptureUserEnabled)
      const_cast<Fl_Menu_Item *>(captureItem)->set();
    else
      const_cast<Fl_Menu_Item *>(captureItem)->clear();
  }

  char path[96];
  std::snprintf(path, sizeof(path),
                "Machine/Frame Navigation/History size/%d MiB",
                rewindHistoryLimitMiB);
  const Fl_Menu_Item *selected = mainMenuBar->find_item(path);
  if (selected)
    const_cast<Fl_Menu_Item *>(selected)->setonly();
  if (mainMenuBar)
    mainMenuBar->redraw();
}

void Ep128EmuGUI::createMenus()
{
#if defined(__APPLE__) || defined(WIN32)
  applyMenuSize();
#endif
#if defined(LINUX_FLTK_VERSION) && (LINUX_FLTK_VERSION >= 10302)
  {
    // work around broken auto-repeat on Linux
    Bool    supportedReturn = False;
    (void) XkbSetDetectableAutoRepeat(fl_display, True, &supportedReturn);
  }
#endif
  int     iconNum = (typeid(vm) == typeid(Ep128::Ep128VM) ?
                     0 : (typeid(vm) == typeid(ZX128::ZX128VM) ?
                          1 : (typeid(vm) == typeid(CPC464::CPC464VM) ?
                               2 : 3)));
  Ep128Emu::setWindowIcon(mainWindow, iconNum);
  Ep128Emu::setWindowIcon(diskConfigWindow->window, iconNum);
  Ep128Emu::setWindowIcon(displaySettingsWindow->window, iconNum);
  Ep128Emu::setWindowIcon(keyboardConfigWindow->window, iconNum);
  Ep128Emu::setWindowIcon(machineConfigWindow->window, iconNum);
  Ep128Emu::setWindowIcon(aboutWindow->window, 10);
  Ep128Emu::setWindowIcon(errorMessageWindow, 12);
  // hide configuration widgets that are not useful on the given machine
  // or emulator build
#ifdef ENABLE_SDEXT
  if (iconNum >= 1 && iconNum <= 2)     // Spectrum or CPC
#endif
  {
    machineConfigWindow->vmEnableSDExtValuator->hide();
    machineConfigWindow->vmEnableFileIOValuator->resize(30, 248, 250, 25);
    machineConfigWindow->sdExtROMFileNameValuator->hide();
    machineConfigWindow->sdExtROMFileNameButton->hide();
#ifndef ENABLE_SDEXT
    diskConfigWindow->ideConfigGroup->label("IDE");
    diskConfigWindow->sdCardImageGroup->hide();
#endif
  }
  if (iconNum != 0) {                   // not Enterprise
    remCaptionDisplay->hide();
    rem1StatusDisplay->hide();
    rem1NumberDisplay->hide();
    rem2StatusDisplay->hide();
    rem2NumberDisplay->hide();
    machineConfigWindow->vmCPUFrequencyValuator->deactivate();
    machineConfigWindow->vmSoundClockFrequencyValuator->deactivate();
    machineConfigWindow->enableMemoryTimingValuator->hide();
#ifndef ENABLE_RESID
  }
  {
#endif
    machineConfigWindow->sidConfigurationGroup->deactivate();
    machineConfigWindow->sidConfigurationGroup->hide();
    machineConfigWindow->vmConfigurationTabs->remove(
        *(machineConfigWindow->sidConfigurationGroup));
  }
  mainMenuBar->add("File/Configuration/Load from ASCII file (" EP128EMU_ALT_KEY_TEXT "Q)",
                   (char *) 0, &menuCallback_File_LoadConfig, (void *) this);
  mainMenuBar->add("File/Configuration/Load from binary file (" EP128EMU_ALT_KEY_TEXT "L)",
                   (char *) 0, &menuCallback_File_LoadFile, (void *) this);
  mainMenuBar->add("File/Configuration/Save as ASCII file",
                   (char *) 0, &menuCallback_File_SaveConfig, (void *) this);
  mainMenuBar->add("File/Configuration/Save",
                   (char *) 0, &menuCallback_File_SaveMainCfg, (void *) this);
  mainMenuBar->add("File/Configuration/Revert",
                   (char *) 0, &menuCallback_File_RevertCfg, (void *) this);
  mainMenuBar->add("File/Save snapshot (" EP128EMU_ALT_KEY_TEXT "S)",
                   (char *) 0, &menuCallback_File_SaveSnapshot, (void *) this);
  mainMenuBar->add("File/Load snapshot (" EP128EMU_ALT_KEY_TEXT "L)",
                   (char *) 0, &menuCallback_File_LoadSnapshot, (void *) this);
  mainMenuBar->add("File/Recent/Disk images/(empty)", (char *) 0,
                   (Fl_Callback *) 0, (void *) this, FL_MENU_INACTIVE);
  mainMenuBar->add("File/Recent/Tape images/(empty)", (char *) 0,
                   (Fl_Callback *) 0, (void *) this, FL_MENU_INACTIVE);
  mainMenuBar->add("File/Recent/Snapshots/(empty)", (char *) 0,
                   (Fl_Callback *) 0, (void *) this, FL_MENU_INACTIVE);
  mainMenuBar->add("File/Recent/Configurations/(empty)", (char *) 0,
                   (Fl_Callback *) 0, (void *) this, FL_MENU_INACTIVE);
  mainMenuBar->add("File/Recent/Clear all", (char *) 0,
                   &menuCallback_RecentClearAll, (void *) this);
  mainMenuBar->add("File/Quick snapshot/Set file name",
                   (char *) 0, &menuCallback_File_QSFileName, (void *) this);
  mainMenuBar->add("File/Quick snapshot/Save",
                   (char *) 0, &menuCallback_File_QSSave, (void *) this);
  mainMenuBar->add("File/Quick snapshot/Load",
                   (char *) 0, &menuCallback_File_QSLoad, (void *) this);
  mainMenuBar->add("File/Record demo",
                   (char *) 0, &menuCallback_File_RecordDemo, (void *) this);
  mainMenuBar->add("File/Stop demo (" EP128EMU_ALT_KEY_TEXT "K)",
                   (char *) 0, &menuCallback_File_StopDemo, (void *) this);
  mainMenuBar->add("File/Load demo (" EP128EMU_ALT_KEY_TEXT "L)",
                   (char *) 0, &menuCallback_File_LoadFile, (void *) this);
  mainMenuBar->add("File/Record audio/Start...",
                   (char *) 0, &menuCallback_File_RecordSound, (void *) this);
  mainMenuBar->add("File/Record audio/Stop",
                   (char *) 0, &menuCallback_File_StopSndRecord, (void *) this);
  mainMenuBar->add("File/Record video/Start...",
                   (char *) 0, &menuCallback_File_RecordVideo, (void *) this);
  mainMenuBar->add("File/Record video/Stop",
                   (char *) 0, &menuCallback_File_StopAVIRecord, (void *) this);
  mainMenuBar->add("File/Save screenshot (F12:auto)",
                   (char *) 0, &menuCallback_File_Screenshot, (void *) this);
  mainMenuBar->add("File/Save screenshot as...",
                   (char *) 0, &menuCallback_File_ScreenshotAs, (void *) this);
  mainMenuBar->add("File/Quit (Shift+F12)",
                   (char *) 0, &menuCallback_File_Quit, (void *) this);
  mainMenuBar->add("Machine/Speed/No limit",
                   (char *) 0, &menuCallback_Machine_FullSpeed, (void *) this);
  mainMenuBar->add("Machine/Speed/10% (" EP128EMU_ALT_KEY_TEXT "1)",
                   (char *) 0, &menuCallback_Machine_Speed_10, (void *) this);
  mainMenuBar->add("Machine/Speed/25% (" EP128EMU_ALT_KEY_TEXT "2)",
                   (char *) 0, &menuCallback_Machine_Speed_25, (void *) this);
  mainMenuBar->add("Machine/Speed/50% (" EP128EMU_ALT_KEY_TEXT "3)",
                   (char *) 0, &menuCallback_Machine_Speed_50, (void *) this);
  mainMenuBar->add("Machine/Speed/100% (" EP128EMU_ALT_KEY_TEXT "4, " EP128EMU_ALT_KEY_TEXT "E)",
                   (char *) 0, &menuCallback_Machine_Speed_100, (void *) this);
  mainMenuBar->add("Machine/Speed/200% (" EP128EMU_ALT_KEY_TEXT "5)",
                   (char *) 0, &menuCallback_Machine_Speed_200, (void *) this);
  mainMenuBar->add("Machine/Speed/400% (" EP128EMU_ALT_KEY_TEXT "6)",
                   (char *) 0, &menuCallback_Machine_Speed_400, (void *) this);
#if defined(__APPLE__) || defined(WIN32)
  mainMenuBar->add("Machine/Emulated machine/Enterprise",
                   (char *) 0, &menuCallback_Machine_RestartAsEP, (void *) this);
  mainMenuBar->add("Machine/Emulated machine/ZX Spectrum",
                   (char *) 0, &menuCallback_Machine_RestartAsZX, (void *) this);
  mainMenuBar->add("Machine/Emulated machine/Amstrad CPC",
                   (char *) 0, &menuCallback_Machine_RestartAsCPC, (void *) this);
  mainMenuBar->add("Machine/Emulated machine/Videoton TVC",
                   (char *) 0, &menuCallback_Machine_RestartAsTVC, (void *) this);
#endif
#if defined(__APPLE__) || defined(WIN32)
  if (iconNum == 1) { // Spectrum only; explicit experimental opt-in
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/SpeccyDOS 4.2 - WD1770 (bundled ROM)",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSBundled1770, (void *) this);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/SpeccyDOS 4.2 - WD1772 (bundled ROM)",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSBundled1772, (void *) this);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/Load external SpeccyDOS 4.x ROM...",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSROM, (void *) this);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/Insert SDD drive A...",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSDisk, (void *) this);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/Write protect drive A",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSWriteProtect,
                     (void *) this, FL_MENU_TOGGLE);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/Eject drive A",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSEject, (void *) this);
    mainMenuBar->add("Machine/SpeccyDOS (experimental)/Disable SpeccyDOS",
                     (char *) 0, &menuCallback_Machine_SpeccyDOSDisable, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Enable TR-DOS 5.03 (recommended)",
                     (char *) 0, &menuCallback_Machine_BetaDiskBundled503, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Enable TR-DOS 5.04T (verified)",
                     (char *) 0, &menuCallback_Machine_BetaDiskBundled504, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Enable TR-DOS 6.04",
                     (char *) 0, &menuCallback_Machine_BetaDiskBundled604, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Load external 16K TR-DOS ROM...",
                     (char *) 0, &menuCallback_Machine_BetaDiskROM, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Enter TR-DOS",
                     (char *) 0, &menuCallback_Machine_BetaDiskEnterDOS, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Auto-enter TR-DOS after reset or TRD insert",
                     (char *) 0, &menuCallback_Machine_BetaDiskAutoEnter,
                     (void *) this, FL_MENU_TOGGLE);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Insert TRD drive A...",
                     (char *) 0, &menuCallback_Machine_BetaDiskTRD, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Write protect drive A",
                     (char *) 0, &menuCallback_Machine_BetaDiskWriteProtect,
                     (void *) this, FL_MENU_TOGGLE);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Eject TRD drive A",
                     (char *) 0, &menuCallback_Machine_BetaDiskEject, (void *) this);
    mainMenuBar->add("Machine/Beta Disk (experimental)/Disable Beta Disk",
                     (char *) 0, &menuCallback_Machine_BetaDiskDisable, (void *) this);
  }
#endif
  if (iconNum == 0) {
    mainMenuBar->add("Machine/Printer/Capture printer output",
                     (char *) 0, &menuCallback_Machine_PrinterCapture,
                     (void *) this, FL_MENU_TOGGLE);
    mainMenuBar->add("Machine/Printer/Output extension/Text (.txt)",
                     (char *) 0, &menuCallback_Machine_PrinterFormatText,
                     (void *) this, FL_MENU_RADIO);
    mainMenuBar->add("Machine/Printer/Output extension/RAW (.raw)",
                     (char *) 0, &menuCallback_Machine_PrinterFormatRaw,
                     (void *) this, FL_MENU_RADIO);
    mainMenuBar->add("Machine/Printer/New file on next print",
                     (char *) 0, &menuCallback_Machine_PrinterNewFile,
                     (void *) this);
  }
  // Mac T013/T014: a descriptive label is not an FLTK accelerator.
  // Windows needs the same native menubar delivery as the accepted Mac case;
  // legacy FL_KEYDOWN and FL_SHORTCUT host routes remain as fallbacks.
#ifdef WIN32
  const int tapeOpenMenuShortcut = FL_ALT | 't';
#else
  const int tapeOpenMenuShortcut = 0;
#endif
  mainMenuBar->add("Machine/Tape/Select image file (" EP128EMU_ALT_KEY_TEXT "T)",
                   tapeOpenMenuShortcut,
                   &menuCallback_Machine_OpenTape, (void *) this);
  mainMenuBar->add("Machine/Tape/Force motor on",
                   (char *) 0, &menuCallback_Machine_TapeForce,
                   (void *) this, FL_MENU_TOGGLE);
  if (iconNum == 0) {
    mainMenuBar->add("Machine/Tape/Motor spin-up/0 ms (off)",
                     (char *) 0, &menuCallback_Machine_TapeSpin0,
                     (void *) this, FL_MENU_RADIO);
    mainMenuBar->add("Machine/Tape/Motor spin-up/500 ms",
                     (char *) 0, &menuCallback_Machine_TapeSpin500,
                     (void *) this, FL_MENU_RADIO);
    mainMenuBar->add("Machine/Tape/Motor spin-up/1000 ms",
                     (char *) 0, &menuCallback_Machine_TapeSpin1000,
                     (void *) this, FL_MENU_RADIO);
    mainMenuBar->add("Machine/Tape/Motor spin-up/1500 ms",
                     (char *) 0, &menuCallback_Machine_TapeSpin1500,
                     (void *) this, FL_MENU_RADIO);
  }
  mainMenuBar->add("Machine/Tape/Play (" EP128EMU_ALT_KEY_TEXT "P)",
                   (char *) 0, &menuCallback_Machine_TapePlay, (void *) this);
  mainMenuBar->add("Machine/Tape/Stop (" EP128EMU_ALT_KEY_TEXT "O)",
                   (char *) 0, &menuCallback_Machine_TapeStop, (void *) this);
  mainMenuBar->add("Machine/Tape/Record",
                   (char *) 0, &menuCallback_Machine_TapeRecord, (void *) this);
  mainMenuBar->add("Machine/Tape/Rewind/To beginning of tape (" EP128EMU_ALT_KEY_TEXT "R)",
                   (char *) 0, &menuCallback_Machine_TapeRewind, (void *) this);
  mainMenuBar->add("Machine/Tape/Rewind/To previous marker (" EP128EMU_ALT_KEY_TEXT ",)",
                   (char *) 0, &menuCallback_Machine_TapePrvCP, (void *) this);
  mainMenuBar->add("Machine/Tape/Rewind/By 10 seconds",
                   (char *) 0, &menuCallback_Machine_TapeBwd10s, (void *) this);
  mainMenuBar->add("Machine/Tape/Rewind/By 60 seconds",
                   (char *) 0, &menuCallback_Machine_TapeBwd60s, (void *) this);
  mainMenuBar->add("Machine/Tape/Fast forward/To next marker (" EP128EMU_ALT_KEY_TEXT ".)",
                   (char *) 0, &menuCallback_Machine_TapeNxtCP, (void *) this);
  mainMenuBar->add("Machine/Tape/Fast forward/By 10 seconds",
                   (char *) 0, &menuCallback_Machine_TapeFwd10s, (void *) this);
  mainMenuBar->add("Machine/Tape/Fast forward/By 60 seconds",
                   (char *) 0, &menuCallback_Machine_TapeFwd60s, (void *) this);
  mainMenuBar->add("Machine/Tape/Marker/Add",
                   (char *) 0, &menuCallback_Machine_AddCP, (void *) this);
  mainMenuBar->add("Machine/Tape/Marker/Delete nearest",
                   (char *) 0, &menuCallback_Machine_DeleteCP, (void *) this);
  mainMenuBar->add("Machine/Tape/Marker/Delete all",
                   (char *) 0, &menuCallback_Machine_DeleteCPs, (void *) this);
  mainMenuBar->add("Machine/Tape/Close",
                   (char *) 0, &menuCallback_Machine_TapeClose, (void *) this);
  mainMenuBar->add("Machine/Reset/Reset",
                   (char *) 0, &menuCallback_Machine_Reset, (void *) this);
  mainMenuBar->add("Machine/Reset/Force reset",
                   (char *) 0, &menuCallback_Machine_ColdReset, (void *) this);
  mainMenuBar->add("Machine/Reset/Reset clock frequencies",
                   (char *) 0, &menuCallback_Machine_ResetFreqs, (void *) this);
  mainMenuBar->add("Machine/Reset/Reset machine configuration (Shift+F11)",
                   (char *) 0, &menuCallback_Machine_ResetAll, (void *) this);
  mainMenuBar->add("Machine/Quick configuration/Load config 1 (PageDown)",
                   (char *) 0, &menuCallback_Machine_QuickCfgL1, (void *) this);
  mainMenuBar->add("Machine/Quick configuration/Load config 2 (PageUp)",
                   (char *) 0, &menuCallback_Machine_QuickCfgL2, (void *) this);
  mainMenuBar->add("Machine/Quick configuration/Save config 1",
                   (char *) 0, &menuCallback_Machine_QuickCfgS1, (void *) this);
  mainMenuBar->add("Machine/Quick configuration/Save config 2",
                   (char *) 0, &menuCallback_Machine_QuickCfgS2, (void *) this);
  mainMenuBar->add("Machine/Toggle pause",
                   (char *) 0, &menuCallback_Machine_Pause, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/1 frame backward",
                   (char *) 0,
                   &menuCallback_Machine_PreviousFrame, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/1 frame forward",
                   (char *) 0,
                   &menuCallback_Machine_NextFrame, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/100 frames backward",
                   (char *) 0,
                   &menuCallback_Machine_Previous100, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/100 frames forward",
                   (char *) 0,
                   &menuCallback_Machine_Next100, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/Oldest frame",
                   (char *) 0,
                   &menuCallback_Machine_OldestFrame, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/Latest frame",
                   (char *) 0,
                   &menuCallback_Machine_LatestFrame, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/Record frame history",
                   (char *) 0,
                   &menuCallback_Machine_RewindCapture, (void *) this);
  {
    int n = mainMenuBar->find_index(
        "Machine/Frame Navigation/Record frame history");
    if (n >= 0)
      mainMenuBar->mode(n, FL_MENU_TOGGLE);
  }
  mainMenuBar->add("Machine/Frame Navigation/History size/16 MiB",
                   (char *) 0,
                   &menuCallback_Machine_RewindHistory16, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/History size/32 MiB",
                   (char *) 0,
                   &menuCallback_Machine_RewindHistory32, (void *) this);
  mainMenuBar->add("Machine/Frame Navigation/History size/64 MiB",
                   (char *) 0,
                   &menuCallback_Machine_RewindHistory64, (void *) this);
  {
    const char *paths[] = {
      "Machine/Frame Navigation/History size/16 MiB",
      "Machine/Frame Navigation/History size/32 MiB",
      "Machine/Frame Navigation/History size/64 MiB"
    };
    for (size_t i = 0; i < 3; i++) {
      int n = mainMenuBar->find_index(paths[i]);
      if (n >= 0)
        mainMenuBar->mode(n, FL_MENU_RADIO);
    }
    applyRewindHistoryLimit();
  }
  {
    int n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_NextFrame);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
    n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_PreviousFrame);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
    n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Previous100);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
    n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Next100);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
    n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_OldestFrame);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
    n = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_LatestFrame);
    if (n >= 0)
      const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->deactivate();
  }
  mainMenuBar->add("Machine/Configure... (Shift+F10)",
                   (char *) 0, &menuCallback_Machine_Configure, (void *) this);
  mainMenuBar->add("Options/Display/Set size to 384x288",
                   (char *) 0, &menuCallback_Options_DpySize1, (void *) this);
  mainMenuBar->add("Options/Display/Set size to 768x576",
                   (char *) 0, &menuCallback_Options_DpySize2, (void *) this);
  mainMenuBar->add("Options/Display/Set size to 1152x864",
                   (char *) 0, &menuCallback_Options_DpySize3, (void *) this);
  mainMenuBar->add("Options/Display/Set size to Pixel Perfect",
                   (char *) 0, &menuCallback_Options_DpyPixelPerfect,
                   (void *) this);
  updatePixelPerfectMenuLabel(true);
  mainMenuBar->add("Options/Display/Cycle display mode",
                   (char *) 0, &menuCallback_Options_DpyMode, (void *) this);
#if defined(__APPLE__) || defined(WIN32)
  mainMenuBar->add("Options/Display/Menu size/Standard",
                   (char *) 0, &menuCallback_Options_MenuSizeStandard,
                   (void *) this);
  mainMenuBar->add("Options/Display/Menu size/Large",
                   (char *) 0, &menuCallback_Options_MenuSizeLarge,
                   (void *) this);
  {
    int n = mainMenuBar->find_index("Options/Display/Menu size/Standard");
    if (n >= 0)
      mainMenuBar->mode(n, FL_MENU_RADIO);
    n = mainMenuBar->find_index("Options/Display/Menu size/Large");
    if (n >= 0)
      mainMenuBar->mode(n, FL_MENU_RADIO);
    applyMenuSize();
  }
#endif
  mainMenuBar->add("Options/Display/Configure... (Shift+F9)",
                   (char *) 0, &menuCallback_Options_DpyConfig, (void *) this);
  mainMenuBar->add("Options/Notifications/Show informational dialogs",
                   (char *) 0, &menuCallback_Options_InformationDialogs,
                   (void *) this, FL_MENU_TOGGLE);
  mainMenuBar->add("Options/Display/Runtime status display",
                   (char *) 0, &menuCallback_Options_RuntimeHUD, (void *) this);
  {
    int n = mainMenuBar->find_index("Options/Display/Runtime status display");
    if (n >= 0) {
      mainMenuBar->mode(n, FL_MENU_TOGGLE);
      if (runtimeHUDEnabled)
        const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + n)->set();
    }
  }
#if defined(__APPLE__) || defined(WIN32)
  if (iconNum == 0) {
    mainMenuBar->add("Options/Sound/Tape remote relay click",
                     (char *) 0, &menuCallback_Options_TapeRelayClick,
                     (void *) this, FL_MENU_TOGGLE);
  }
#endif
  mainMenuBar->add("Options/Sound/Increase volume",
                   (char *) 0, &menuCallback_Options_SndIncVol, (void *) this);
  mainMenuBar->add("Options/Sound/Decrease volume",
                   (char *) 0, &menuCallback_Options_SndDecVol, (void *) this);
  // The legacy Alt+U text is only a label. On Windows FLTK can route the
  // first Alt+letter through the menu's FL_SHORTCUT path instead of to the
  // emulator display. Give that path a real accelerator too, as for the
  // accepted macOS frame-navigation shortcut; do not steal guest U presses.
#ifdef WIN32
  const int soundConfigMenuShortcut = FL_ALT | 'u';
#else
  const int soundConfigMenuShortcut = 0;
#endif
  mainMenuBar->add("Options/Sound/Configure... (" EP128EMU_ALT_KEY_TEXT "U)",
                   soundConfigMenuShortcut,
                   &menuCallback_Options_SndConfig, (void *) this);
  if (iconNum != 1) {                   // not Spectrum
    mainMenuBar->add("Options/Disk/Remove floppy/Drive A", (char *) 0,
                     &menuCallback_Options_FloppyRmA, (void *) this);
    mainMenuBar->add("Options/Disk/Remove floppy/Drive B", (char *) 0,
                     &menuCallback_Options_FloppyRmB, (void *) this);
    mainMenuBar->add("Options/Disk/Remove floppy/Drive C", (char *) 0,
                     &menuCallback_Options_FloppyRmC, (void *) this);
    mainMenuBar->add("Options/Disk/Remove floppy/Drive D", (char *) 0,
                     &menuCallback_Options_FloppyRmD, (void *) this);
    mainMenuBar->add("Options/Disk/Remove floppy/All drives", (char *) 0,
                     &menuCallback_Options_FloppyRmv, (void *) this);
    mainMenuBar->add("Options/Disk/Replace floppy/Drive A", (char *) 0,
                     &menuCallback_Options_FloppyRpA, (void *) this);
    mainMenuBar->add("Options/Disk/Replace floppy/Drive B", (char *) 0,
                     &menuCallback_Options_FloppyRpB, (void *) this);
    mainMenuBar->add("Options/Disk/Replace floppy/Drive C", (char *) 0,
                     &menuCallback_Options_FloppyRpC, (void *) this);
    mainMenuBar->add("Options/Disk/Replace floppy/Drive D", (char *) 0,
                     &menuCallback_Options_FloppyRpD, (void *) this);
    mainMenuBar->add("Options/Disk/Replace floppy/All drives", (char *) 0,
                     &menuCallback_Options_FloppyRpl, (void *) this);
    mainMenuBar->add("Options/Disk/Configure... (" EP128EMU_ALT_KEY_TEXT "D)", (char *) 0,
                     &menuCallback_Options_FloppyCfg, (void *) this);
  }
  else {
    diskStatusDisplayGroup->hide();
  }
  mainMenuBar->add("Options/Process priority/Idle",
                   (char *) 0, &menuCallback_Options_PPriority, (void *) this);
  mainMenuBar->add("Options/Process priority/Below normal",
                   (char *) 0, &menuCallback_Options_PPriority, (void *) this);
  mainMenuBar->add("Options/Process priority/Normal",
                   (char *) 0, &menuCallback_Options_PPriority, (void *) this);
  mainMenuBar->add("Options/Process priority/Above normal",
                   (char *) 0, &menuCallback_Options_PPriority, (void *) this);
  mainMenuBar->add("Options/Process priority/High",
                   (char *) 0, &menuCallback_Options_PPriority, (void *) this);
  mainMenuBar->add("Options/Keyboard map (" EP128EMU_ALT_KEY_TEXT "I)",
                   (char *) 0, &menuCallback_Options_KbdConfig, (void *) this);
  mainMenuBar->add("Options/Application shortcuts...",
                   (char *) 0, &menuCallback_Options_AppShortcuts,
                   (void *) this);
  mainMenuBar->add("Options/Set working directory (" EP128EMU_ALT_KEY_TEXT "F)",
                   (char *) 0, &menuCallback_Options_FileIODir, (void *) this);
  mainMenuBar->add("Debug/Start debugger (" EP128EMU_ALT_KEY_TEXT "B)",
                   (char *) 0, &menuCallback_Debug_OpenDebugger, (void *) this);
  mainMenuBar->add("Help/About",
                   (char *) 0, &menuCallback_Help_About, (void *) this);
  refreshApplicationShortcutMenus();
  // "File/Record video/Stop"
  getMenuItem(2).deactivate();
  // "Machine/Speed/No limit"
  mainMenuBar->mode(getMenuItemIndex(3), FL_MENU_TOGGLE);
  // "Options/Process priority/Idle"
  // "Options/Process priority/Below normal"
  // "Options/Process priority/Normal"
  // "Options/Process priority/Above normal"
  // "Options/Process priority/High"
  for (int i = 4; i <= 8; i++)
    mainMenuBar->mode(getMenuItemIndex(i), FL_MENU_RADIO);
#if !defined(WIN32) && !defined(__APPLE__)
  // TODO: implement process priority setting on other non-Windows platforms
  for (int i = 4; i <= 8; i++)
    getMenuItem(i).deactivate();
#endif
  updateMenu();
}

#ifdef WIN32
static void ep128emuWinDropDispatch(void *userData,const std::string& files)
{
  reinterpret_cast<Ep128EmuGUI *>(userData)->queueWindowsDroppedFiles(files);
}

void Ep128EmuGUI::queueWindowsDroppedFiles(const std::string& files)
{
  pendingDroppedFiles=files;
  Fl::remove_timeout(&droppedFileCallback,(void *)this);
  Fl::add_timeout(0.001,&droppedFileCallback,(void *)this);
}
#endif

void Ep128EmuGUI::run()
{
  std::string startupError;
  config.setErrorCallback(&errorMessageCallback, (void *) this);
  // set initial window size from saved configuration
  flDisplay->setFLTKEventCallback(&handleFLTKEvent, (void *) this);
  mainWindow->resizable((Fl_Widget *) 0);
  emulatorWindow->color(47, 47);
  resizeWindow(config.display.width, config.display.height);
  updateDisplay_windowTitle();
  // create menu bar
  createMenus();
  // load GUI configuration before showing the main window, so a saved
  // desktop position can be restored without a visible jump from (0, 0).
  try {
    Ep128Emu::File  f("gui_cfg.dat", true);
    try {
      guiConfig.registerChunkType(f);
      f.processAllChunks();
    }
    catch (std::exception& e) {
      startupError = e.what();
    }
  }
  catch (...) {
  }
#ifdef WIN32
  // Windows Program Files is not a writable default for EF02 screenshot.
  // Mirror the accepted Mac Pictures/home fallback using Unicode Win32 APIs.
  if (screenshotDirectory.empty() || screenshotDirectory == ".") {
    const wchar_t *home = _wgetenv(L"USERPROFILE");
    if (home && *home) {
      std::wstring pictures(home);
      pictures += L"\\Pictures";
      DWORD attributes = GetFileAttributesW(pictures.c_str());
      const bool valid = (attributes != INVALID_FILE_ATTRIBUTES &&
                          (attributes & FILE_ATTRIBUTE_DIRECTORY));
      screenshotDirectory = ep128emuFFUTF8(valid ? pictures : std::wstring(home));
    }
  }
#else
  // Finder starts applications with a launch-dependent working directory.
  // Migrate the legacy "." screenshot default to a writable user location.
  if (screenshotDirectory.empty() || screenshotDirectory == ".") {
    const char *home = std::getenv("HOME");
    if (home && home[0]) {
      std::string pictures = std::string(home) + "/Pictures";
      struct stat st;
      screenshotDirectory = (::stat(pictures.c_str(), &st) == 0 &&
                             S_ISDIR(st.st_mode) ? pictures : std::string(home));
    }
  }
#endif
#if defined(__APPLE__) || defined(WIN32)
  // Apply the persisted menu-size choice before the main window is shown.
  applyMenuSize();
#endif
  // Apply persisted rewind budget after gui_cfg.dat has been loaded.
  applyRewindHistoryLimit();
  if (printerOutputFormat != 0 && printerOutputFormat != 1)
    printerOutputFormat = 0;
#ifdef WIN32
  // The installed executable usually lives in read-only Program Files.
  // EF05 accepted GUI uses '.' for legacy printer output; migrate only
  // the built-in default to the user's writable Documents folder.
  if (printerOutputDirectory.empty() || printerOutputDirectory == ".") {
    const wchar_t *home = _wgetenv(L"USERPROFILE");
    if (home && *home) {
      std::wstring documents(home);
      documents += L"\\Documents";
      DWORD attrs = GetFileAttributesW(documents.c_str());
      const bool exists = (attrs != INVALID_FILE_ATTRIBUTES &&
                           (attrs & FILE_ATTRIBUTE_DIRECTORY));
      printerOutputDirectory = ep128emuFFUTF8(
          exists ? documents : std::wstring(home));
    }
  }
#endif
  if (tapeMotorSpinUpDelayMs != 0 && tapeMotorSpinUpDelayMs != 500 &&
      tapeMotorSpinUpDelayMs != 1000 && tapeMotorSpinUpDelayMs != 1500)
    tapeMotorSpinUpDelayMs = 1000;
  {
    // Historical default migrations. Always migrate only an exact known
    // built-in pair so a user-customized mapping is never overwritten.
    if (appShortcutStrings[EP_SC_PREVIOUS_FRAME] == "Option+Shift+N" &&
        appShortcutStrings[EP_SC_NEXT_FRAME] == "Option+N") {
      appShortcutStrings[EP_SC_PREVIOUS_FRAME] = "Comma";
      appShortcutStrings[EP_SC_NEXT_FRAME] = "Period";
    }
    // T076 unifies timeline navigation on the horizontal arrow-key family:
    // Left/Right = one frame, Shift+Left/Right = 100, Home/End = endpoints.
    if (appShortcutStrings[EP_SC_PREVIOUS_FRAME] == "Comma" &&
        appShortcutStrings[EP_SC_NEXT_FRAME] == "Period") {
      appShortcutStrings[EP_SC_PREVIOUS_FRAME] = "Left";
      appShortcutStrings[EP_SC_NEXT_FRAME] = "Right";
    }

    std::string values[EP_SC_COUNT];
    for (int i = 0; i < EP_SC_COUNT; i++)
      values[i] = appShortcutStrings[i];
    std::string shortcutError;
    if (!setApplicationShortcuts(values, shortcutError)) {
      resetApplicationShortcuts();
      if (!startupError.empty())
        startupError += "\n\n";
      startupError += "Invalid saved application shortcut settings; defaults restored.\n";
      startupError += shortcutError;
    }
  }
  refreshRecentMenus();
#ifdef __APPLE__
  if (pixelPerfectEnabled && emulatorWindow->as_gl_window())
    Fl::use_high_res_GL(1);
#endif
  updateMenu();
  {
    const Fl_Menu_Item *m =
        mainMenuBar->find_item("Options/Display/Runtime status display");
    if (m) {
      Fl_Menu_Item *mm = const_cast<Fl_Menu_Item *>(m);
      if (runtimeHUDEnabled)
        mm->set();
      else
        mm->clear();
    }
  }
  updateDisplay_windowTitle();
  restoreMainWindowPosition();
  // Native window-close must take the same SDD durability path as File/Quit.
  // Without a callback FLTK hides mainWindow immediately, bypassing it.
  mainWindow->callback(&menuCallback_File_Quit, (void *) this);
  mainWindow->show();
#ifdef __APPLE__
  ep128emuSetMacOSTitlebarStatus(mainWindow, "FPS  --.-", runtimeHUDEnabled);
#endif
  emulatorWindow->show();
#ifdef WIN32
  // Win32 Explorer/Parallels CF_HDROP works via an explicit OLE native drop
  // target. FLTK's GL child does not reliably deliver FL_DND_* on Windows.
  (void) Ep128EmuWinDrop::attach(
      reinterpret_cast<void *>(fl_xid(mainWindow)),
      reinterpret_cast<void *>(fl_xid(emulatorWindow)),
      &ep128emuWinDropDispatch,(void *)this);
#endif
  updatePixelPerfectMenuLabel(true);
  if (pixelPerfectEnabled) {
    // High-resolution GL was selected before the initial show() above.
    // Do not immediately hide/show the just-created Cocoa GL child: on an
    // external non-Retina 4K display pixels_per_unit() legitimately remains
    // 1.0, which previously looked like a failed high-res switch and could
    // crash FLTK while recreating the startup GL context.
    applyPixelPerfectDisplayState(false);
  }
  vmThread.lock(0x7FFFFFFF);
  vmThread.setUserData((void *) this);
  vmThread.setErrorCallback(&errorMessageCallback);
  vmThread.setProcessCallback(&pollJoystickInput);
  vm.setFileNameCallback(&fileNameCallback, (void *) this);
  vm.setBreakPointCallback(&Ep128EmuGUI_DebugWindow::breakPointCallback,
                           (void *) debugWindow);
  // Protected by the already-held VMThread lock. Enterprise only.
  if (typeid(vm) == typeid(Ep128::Ep128VM)) {
    Ep128::Ep128VM& ep = static_cast<Ep128::Ep128VM&>(vm);
    ep.setTapeMotorSpinUpDelayMs(getEffectiveTapeMotorSpinUpDelayMs());
    // 100 ms acoustic gate: real manual REM gap measured 29-32 ms, while
    // actual long motor pauses exceed 800 ms. Never delay raw motor OFF.
    // Sound-only debounce is independent of machine speed. The GUI already
    // suppresses relay playback when not at 100%; bypassing this filter at
    // other speeds could leave a stale zero-delay setting at the first header.
    ep.setTapeRelayReleaseDelayMs(100U);
    ep.setPrinterCaptureEnabled(printerCaptureEnabled);
  }
  applyEmulatorConfiguration();
  vmThread.unlock();
  // run emulation
  vmThread.pause(false);
#if defined(__APPLE__) || defined(WIN32)
  // After a machine switch the new FLTK/GL display exists but may not own
  // keyboard focus. This also affects the first Alt combination on Windows.
  // Restore the focus once on startup, not repeatedly (dialogs must retain
  // their own focus when deliberately opened).
#ifdef WIN32
  // Separate Win32 foreground ownership from FLTK widget focus. On a
  // user-initiated restart, the old process grants this new process
  // foreground activation rights before exiting.
  HWND topLevel = (HWND) fl_xid(mainWindow);
  if (topLevel && GetForegroundWindow() != topLevel)
    (void) SetForegroundWindow(topLevel);
#endif
  emulatorWindow->take_focus();
#endif
  // Never enter the modal error/updateDisplay loop before the OpenGL child
  // window has been shown and initialized. Startup configuration validation
  // can legitimately fail (for example after shortcut migration), and the
  // old early errorMessage() path could ask FLTK to create a Cocoa GL context
  // before the window was ready.
  if (!startupError.empty())
    errorMessage(startupError.c_str());
  do {
    updateDisplay();
  } while (mainWindow->shown() && !exitFlag);
#ifdef WIN32
  Ep128EmuWinDrop::detach();
#endif
  if ((displayMode & 2) == 0) {
    mainWindowPositionX = mainWindow->x();
    mainWindowPositionY = mainWindow->y();
  }
  // close windows and stop emulation thread
  browseFileWindowShowFlag = false;
  windowToShow = (Fl_Window *) 0;
  if (errorMessageWindow->shown())
    errorMessageWindow->hide();
  debugWindowShowFlag = false;
  if (debugWindow->shown())
    debugWindow->hide();
  if (debugWindowOpenFlag) {
    debugWindowOpenFlag = false;
    unlockVMThread();
  }
  updateDisplay();
  Fl::unlock();
  vmThread.quit(true);
  Fl::lock();
  if (mainWindow->shown())
    mainWindow->hide();
  updateDisplay();
  if (errorFlag)
    errorMessage("exiting due to a fatal error");
  // if still recording a demo, stop now, and write file
  try {
    vm.stopDemo();
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
  closeDemoFile(false);
  Fl::remove_timeout(&archiveAutoRunCallback, (void *) this);
  try {
    restoreArchiveAutoRunSpeed();
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
  Ep128EmuArchiveDrop::removeTemporaryDirectory(archiveTemporaryDirectory);
  archiveTemporaryDirectory.clear();
  // save GUI configuration
  try {
    Ep128Emu::File  f;
    guiConfig.saveState(f);
    f.writeFile("gui_cfg.dat", true);
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
}

void Ep128EmuGUI::restoreMainWindowPosition()
{
  int xPos = mainWindowPositionX;
  int yPos = mainWindowPositionY;
  int screenX = 0;
  int screenY = 0;
  int screenW = 0;
  int screenH = 0;

  if (xPos <= -32000 || yPos <= -32000) {
    Fl::screen_work_area(screenX, screenY, screenW, screenH);
    xPos = screenX + ((screenW - mainWindow->w()) >> 1);
    yPos = screenY + ((screenH - mainWindow->h()) >> 1);
  }
  else {
    int screenNum = Fl::screen_num(xPos, yPos,
                                   mainWindow->w(), mainWindow->h());
    if (screenNum < 0)
      screenNum = 0;
    Fl::screen_work_area(screenX, screenY, screenW, screenH, screenNum);
  }

  if (mainWindow->w() <= screenW) {
    if (xPos < screenX)
      xPos = screenX;
    if ((xPos + mainWindow->w()) > (screenX + screenW))
      xPos = screenX + screenW - mainWindow->w();
  }
  else
    xPos = screenX;
  if (mainWindow->h() <= screenH) {
    if (yPos < screenY)
      yPos = screenY;
    if ((yPos + mainWindow->h()) > (screenY + screenH))
      yPos = screenY + screenH - mainWindow->h();
  }
  else
    yPos = screenY;

  mainWindowPositionX = xPos;
  mainWindowPositionY = yPos;
  mainWindow->position(xPos, yPos);
}

void Ep128EmuGUI::positionAuxiliaryWindow(Fl_Window *w)
{
  if (w == (Fl_Window *) 0)
    return;
  int screenNum = Fl::screen_num(mainWindow->x(), mainWindow->y(),
                                 mainWindow->w(), mainWindow->h());
  if (screenNum < 0)
    screenNum = 0;
  int screenX = 0;
  int screenY = 0;
  int screenW = 0;
  int screenH = 0;
  Fl::screen_work_area(screenX, screenY, screenW, screenH, screenNum);

  const int auxiliaryWindowGap = 12;
  int xPos = mainWindow->x() - w->w() - auxiliaryWindowGap;
  int yPos = mainWindow->y() + ((mainWindow->h() - w->h()) >> 1);

  if (w->w() <= screenW) {
    // Prefer the left side of the emulator. If that would place the
    // auxiliary window outside the visible work area, use the right side
    // when possible; only then fall back to clamping at the screen edge.
    if (xPos < screenX) {
      int rightPos = mainWindow->x() + mainWindow->w() + auxiliaryWindowGap;
      if ((rightPos + w->w()) <= (screenX + screenW))
        xPos = rightPos;
      else
        xPos = screenX;
    }
    if ((xPos + w->w()) > (screenX + screenW))
      xPos = screenX + screenW - w->w();
  }
  else
    xPos = screenX;
  if (w->h() <= screenH) {
    if (yPos < screenY)
      yPos = screenY;
    if ((yPos + w->h()) > (screenY + screenH))
      yPos = screenY + screenH - w->h();
  }
  else
    yPos = screenY;
  w->position(xPos, yPos);
}

void Ep128EmuGUI::resizeWindow(int w, int h)
{
  if ((displayMode & 2) == 0) {
    config.display.width = w;
    config.display.height = h;
    if ((displayMode & 1) == 0)
      h += (w >= 745 ? 30 : 60);
#ifdef __APPLE__
    ep128emuResizeMacOSWindowExact(mainWindow, mainWindow->x(), mainWindow->y(),
                                   w, h);
#else
    mainWindow->resize(mainWindow->x(), mainWindow->y(), w, h);
#endif
    if ((displayMode & 1) == 0)
      mainWindow->size_range(384, 348,
                             (pixelPerfectEnabled ? 6144 : 1536),
                             (pixelPerfectEnabled ? 4668 : 1182));
    else
      mainWindow->size_range(384, 288,
                             (pixelPerfectEnabled ? 6144 : 1536),
                             (pixelPerfectEnabled ? 4608 : 1152));
  }
  mainWindow->cursor(!(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
}

void Ep128EmuGUI::sendMouseEvent(bool enableButtons, bool mouseWheelEvent)
{
  int xPos = Fl::event_x() - mouseXOffset;
  int yPos = Fl::event_y() - mouseYOffset;
  int contentW = emulatorWindow->w() - (mouseXOffset << 1);
  int contentH = emulatorWindow->h() - (mouseYOffset << 1);
  if (!pixelPerfectEnabled) {
    contentW = emulatorWindow->w();
    contentH = emulatorWindow->h();
  }
  uint8_t edgeFlags = (xPos < (contentW - mouseXMin) ? 0x00 : 0x08)
                      | (xPos >= mouseXMin ? 0x00 : 0x04)
                      | (yPos < (contentH - mouseYMin) ? 0x00 : 0x02)
                      | (yPos >= mouseYMin ? 0x00 : 0x01);
  xPos = int(float(xPos) * mouseXScale + 0.5f);
  yPos = int(float(yPos) * mouseYScale + 0.5f);
  uint8_t buttonState = 0x00;
  uint8_t mouseWheelEvents = 0x00;
  if (enableButtons) {
    int     tmp = Fl::event_buttons();
    if (tmp & FL_BUTTONS) {
      buttonState = ((tmp & FL_BUTTON(FL_LEFT_MOUSE)) ? 0x01 : 0x00)
                    | ((tmp & FL_BUTTON(FL_RIGHT_MOUSE)) ? 0x02 : 0x00)
                    | ((tmp & FL_BUTTON(FL_MIDDLE_MOUSE)) ? 0x04 : 0x00)
                    | ((tmp & FL_BUTTON(4)) ? 0x08 : 0x00)
                    | ((tmp & FL_BUTTON(5)) ? 0x10 : 0x00);
    }
  }
  if (mouseWheelEvent) {
    int     dX = Fl::event_dx();
    int     dY = Fl::event_dy();
    if (dY < 0)
      mouseWheelEvents = mouseWheelEvents | 0x01;
    else if (dY > 0)
      mouseWheelEvents = mouseWheelEvents | 0x02;
    if (dX < 0)
      mouseWheelEvents = mouseWheelEvents | 0x04;
    else if (dX > 0)
      mouseWheelEvents = mouseWheelEvents | 0x08;
  }
  int     dX =
      (EP128EMU_EXPECT(prvMouseXPos != -32768) ? (prvMouseXPos - xPos) : 0);
  int     dY =
      (EP128EMU_EXPECT(prvMouseYPos != -32768) ? (prvMouseYPos - yPos) : 0);
  prvMouseXPos = xPos;
  prvMouseYPos = yPos;
  if (edgeFlags) {
    // if the pointer is near the edges of the emulator window:
#if defined(__linux) || defined(__linux__) || defined(WIN32)
    if (displayMode == 3 && Fl::focus() == emulatorWindow) {
      // in full screen mode with no menu bar, if the emulator window has the
      // focus, then warp the pointer to the center of the screen
      prvMouseXPos = -32768;
      prvMouseYPos = -32768;
#  ifdef WIN32
      SetCursorPos(emulatorWindow->w() >> 1, emulatorWindow->h() >> 1);
#  else
      XWarpPointer(fl_display, None, fl_xid(emulatorWindow), 0, 0, 0, 0,
                   emulatorWindow->w() >> 1, emulatorWindow->h() >> 1);
      XSync(fl_display, False);
#  endif
      prvMouseXPos = -32768;
      prvMouseYPos = -32768;
    }
    else
#endif
    {
      // otherwise, simulate fast motion towards the edges
      if ((edgeFlags & 0x04) && dX < 16)
        dX = 16;
      else if ((edgeFlags & 0x08) && dX > -16)
        dX = -16;
      if ((edgeFlags & 0x01) && dY < 16)
        dY = 16;
      else if ((edgeFlags & 0x02) && dY > -16)
        dY = -16;
    }
  }
  if ((dX | dY | int(mouseWheelEvents)) == 0 &&
      buttonState == prvMouseButtonState) {
    return;
  }
  dX = (dX > -128 ? (dX < 127 ? dX : 127) : -128);
  dY = (dY > -128 ? (dY < 127 ? dY : 127) : -128);
  prvMouseButtonState = buttonState;
  vmThread.setMouseState(int8_t(dX), int8_t(dY), buttonState, mouseWheelEvents);
}

int Ep128EmuGUI::handleFLTKEvent(void *userData, int event)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
#ifdef WIN32
  // Only Alt-related FLTK delivery and focus transitions, never guest text.
  if (event == FL_KEYDOWN || event == FL_KEYUP || event == FL_SHORTCUT) {
    const int k = Fl::event_key();
    if (Fl::event_alt() || k == 't' || k == 'u' ||
        k == FL_Alt_L || k == FL_Alt_R)
      ep128emuWindowsGuiTrace("FLTK-EVENT", event, k,
                              Fl::event_state());
  }
  else if (event == FL_FOCUS || event == FL_UNFOCUS) {
    ep128emuWindowsGuiTrace("FOCUS-EVENT", event, 0, 0);
  }
#endif
  switch (event) {
  case FL_FOCUS:
    return 1;
  case FL_UNFOCUS:
    gui_.functionKeyState = 0U;
    gui_.appShortcutPressedMask = 0U;
    Fl::remove_timeout(&Ep128EmuGUI::nextFrameRepeatCallback, (void *) &gui_);
    Fl::remove_timeout(&Ep128EmuGUI::previousFrameRepeatCallback,
                       (void *) &gui_);
    try {
      gui_.vmThread.resetKeyboard();
    }
    catch (std::exception& e) {
      gui_.errorMessage(e.what());
    }
    return 1;
  case FL_DND_ENTER:
#ifdef __APPLE__
    ep128emuRememberMacOSDragSourceApplication();
#endif
    return 1;
  case FL_DND_DRAG:
    return 1;
  case FL_DND_RELEASE:
#ifdef __APPLE__
    // In case FL_DND_ENTER arrived after macOS already activated ep128emu,
    // keep the earliest non-self frontmost application that was observed.
    ep128emuRememberMacOSDragSourceApplication();
#endif
    gui_.dropPasteExpected = true;
    return 1;
  case FL_DND_LEAVE:
    gui_.dropPasteExpected = false;
#ifdef __APPLE__
    ep128emuClearRememberedMacOSDragSourceApplication();
#endif
    return 1;
  case FL_PASTE:
    if (gui_.dropPasteExpected) {
      gui_.dropPasteExpected = false;
      if (Fl::event_text() && Fl::event_length() > 0) {
        gui_.pendingDroppedFiles.assign(Fl::event_text(),
                                        size_t(Fl::event_length()));
        Fl::remove_timeout(&droppedFileCallback, userData);
        Fl::add_timeout(0.001, &droppedFileCallback, userData);
      }
      return 1;
    }
    return 0;
  case FL_ENTER:
  case FL_LEAVE:
    return 1;
  case FL_MOVE:
    gui_.sendMouseEvent(false, false);
    return 1;
  case FL_MOUSEWHEEL:
    gui_.sendMouseEvent(true, true);
    return 1;
  case FL_PUSH:
  case FL_DRAG:
    gui_.emulatorWindow->take_focus();
  case FL_RELEASE:
    gui_.sendMouseEvent(true, false);
    return 1;
  case FL_KEYUP:
  case FL_KEYDOWN:
    {
      int   keyCode = Fl::event_key();
#ifdef WIN32
      keyCode = ep128emuWindowsRecoverFirstAltKey(event, keyCode);
#endif
      bool  isKeyPress = (event == FL_KEYDOWN);

      if (gui_.handleApplicationShortcut(keyCode, isKeyPress))
        return 1;

      // Configurable application shortcuts are dispatched above before any
      // event can reach the emulated keyboard. The remaining code handles
      // legacy fixed shortcuts that are not configurable yet.

      bool  altShortcutPress = false;
      if (Fl::event_alt() && keyCode >= 0x31 && keyCode <= 0x36) {
        // Alt+1..Alt+6 select the six fixed speed presets in menu order.
        // Handle these directly so they can never leak digits into the VM.
        if (isKeyPress) {
          switch (keyCode) {
          case 0x31:
            gui_.menuCallback_Machine_Speed_10((Fl_Widget *) 0, userData);
            break;
          case 0x32:
            gui_.menuCallback_Machine_Speed_25((Fl_Widget *) 0, userData);
            break;
          case 0x33:
            gui_.menuCallback_Machine_Speed_50((Fl_Widget *) 0, userData);
            break;
          case 0x34:
            gui_.menuCallback_Machine_Speed_100((Fl_Widget *) 0, userData);
            break;
          case 0x35:
            gui_.menuCallback_Machine_Speed_200((Fl_Widget *) 0, userData);
            break;
          case 0x36:
            gui_.menuCallback_Machine_Speed_400((Fl_Widget *) 0, userData);
            break;
          }
        }
        return 1;
      }
      if (Fl::event_alt() && (keyCode == 0x61 || keyCode == 0x76)) {
        // Dedicated recording shortcuts.  Shift selects Stop so Start/Stop
        // stay on the same mnemonic key: A = audio, V = video.
        if (isKeyPress) {
          if (keyCode == 0x61) {
            if (Fl::event_shift())
              gui_.menuCallback_File_StopSndRecord((Fl_Widget *) 0, userData);
            else
              gui_.menuCallback_File_RecordSound((Fl_Widget *) 0, userData);
          }
          else {
            if (Fl::event_shift())
              gui_.menuCallback_File_StopAVIRecord((Fl_Widget *) 0, userData);
            else
              gui_.menuCallback_File_RecordVideo((Fl_Widget *) 0, userData);
          }
        }
        return 1;
      }

      if (isKeyPress && Fl::event_alt()) {
        if (keyCode == 0x2C || keyCode == 0x2E) {
          altShortcutPress = true;
        }
        else if (keyCode >= 0x61 && keyCode <= 0x77) {
          static const unsigned char altKeyMap_[0x17] = {
             0, 24, 25, 26,  0, 28,  0,  0, 30,  0, 31, 32,
             0,  0, 33, 34, 35, 36, 37, 38, 39,  0,  0
          };
          altShortcutPress = bool(altKeyMap_[keyCode - 0x61]);
        }
      }

      bool legacyShiftFunctionShortcut =
          (Fl::event_shift() &&
           keyCode >= (FL_F + 9) && keyCode <= (FL_F + 12));
      if (!altShortcutPress && !legacyShiftFunctionShortcut) {
        int   n = gui_.config.convertKeyCode(keyCode);
        if (n >= 0 && (gui_.functionKeyState == 0U || !isKeyPress)) {
          try {
#ifdef WIN32
            if (keyCode == FL_Shift_L || keyCode == FL_Shift_R) {
              // work around FLTK bug
              int   tmp = gui_.config.convertKeyCode(FL_Shift_L);
              if (tmp >= 0)
                gui_.vmThread.setKeyboardState(
                    uint8_t(tmp), (GetKeyState(VK_LSHIFT) < 0));
              tmp = gui_.config.convertKeyCode(FL_Shift_R);
              if (tmp >= 0)
                gui_.vmThread.setKeyboardState(
                    uint8_t(tmp), (GetKeyState(VK_RSHIFT) < 0));
            }
            else
#endif
            {
              gui_.vmThread.setKeyboardState(uint8_t(n), isKeyPress);
            }
          }
          catch (std::exception& e) {
            gui_.errorMessage(e.what());
          }
          if (gui_.functionKeyState == 0U || isKeyPress)
            return 1;
        }
      }
      int   n = -1;
      switch (keyCode) {
      case (FL_F + 9):
      case (FL_F + 10):
      case (FL_F + 11):
      case (FL_F + 12):
        if (Fl::event_shift())
          n = 4 + keyCode - (FL_F + 9);
        break;
      case FL_Page_Down:
        n = 12;
        break;
      case FL_Page_Up:
        n = 13;
        break;
      case FL_Alt_L:
        n = 14;
        break;
      case FL_Alt_R:
        n = 15;
        break;
      case (FL_KP + '0'):
      case (FL_KP + '1'):
      case (FL_KP + '3'):
      case (FL_KP + '4'):
        n = 16 + (keyCode - (FL_KP + '0'));
        break;
      case 0x2C:                // ','
        n = 20;
        break;
      case 0x2E:                // '.'
        n = 21;
        break;
      default:                  // 'A' to 'W'
        if (keyCode >= 0x61 && keyCode <= 0x77) {
          // E/H/W are configurable; G/J/M/N/V have separate or no mapping
          static const unsigned char altKeyMap_[0x17] = {
             0, 24, 25, 26,  0, 28,  0,  0, 30,  0, 31, 32,
             0,  0, 33, 34, 35, 36, 37, 38, 39,  0,  0
          };
          n = int(altKeyMap_[keyCode - 0x61]) - 1;
        }
        break;
      }
      if (n >= 20) {
        if (isKeyPress && !Fl::event_alt())
          n = -1;
      }
      if (n >= 0) {
        uint32_t  bitMask = 1U << (unsigned char) (n < 12 ? (n & 3) : (n - 8));
        bool      wasPressed = bool(gui_.functionKeyState & bitMask);
        if (isKeyPress != wasPressed) {
          if (isKeyPress)
            gui_.functionKeyState |= bitMask;
          else
            gui_.functionKeyState &= (bitMask ^ uint32_t(0xFFFFFFFFU));
          if (isKeyPress) {
            switch (n) {
            case 0:                                     // F9:
              gui_.menuCallback_Options_DpyMode((Fl_Widget *) 0, userData);
              break;
            case 1:                                     // F10:
              gui_.menuCallback_Machine_Pause((Fl_Widget *) 0, userData);
              break;
            case 2:                                     // F11:
              gui_.menuCallback_Machine_Reset((Fl_Widget *) 0, userData);
              break;
            case 3:                                     // F12:
            case 24:                                    // Alt+C:
              gui_.menuCallback_File_Screenshot((Fl_Widget *) 0, userData);
              break;
            case 4:                                     // Shift+F9:
              gui_.menuCallback_Options_DpyConfig((Fl_Widget *) 0, userData);
              break;
            case 5:                                     // Shift+F10:
              gui_.menuCallback_Machine_Configure((Fl_Widget *) 0, userData);
              break;
            case 6:                                     // Shift+F11:
              gui_.menuCallback_Machine_ResetAll((Fl_Widget *) 0, userData);
              break;
            case 7:                                     // Shift+F12:
              gui_.menuCallback_File_Quit((Fl_Widget *) 0, userData);
              break;
            case 8:                                     // Ctrl+F9:
              gui_.menuCallback_File_QSSave((Fl_Widget *) 0, userData);
              break;
            case 9:                                     // Ctrl+F10:
              gui_.menuCallback_File_QSLoad((Fl_Widget *) 0, userData);
              break;
            case 10:                                    // Ctrl+F11:
              gui_.menuCallback_Machine_ColdReset((Fl_Widget *) 0, userData);
              break;
            case 12:                                    // PageDown:
              gui_.menuCallback_Machine_QuickCfgL1((Fl_Widget *) 0, userData);
              break;
            case 13:                                    // PageUp:
              gui_.menuCallback_Machine_QuickCfgL2((Fl_Widget *) 0, userData);
              break;
            case 16:                                    // KP_0:
            case 37:                                    // Alt+T:
              gui_.menuCallback_Machine_OpenTape((Fl_Widget *) 0, userData);
              break;
            case 17:                                    // KP_1:
            case 32:                                    // Alt+O:
              gui_.menuCallback_Machine_TapeStop((Fl_Widget *) 0, userData);
              break;
            case 18:                                    // KP_3:
            case 33:                                    // Alt+P:
              gui_.menuCallback_Machine_TapePlay((Fl_Widget *) 0, userData);
              break;
            case 19:                                    // KP_4:
            case 35:                                    // Alt+R:
              gui_.menuCallback_Machine_TapeRewind((Fl_Widget *) 0, userData);
              break;
            case 20:                                    // Alt+,:
              gui_.menuCallback_Machine_TapePrvCP((Fl_Widget *) 0, userData);
              break;
            case 21:                                    // Alt+.:
              gui_.menuCallback_Machine_TapeNxtCP((Fl_Widget *) 0, userData);
              break;
            case 23:                                    // Alt+B:
              gui_.menuCallback_Debug_OpenDebugger((Fl_Widget *) 0, userData);
              break;
            case 25:                                    // Alt+D:
              gui_.menuCallback_Options_FloppyCfg((Fl_Widget *) 0, userData);
              break;
            case 27:                                    // Alt+F:
              gui_.menuCallback_Options_FileIODir((Fl_Widget *) 0, userData);
              break;
            case 29:                                    // Alt+I:
              gui_.menuCallback_Options_KbdConfig((Fl_Widget *) 0, userData);
              break;
            case 30:                                    // Alt+K:
              gui_.menuCallback_File_StopDemo((Fl_Widget *) 0, userData);
              break;
            case 31:                                    // Alt+L:
              gui_.menuCallback_File_LoadFile((Fl_Widget *) 0, userData);
              break;
            case 34:                                    // Alt+Q:
              gui_.menuCallback_File_LoadConfig((Fl_Widget *) 0, userData);
              break;
            case 36:                                    // Alt+S:
              gui_.menuCallback_File_SaveSnapshot((Fl_Widget *) 0, userData);
              break;
            case 38:                                    // Alt+U:
              gui_.menuCallback_Options_SndConfig((Fl_Widget *) 0, userData);
              break;
            }
          }
        }
      }
    }
    return 1;
  case FL_SHORTCUT:
#ifdef WIN32
    // Windows may synthesize FL_SHORTCUT for the first Alt+key while the
    // native menu is activating. Give registered application shortcuts first
    // refusal and reuse the existing (unchanged) FL_KEYDOWN legacy handler
    // only for known Alt hotkeys; never forward arbitrary shortcut events
    // into the emulated machine keyboard.
    if (Fl::event_alt()) {
      const int key = ep128emuWindowsRecoverFirstAltKey(
          event, Fl::event_key());
      if (gui_.handleApplicationShortcut(key, true))
        return 1;
      const bool legacyAlt =
          ((key >= '1' && key <= '6') || key == ',' || key == '.' ||
           key == 'a' || key == 'v' || key == 'b' || key == 'c' ||
           key == 'd' || key == 'f' || key == 'i' || key == 'k' ||
           key == 'l' || key == 'o' || key == 'p' || key == 'q' ||
           key == 'r' || key == 's' || key == 't' || key == 'u');
      if (legacyAlt) {
        // A native Windows menu may send FL_SHORTCUT but consume FL_KEYUP.
        // The old synthetic FL_KEYDOWN left functionKeyState latched so the
        // next Alt+T/Alt+U was ignored. Clear only this synthesized host bit
        // before and after dispatch. Never synthesize guest keyboard input.
        const uint32_t syntheticMask =
            ep128emuSyntheticAltShortcutMask(key);
        gui_.functionKeyState &= ~syntheticMask;
        const int handled = handleFLTKEvent(userData, FL_KEYDOWN);
        gui_.functionKeyState &= ~syntheticMask;
        return handled;
      }
    }
#endif
    // On macOS, the menu bar handles Option+key and registered shortcuts;
    // on Windows it remains the fallback for unhandled native menu events.
    (void) gui_.mainMenuBar->test_shortcut();
    return 1;
  }
  return 0;
}

bool Ep128EmuGUI::lockVMThread(size_t t)
{
#ifdef WIN32
  // TASMON/FileIO F1 suspends the VM inside the guest-owned native file
  // chooser. Windows common dialogs pump messages (including FLTK checks).
  // A nested UI VM-lock attempt cannot be acknowledged until the guest
  // chooser returns, so do not block for two seconds or create a second
  // modal error window while the expected native chooser owns the UI.
  // Only the UI thread is excluded; the VM worker itself is unchanged.
  if (guestFileIOPickerActive.load(std::memory_order_acquire) &&
      uintptr_t(GetCurrentThreadId()) == mainThreadID) {
    ep128emuWindowsGuiTrace("VMLOCK-SKIPPED-GUEST-PICKER", -1, 0, 0);
    return false;
  }
#endif
  const int result = vmThread.lock(t);
  // -1 means the VM has already been instructed to exit (normal on a
  // machine-type restart). This is NOT a responsiveness timeout; showing a
  // modal error here can recursively schedule more GUI polling and dialogs.
  if (result < 0)
    return false;
  if (result > 0) {
#ifdef WIN32
    ep128emuWindowsGuiTrace(
        guestFileIOPickerActive.load(std::memory_order_acquire)
            ? "VMLOCK-TIMEOUT-GUEST-PICKER" : "VMLOCK-TIMEOUT-OTHER",
        -1, 0, 0);
#endif
    errorMessage("cannot lock virtual machine: "
                 "the emulation thread does not respond");
    return false;
  }
  return true;
}

void Ep128EmuGUI::unlockVMThread()
{
  vmThread.unlock();
}

void Ep128EmuGUI::addRecentFile(std::string *list,
                                  const std::string& fileName)
{
  if (!list || fileName.empty())
    return;
  int oldPos = -1;
  for (int i = 0; i < 10; i++) {
    if (list[i] == fileName) {
      oldPos = i;
      break;
    }
  }
  int last = (oldPos >= 0 ? oldPos : 9);
  for (int i = last; i > 0; i--)
    list[i] = list[i - 1];
  list[0] = fileName;
  refreshRecentMenus();
}

void Ep128EmuGUI::removeRecentFile(std::string *list, int n)
{
  if (!list || n < 0 || n >= 10)
    return;
  for (int i = n; i < 9; i++)
    list[i] = list[i + 1];
  list[9].clear();
  refreshRecentMenus();
}

void Ep128EmuGUI::refreshRecentMenus()
{
  struct RecentCategory {
    const char *path;
    std::string *list;
    Fl_Callback *callback;
  };
  RecentCategory cats[4] = {
    { "File/Recent/Disk images", recentDiskImages, &menuCallback_RecentDisk },
    { "File/Recent/Tape images", recentTapeImages, &menuCallback_RecentTape },
    { "File/Recent/Snapshots", recentSnapshots, &menuCallback_RecentSnapshot },
    { "File/Recent/Configurations", recentConfigurations,
      &menuCallback_RecentConfig }
  };
  for (int c = 0; c < 4; c++) {
    int menuIndex = mainMenuBar->find_index(cats[c].path);
    if (menuIndex >= 0)
      mainMenuBar->clear_submenu(menuIndex);
    bool haveItem = false;
    for (int i = 0; i < 10; i++) {
      if (cats[c].list[i].empty())
        continue;
      std::string dirName;
      std::string baseName;
      Ep128Emu::splitPath(cats[c].list[i], dirName, baseName);
      if (baseName.empty())
        baseName = cats[c].list[i];
      char prefix[16];
      std::sprintf(prefix, "%d. ", i + 1);
      std::string menuPath = std::string(cats[c].path) + "/" +
                             prefix + baseName;
      mainMenuBar->add(menuPath.c_str(), (char *) 0, cats[c].callback,
                       (void *) this);
      haveItem = true;
    }
    if (!haveItem) {
      std::string menuPath = std::string(cats[c].path) + "/(empty)";
      mainMenuBar->add(menuPath.c_str(), (char *) 0, (Fl_Callback *) 0,
                       (void *) this, FL_MENU_INACTIVE);
    }
  }
}

void Ep128EmuGUI::loadSnapshotFile(const std::string& fileName,
                                    bool addRecent)
{
  Ep128Emu::File f(fileName.c_str());
  int fileMachineType = f.getMachineType();
  if (fileMachineType < 0)
    throw Ep128Emu::Exception(
        "file does not contain a supported machine snapshot");
  if (!prepareMachineTypedLoad(fileMachineType, fileName, 1, addRecent))
    return;

  bool runtimeMemoryState = ep128emuFileHasRuntimeMemoryState(f);
  if (lockVMThread()) {
    try {
      vm.registerChunkTypes(f);
      config.registerChunkType(f);
      f.processAllChunks();
      if (runtimeMemoryState)
        setSnapshotRuntimeMemoryActive(true);
      vmThread.clearRewindHistory();
      applyEmulatorConfiguration(true);
    }
    catch (...) {
      unlockVMThread();
      throw;
    }
    unlockVMThread();
  }
  std::string dirName;
  std::string baseName;
  Ep128Emu::splitPath(fileName, dirName, baseName);
  if (!dirName.empty()) {
    snapshotDirectory = dirName;
    loadFileDirectory = dirName;
  }
  if (addRecent)
    addRecentFile(recentSnapshots, fileName);
}

void Ep128EmuGUI::loadConfigFile(const std::string& fileName, bool addRecent)
{
  int fileMachineType =
      Ep128Emu::ConfigurationDB::getMachineTypeMetadata(fileName.c_str());
  if (!prepareMachineTypedLoad(fileMachineType, fileName, 2, addRecent))
    return;
  config.loadState(fileName.c_str());
  // A full explicit configuration load becomes authoritative even when all
  // persisted RAM/ROM values happen to equal the retained base configuration.
  // This is required after a snapshot has replaced the live memory image.
  if (snapshotRuntimeMemoryActive)
    config.memoryConfigurationChanged = true;
  finishArchiveFileIOOverride(true, true);
  // loadState() only updates ConfigurationDB, not the active machine.
  // This matters on all hosts, including the frozen macOS source.
  applyEmulatorConfiguration(true);
  std::string dirName;
  std::string baseName;
  Ep128Emu::splitPath(fileName, dirName, baseName);
  if (!dirName.empty())
    configDirectory = dirName;
  if (addRecent)
    addRecentFile(recentConfigurations, fileName);
}

void Ep128EmuGUI::openTapeImageFile(const std::string& fileName,
                                    bool addRecent)
{
  menuCallback_Machine_TapeStop((Fl_Widget *) 0, (void *) this);
  config["tape.imageFile"] = fileName;
  applyEmulatorConfiguration();
  std::string dirName;
  std::string baseName;
  Ep128Emu::splitPath(fileName, dirName, baseName);
  if (!dirName.empty())
    tapeImageDirectory = dirName;
  if (addRecent)
    addRecentFile(recentTapeImages, fileName);
}

void Ep128EmuGUI::insertFloppyAImageFile(const std::string& fileName,
                                         bool addRecent)
{
  if (typeid(vm) == typeid(ZX128::ZX128VM))
    throw Ep128Emu::Exception("the current machine has no floppy drive");
  if (ep128emuIsCPCDiskImage(fileName) &&
      typeid(vm) != typeid(CPC464::CPC464VM)) {
    throw Ep128Emu::Exception(
        "CPC DSK image cannot be mounted in the current machine mode");
  }
  config["floppy.a.imageFile"] = fileName;
  applyEmulatorConfiguration(true);
  std::string dirName;
  std::string baseName;
  Ep128Emu::splitPath(fileName, dirName, baseName);
  if (!dirName.empty())
    diskImageDirectory = dirName;
  if (addRecent)
    addRecentFile(recentDiskImages, fileName);
}


bool Ep128EmuGUI::chooseArchiveProgram(
    const std::string& directory,
    const std::vector<std::string>& programs,
    std::string& selectedFile)
{
  selectedFile.clear();
  if (programs.empty())
    return false;
  if (programs.size() == 1) {
    selectedFile = programs[0];
#ifdef __APPLE__
    // No selector is needed, so simply return focus to the emulator and let
    // the drag source remain open in the background.
    ep128emuActivateMacOSWindow(mainWindow);
#endif
    return true;
  }

  // Do not expose the extracted directory tree as a filesystem browser.
  // EPFileIO deliberately uses one flat working directory, so present only
  // runnable candidates and show their archive-relative paths to distinguish
  // variants that use the same COM/TRN basename in different directories.
  Fl_Window dialog(640, 360, "Select Enterprise program to run");
  Fl_Hold_Browser browser(20, 35, 600, 255,
                          "Programs found in the archive");
  browser.align(FL_ALIGN_TOP_LEFT);
  browser.format_char(0);
  for (size_t i = 0; i < programs.size(); i++) {
    std::string label =
        Ep128EmuArchiveDrop::relativeProgramPath(directory, programs[i]);
    browser.add(label.c_str());
  }
  browser.value(1);

  Fl_Button runButton(435, 310, 85, 30, "Run");
  Fl_Button cancelButton(535, 310, 85, 30, "Cancel");
  Ep128EmuArchiveProgramDialogState state;
  state.window = &dialog;
  state.browser = &browser;
  state.selectedLine = 0;
  runButton.callback(&ep128emuArchiveProgramRunCallback, (void *) &state);
  cancelButton.callback(&ep128emuArchiveProgramCancelCallback, (void *) &state);
  dialog.callback(&ep128emuArchiveProgramCancelCallback, (void *) &state);
  dialog.set_modal();
  dialog.end();
  if (mainWindow) {
    int x = mainWindow->x() + ((mainWindow->w() - dialog.w()) >> 1);
    int y = mainWindow->y() + ((mainWindow->h() - dialog.h()) >> 1);
    dialog.position(x, y);
  }
#ifdef __APPLE__
  const bool dragSourceHidden =
      ep128emuHideRememberedMacOSDragSourceApplication();
#endif
  dialog.show();
  while (dialog.shown())
    Fl::wait();
#ifdef __APPLE__
  if (dragSourceHidden)
    ep128emuUnhideRememberedMacOSDragSourceApplication();
#endif

  if (state.selectedLine < 1 ||
      size_t(state.selectedLine) > programs.size())
    return false;
  selectedFile = programs[size_t(state.selectedLine - 1)];
  return true;
}

#ifdef WIN32
// Windows installs keep their built-in ROM files beside the executable in
// roms/. The persisted GUI ROM chooser directory may be "." or a user's
// unrelated directory; it must not disable the standard 128K preflight.
static bool ep128emuArchiveROMsPresent(const std::string& folder)
{
  return ep128emuReadableFile(ep128emuJoinPath(folder,"exos21.rom"))
      && ep128emuReadableFile(ep128emuJoinPath(folder,"basic21.rom"))
      && ep128emuReadableFile(ep128emuJoinPath(folder,"epfileio.rom"));
}

static std::string ep128emuArchiveFindStandardROMDirectory(
    const std::string& preferred)
{
  if(ep128emuArchiveROMsPresent(preferred))return preferred;
  std::string userSubdir=ep128emuJoinPath(preferred,"roms");
  if(ep128emuArchiveROMsPresent(userSubdir))return userSubdir;
  wchar_t exeName[32768]={0};
  DWORD n=GetModuleFileNameW(NULL,exeName,32768);
  if(n>0 && n<32768) {
    std::string exePath;
    Ep128Emu::convertToUTF8(exePath,exeName);
    size_t slash=exePath.find_last_of("/\\\\");
    if(slash!=std::string::npos) {
      std::string candidate=ep128emuJoinPath(
          exePath.substr(0,slash),"roms");
      if(ep128emuArchiveROMsPresent(candidate))return candidate;
    }
  }
  return preferred;
}
#endif

bool Ep128EmuGUI::archiveCurrentSetupSupportsAutoRun() const
{
  // The keyboard-driven Drag & Run loader is qualified only for the classic
  // EXOS 2.1 startup path with an IS-BASIC 2.0/2.1 ROM present somewhere in
  // the configured ROM set. Newer ZozoTools/EXOS 2.4 startup environments
  // use a different boot/program-selection flow and cannot safely share the
  // T081 BASIC-style LOAD injection.
  if (!config.memory.configFile.empty())
    return false;

  std::string exosName =
      Ep128EmuArchiveDrop::baseName(config.memory.rom[0x00].file);
  Ep128Emu::stringToLowerCase(exosName);
  if (exosName != "exos21.rom")
    return false;

  for (size_t i = 0; i < 68; i++) {
    if (config.memory.rom[i].file.empty())
      continue;
    std::string romName =
        Ep128EmuArchiveDrop::baseName(config.memory.rom[i].file);
    Ep128Emu::stringToLowerCase(romName);
    if (romName == "basic20.rom" || romName == "basic21.rom")
      return true;
  }
  return false;
}

bool Ep128EmuGUI::archiveStandard128KAvailable() const
{
#ifdef WIN32
  // Preserve the W05 installer/Program Files ROM directory resolver.
  return ep128emuArchiveROMsPresent(
      ep128emuArchiveFindStandardROMDirectory(romImageDirectory));
#else
  // EF08N macOS uses the complete same-directory source ROM set resolver.
  Ep128EmuArchiveStandard128::ROMSet selected;
  return ep128emuFindArchiveStandard128KROMs(romImageDirectory,config,selected);
#endif
}

bool Ep128EmuGUI::chooseArchiveRunConfiguration(
    const std::string& archiveDirectory,
    const std::string& programFile,
    bool& useStandard128K)
{
  useStandard128K = false;

  Ep128EmuArchiveDrop::ProgramInspection info;
  std::string inspectionError;
  (void) Ep128EmuArchiveDrop::inspectProgram(
      programFile, info, inspectionError);

  std::string relativePath =
      Ep128EmuArchiveDrop::relativeProgramPath(archiveDirectory, programFile);
  std::string programLabel("Program: ");
  programLabel += relativePath;

  const bool currentAvailable = archiveCurrentSetupSupportsAutoRun();
  const bool standardAvailable = archiveStandard128KAvailable();
  const bool unusualProgram =
      (!inspectionError.empty() || !info.type5Header ||
       !info.payloadSizeMatches);

  Fl_Window dialog(560, 260, "Run Enterprise program");

  Fl_Box title(25, 20, 510, 28, "How should this program start?");
  title.labelfont(FL_BOLD);
  title.labelsize(17);
  title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  Fl_Box programBox(25, 55, 510, 24, programLabel.c_str());
  programBox.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  programBox.labelsize(12);

  Fl_Button currentButton(25, 95, 245, 52, "Keep Current Setup");
  currentButton.labelfont(FL_BOLD);
  currentButton.labelsize(14);
  if (!currentAvailable)
    currentButton.deactivate();

  Fl_Button standardButton(290, 95, 245, 52, "Use Standard 128K");
  standardButton.labelfont(FL_BOLD);
  standardButton.labelsize(14);
  if (!standardAvailable)
    standardButton.deactivate();

  Fl_Box noteBox(25, 158, 510, 22,
                 "Your saved settings will not be changed.");
  noteBox.align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
  noteBox.labelsize(12);

  Fl_Box statusBox(25, 184, 510, 24, "");
  if (!currentAvailable && standardAvailable) {
    statusBox.label("Current setup cannot auto-run this program.");
    statusBox.align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    statusBox.labelsize(11);
  }
  else if (!currentAvailable && !standardAvailable) {
    statusBox.label("No compatible auto-run setup is available.");
    statusBox.align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    statusBox.labelsize(11);
  }
  else if (unusualProgram) {
    statusBox.label("This file may not be a standard Enterprise application.");
    statusBox.align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    statusBox.labelsize(11);
  }
  else if (!standardAvailable) {
    statusBox.label("Standard 128K is not available in this installation.");
    statusBox.align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    statusBox.labelsize(11);
  }

  Fl_Button cancelButton(445, 218, 90, 28, "Cancel");

  Ep128EmuArchiveRunDialogState state;
  state.window = &dialog;
  state.choice = 0;
  currentButton.callback(&ep128emuArchiveRunCurrentCallback, (void *) &state);
  standardButton.callback(&ep128emuArchiveRunStandardCallback, (void *) &state);
  cancelButton.callback(&ep128emuArchiveRunCancelCallback, (void *) &state);
  dialog.callback(&ep128emuArchiveRunCancelCallback, (void *) &state);
  dialog.set_modal();
  dialog.end();

  if (mainWindow) {
    int x = mainWindow->x() + ((mainWindow->w() - dialog.w()) >> 1);
    int y = mainWindow->y() + ((mainWindow->h() - dialog.h()) >> 1);
    dialog.position(x, y);
  }

#ifdef __APPLE__
  const bool dragSourceHidden =
      ep128emuHideRememberedMacOSDragSourceApplication();
#endif
  dialog.show();
  while (dialog.shown())
    Fl::wait();
#ifdef __APPLE__
  if (dragSourceHidden)
    ep128emuUnhideRememberedMacOSDragSourceApplication();
#endif

  if (state.choice == 2) {
    useStandard128K = true;
    return true;
  }
  if (state.choice == 1)
    return true;
  return false;
}

void Ep128EmuGUI::applyArchiveStandard128KConfiguration()
{
#ifdef WIN32
  const std::string resolvedROMDirectory =
      ep128emuArchiveFindStandardROMDirectory(romImageDirectory);
  const std::string exosPath =
      ep128emuJoinPath(resolvedROMDirectory, "exos21.rom");
  const std::string basicPath =
      ep128emuJoinPath(resolvedROMDirectory, "basic21.rom");
  const std::string fileIOPath =
      ep128emuJoinPath(resolvedROMDirectory, "epfileio.rom");
  if (!(ep128emuReadableFile(exosPath) &&
        ep128emuReadableFile(basicPath) &&
        ep128emuReadableFile(fileIOPath)))
#else
  Ep128EmuArchiveStandard128::ROMSet selected;
  if (!ep128emuFindArchiveStandard128KROMs(
          romImageDirectory,config,selected))
#endif
  {
    throw Ep128Emu::Exception(
        "Standard 128K is not available because the complete EXOS 2.1, "
        "BASIC 2.1 and EPFileIO ROM set could not be found.");
  }
#ifndef WIN32
  const std::string& exosPath=selected.exos;
  const std::string& basicPath=selected.basic;
  const std::string& fileIOPath=selected.fileIO;
#endif

  const bool firstActivation = !archiveTemporaryConfigActive;
  if (firstActivation)
    config.saveState(archiveOriginalConfigState);

  try {
    config.vm.cpuClockFrequency = 4000000U;
    config.vm.videoClockFrequency = 889846U;
    config.vm.soundClockFrequency = 500000U;
    config.vm.enableMemoryTimingEmulation = true;
    config.vm.enableFileIO = true;

    config.memory.ram.size = 128;
    config.memory.configFile.clear();
    for (size_t i = 0; i < 68; i++) {
      config.memory.rom[i].file.clear();
      config.memory.rom[i].offset = 0;
    }
    config.memory.rom[0x00].file = exosPath;
    config.memory.rom[0x00].offset = 0;
    config.memory.rom[0x01].file = exosPath;
    config.memory.rom[0x01].offset = 0x4000;
    config.memory.rom[0x04].file = basicPath;
    config.memory.rom[0x04].offset = 0;
    config.memory.rom[0x10].file = fileIOPath;
    config.memory.rom[0x10].offset = 0;
    config.sdext.enabled = false;
    config.sdext.romFile.clear();

    config.vmConfigurationChanged = true;
    config.memoryConfigurationChanged = true;
    applyEmulatorConfiguration(true);
    archiveTemporaryConfigActive = true;
  }
  catch (...) {
    if (firstActivation) {
      try {
        config.loadState(archiveOriginalConfigState);
        applyEmulatorConfiguration(true);
      }
      catch (...) {
      }
      archiveOriginalConfigState.clear();
      archiveTemporaryConfigActive = false;
    }
    throw;
  }
}

void Ep128EmuGUI::restoreArchivePersistentConfiguration()
{
  if (!archiveTemporaryConfigActive)
    return;
  try {
    config.loadState(archiveOriginalConfigState);
  }
  catch (...) {
  }
  archiveOriginalConfigState.clear();
  archiveTemporaryConfigActive = false;
}

void Ep128EmuGUI::rollbackArchiveTemporaryConfiguration()
{
  if (!archiveTemporaryConfigActive)
    return;
  config.loadState(archiveOriginalConfigState);
  applyEmulatorConfiguration(true);
  archiveOriginalConfigState.clear();
  archiveTemporaryConfigActive = false;
}

void Ep128EmuGUI::finishArchiveFileIOOverride(bool updateWindowFlag_,
                                              bool replaceConfiguration_)
{
  // Normal configuration transitions must not perturb the VMThread message
  // queue.  Archive Drag & Run is the only product path that installs this
  // runtime-only FILE: override, so there is nothing to finish if no archive
  // lifecycle state is active.
  const bool autoRunActive =
      (archiveAutoRunPhase >= 0 || archiveAutoRunKeyCode >= 0 ||
       archiveAutoRunShift);
  const bool archiveStateActive =
      (autoRunActive || archiveAutoRunSpeedOverrideActive ||
       archiveTemporaryConfigActive || !archiveTemporaryDirectory.empty());
  if (!archiveStateActive) {
    // An ordinary ASCII/binary/quick configuration load must publish the
    // requested RAM, ROM and VM settings to the running machine too.  This
    // helper is shared by those paths; without an archive override it used
    // to return before applying anything (EF08W regression).
    applyEmulatorConfiguration(updateWindowFlag_);
    return;
  }

  // An explicit configuration authority transition ends Archive Drag & Run's
  // runtime-only FILE: override. Stop any still-pending auto-run sequence
  // before publishing the configured state back to the VM.
  Fl::remove_timeout(&archiveAutoRunCallback, (void *) this);
  archiveAutoRunPhase = -1;
  archiveAutoRunPosition = 0;
  archiveAutoRunKeyCode = -1;
  archiveAutoRunShift = false;

  restoreArchiveAutoRunSpeed();

  // The runtime override deliberately does not mirror these values into
  // ConfigurationDB. Force both values back into the VM even when the
  // explicit configuration contains strings/booleans equal to those already
  // stored in the GUI configuration object.
  config.vmConfigurationChanged = true;
  config.fileioSettingsChanged = true;

  if (autoRunActive) {
    // resetKeyboard() is asynchronous. Queueing it and then immediately
    // locking the VM is a race on macOS. Keep one outer VM lock across the
    // interrupted auto-run keyboard cleanup and configuration publication;
    // applyEmulatorConfiguration() then uses its supported nested lock path.
    if (!lockVMThread())
      throw Ep128Emu::Exception(
          "cannot lock virtual machine while ending archive auto-run");
    try {
      vmThread.resetKeyboardLocked();
      applyEmulatorConfiguration(updateWindowFlag_);
    }
    catch (...) {
      unlockVMThread();
      throw;
    }
    unlockVMThread();
  }
  else {
    applyEmulatorConfiguration(updateWindowFlag_);
  }

  // A complete explicit configuration replacement supersedes the session-only
  // Standard 128K persistence guard.  Do this only after the new configuration
  // has been applied successfully.
  if (replaceConfiguration_ && archiveTemporaryConfigActive) {
    archiveOriginalConfigState.clear();
    archiveTemporaryConfigActive = false;
  }

  // At this point the VM no longer references the extracted archive directory,
  // so it is safe to remove it.
  if (!archiveTemporaryDirectory.empty()) {
    Ep128EmuArchiveDrop::removeTemporaryDirectory(archiveTemporaryDirectory);
    archiveTemporaryDirectory.clear();
  }
}

void Ep128EmuGUI::archiveAutoRunCallback(void *userData)
{
  Ep128EmuGUI *gui_ = reinterpret_cast<Ep128EmuGUI *>(userData);
  if (gui_)
    gui_->archiveAutoRunStep();
}

void Ep128EmuGUI::restoreArchiveAutoRunSpeed()
{
  if (!archiveAutoRunSpeedOverrideActive)
    return;

  archiveAutoRunSpeedOverrideActive = false;
  // Restore the Windows VM audio gate before normal speed pacing, and
  // restore the physical tape inertia only after ending the temporary
  // unlimited-speed Archive Drag & Run override. Neither is persisted.
#ifdef WIN32
  vmThread.setAudioOutputEnabled(config.sound.enabled &&
                                 config.vm.speedPercentage == 100U);
#endif
  // Archive Drag & Run changes only runtime pacing, never persistent user
  // configuration. Also restore the matching host tape inertia setting.
  vmThread.setSpeedPercentage(int(archiveAutoRunPreviousSpeedPercentage));
  if (typeid(vm) == typeid(Ep128::Ep128VM) && lockVMThread()) {
    Ep128::Ep128VM& ep = static_cast<Ep128::Ep128VM&>(vm);
    ep.setTapeMotorSpinUpDelayMs(getEffectiveTapeMotorSpinUpDelayMs());
    // 100 ms acoustic gate: real manual REM gap measured 29-32 ms, while
    // actual long motor pauses exceed 800 ms. Never delay raw motor OFF.
    // Sound-only debounce is independent of machine speed. The GUI already
    // suppresses relay playback when not at 100%; bypassing this filter at
    // other speeds could leave a stale zero-delay setting at the first header.
    ep.setTapeRelayReleaseDelayMs(100U);
    unlockVMThread();
  }
}

void Ep128EmuGUI::archiveAutoRunStep()
{
  try {
    if (archiveAutoRunPhase < 0)
      return;

    // Full-speed execution is deliberately used during boot. Wait on guest
    // video-frame progress rather than host wall time, so expanded-memory and
    // extension-ROM initialization cannot make the LOAD injection arrive
    // early. The frame counter increments at every emulated frame boundary,
    // even when host rendering skips frames.
    if (archiveAutoRunPhase == 0) {
      const uint64_t nFrames =
          display.getFrameSequenceNumber() - archiveAutoRunBootFrameStart;
      if (nFrames < 1000ULL) {          // 20 s at 50 Hz
        Fl::add_timeout(0.020, &archiveAutoRunCallback, (void *) this);
        return;
      }

      // Classic EXOS 2.1 startup: SPACE continues into the installed BASIC.
      vmThread.setKeyboardState(0x46, true);
      archiveAutoRunKeyCode = 0x46;
      archiveAutoRunShift = false;
      archiveAutoRunPostStartupFrame = display.getFrameSequenceNumber();
      archiveAutoRunPhase = 1;
      Fl::add_timeout(0.010, &archiveAutoRunCallback, (void *) this);
      return;
    }

    if (archiveAutoRunPhase == 1) {
      vmThread.setKeyboardState(archiveAutoRunKeyCode, false);
      archiveAutoRunKeyCode = -1;
      archiveAutoRunPhase = 2;
      Fl::add_timeout(0.020, &archiveAutoRunCallback, (void *) this);
      return;
    }

    if (archiveAutoRunPhase == 2) {
      const uint64_t nFrames =
          display.getFrameSequenceNumber() - archiveAutoRunPostStartupFrame;
      if (nFrames < 250ULL) {           // 5 s after leaving startup
        Fl::add_timeout(0.020, &archiveAutoRunCallback, (void *) this);
        return;
      }
      // The accepted macOS T081I/L path keeps Full Speed through LOAD.
      // Windows retains W01D's guest-frame key-down/release gates instead
      // of throttling keyboard injection to a hard-coded 400% ceiling.
      archiveAutoRunPhase = 3;
      Fl::add_timeout(0.001, &archiveAutoRunCallback, (void *) this);
      return;
    }

    if (archiveAutoRunPhase == 3) {
      if (archiveAutoRunPosition >= archiveAutoRunText.size()) {
        archiveAutoRunPhase = -1;
        emulatorWindow->take_focus();
        return;
      }
      Ep128EmuEnterpriseKey k;
      char c = archiveAutoRunText[archiveAutoRunPosition];
      if (!ep128emuEnterpriseKeyForChar(c, k))
        throw Ep128Emu::Exception("Archive auto-run contains an unsupported "
                                  "Enterprise keyboard character.");
      archiveAutoRunKeyCode = k.code;
      archiveAutoRunShift = k.shift;
      if (archiveAutoRunShift)
        vmThread.setKeyboardState(7, true);
      vmThread.setKeyboardState(archiveAutoRunKeyCode, true);
#ifdef WIN32
      // Matrix events are queued into VMThread. Require actual guest video
      // progress rather than assuming that a 10-ms Windows GUI timeout
      // corresponds to enough simulated time for IS-BASIC to scan the key.
      archiveAutoRunKeyFrameStart=display.getFrameSequenceNumber();
#endif
      archiveAutoRunPhase = 4;
      Fl::add_timeout(0.010, &archiveAutoRunCallback, (void *) this);
      return;
    }

    if (archiveAutoRunPhase == 4) {
#ifdef WIN32
      if(!Ep128EmuArchiveAutoRun::keyPhaseElapsed(
            display.getFrameSequenceNumber(),archiveAutoRunKeyFrameStart)) {
        Fl::add_timeout(0.005, &archiveAutoRunCallback, (void *) this);
        return;
      }
#endif
      vmThread.setKeyboardState(archiveAutoRunKeyCode, false);
      if (archiveAutoRunShift)
        vmThread.setKeyboardState(7, false);
      archiveAutoRunKeyCode = -1;
      archiveAutoRunShift = false;

#ifdef WIN32
      archiveAutoRunKeyFrameStart=display.getFrameSequenceNumber();
      archiveAutoRunPhase=5; // require key release to reach the VM also
      Fl::add_timeout(0.005, &archiveAutoRunCallback, (void *) this);
      return;
    }

    if(archiveAutoRunPhase==5) {
      if(!Ep128EmuArchiveAutoRun::keyPhaseElapsed(
            display.getFrameSequenceNumber(),archiveAutoRunKeyFrameStart)) {
        Fl::add_timeout(0.005, &archiveAutoRunCallback, (void *) this);
        return;
      }
      char c=archiveAutoRunText[archiveAutoRunPosition++];
      archiveAutoRunPhase=3;
      if(c=='\r' || c=='\n')
        restoreArchiveAutoRunSpeed();
      Fl::add_timeout(0.001, &archiveAutoRunCallback, (void *) this);
#else
      char c = archiveAutoRunText[archiveAutoRunPosition++];
      archiveAutoRunPhase = 3;
      const bool commandEnd = (c == '\r' || c == '\n');
      if (commandEnd)
        restoreArchiveAutoRunSpeed();
      double delay = commandEnd ? 0.020 : 0.010;
      Fl::add_timeout(delay, &archiveAutoRunCallback, (void *) this);
#endif
      return;
    }
  }
  catch (std::exception& e) {
    archiveAutoRunPhase = -1;
    try {
      restoreArchiveAutoRunSpeed();
    }
    catch (...) {
    }
    try {
      vmThread.resetKeyboard();
    }
    catch (...) {
    }
    errorMessage(e.what());
  }
}

void Ep128EmuGUI::startArchiveAutoRun(const std::string& programFile)
{
  std::string name = Ep128EmuArchiveDrop::baseName(programFile);
  archiveAutoRunText = "LOAD \"FILE:" + name + "\"\r";
  archiveAutoRunPosition = 0;
  archiveAutoRunPhase = 0;
  archiveAutoRunKeyCode = -1;
  archiveAutoRunShift = false;
  archiveAutoRunBootFrameStart = display.getFrameSequenceNumber();
  archiveAutoRunPostStartupFrame = archiveAutoRunBootFrameStart;
  archiveAutoRunKeyFrameStart = archiveAutoRunBootFrameStart;

  for (size_t i = 0; i < archiveAutoRunText.size(); i++) {
    Ep128EmuEnterpriseKey k;
    if (!ep128emuEnterpriseKeyForChar(archiveAutoRunText[i], k)) {
      archiveAutoRunPhase = -1;
      throw Ep128Emu::Exception("Archive auto-run command contains an "
                                "unsupported Enterprise keyboard character.");
    }
  }

  Fl::remove_timeout(&archiveAutoRunCallback, (void *) this);
  restoreArchiveAutoRunSpeed();

  archiveAutoRunPreviousSpeedPercentage = config.vm.speedPercentage;

  // Queue all File I/O state on the emulation thread itself. The message is
  // FIFO-ordered before the cold reset below, so no GUI-thread VM lock is
  // required even when the runtime is already at Full Speed.
  vmThread.setFileIOState(Ep128EmuArchiveDrop::directoryName(programFile),
                          true, true);

#ifdef WIN32
  // Always restore the original speed, including an unlimited 0% setting:
  // Windows has an explicit key-phase override to restore (even 0%).
  archiveAutoRunSpeedOverrideActive = true;
#else
  archiveAutoRunSpeedOverrideActive =
      (archiveAutoRunPreviousSpeedPercentage != 0U);
#endif
  vmThread.reset(true);
#ifdef WIN32
  // Configured 100% audio uses blocking PortAudio writes (WASAPI/MME).
  // Merely setting VMThread to No limit cannot outrun audio playback.
  // Suppress VM audio *only* during automated boot and keyboard injection;
  // keep the user's persisted sound configuration/device unchanged.
  vmThread.setAudioOutputEnabled(false);
#endif
  vmThread.setSpeedPercentage(0);
  if (typeid(vm) == typeid(Ep128::Ep128VM) && lockVMThread()) {
    static_cast<Ep128::Ep128VM&>(vm).setTapeMotorSpinUpDelayMs(0U);
    unlockVMThread();
  }
  vmThread.pause(false);

  // Poll guest-frame progress while the cold Enterprise boots at unthrottled
  // Full Speed. archiveAutoRunStep() decides when the startup key is safe.
  Fl::add_timeout(0.020, &archiveAutoRunCallback, (void *) this);
}

bool Ep128EmuGUI::processDroppedArchive(const std::string& fileName)
{
#if !defined(__APPLE__) && !defined(_WIN32)
  (void) fileName;
  errorMessage("Archive Drag & Run is unavailable on this platform.");
  return false;
#else
  if (typeid(vm) != typeid(Ep128::Ep128VM)) {
    errorMessage("ZIP/RAR program Drag & Run currently requires "
                 "Enterprise emulation mode.");
    return false;
  }

  std::string newTempDirectory;
  std::vector<std::string> programs;
  std::string err;
#ifdef WIN32
  const std::string droppedExt=ep128emuLowerExtension(fileName);
  const bool directProgram=(droppedExt==".com"||droppedExt==".trn");
  if(directProgram) {
    programs.push_back(fileName);
  }
  else
#endif
  if (!Ep128EmuArchiveDrop::extractArchive(
          fileName, newTempDirectory, programs, err)) {
    errorMessage(err.c_str());
    return false;
  }

  if (programs.empty()) {
    Ep128EmuArchiveDrop::removeTemporaryDirectory(newTempDirectory);
    errorMessage("The dropped archive contains no COM or TRN program.");
    return false;
  }

  std::string selectedFile;
#ifdef WIN32
  if(directProgram)
    selectedFile=fileName;
  else
#endif
  if (!chooseArchiveProgram(newTempDirectory, programs, selectedFile)) {
    Ep128EmuArchiveDrop::removeTemporaryDirectory(newTempDirectory);
    return false;
  }

  bool useStandard128K = false;
  const std::string programDirectory =
#ifdef WIN32
      (directProgram ? Ep128EmuArchiveDrop::directoryName(selectedFile)
                     : newTempDirectory);
#else
      newTempDirectory;
#endif
  if (!chooseArchiveRunConfiguration(
          programDirectory, selectedFile, useStandard128K)) {
    Ep128EmuArchiveDrop::removeTemporaryDirectory(newTempDirectory);
    return false;
  }
  std::string launchFile(selectedFile);
#ifdef WIN32
  if(directProgram &&
     !Ep128EmuArchiveDrop::canLoadProgramByOriginalName(selectedFile)) {
    if(!Ep128EmuArchiveDrop::winStageProgram(
           selectedFile,newTempDirectory,launchFile,err)) {
      errorMessage(err.c_str());return false;
    }
  }
  else
#endif
  if (!Ep128EmuArchiveDrop::canLoadProgramByOriginalName(selectedFile)) {
    std::string aliasFile;
    if (!Ep128EmuArchiveDrop::createProgramAlias(
            selectedFile, aliasFile, err)) {
      Ep128EmuArchiveDrop::removeTemporaryDirectory(newTempDirectory);
      errorMessage(err.c_str());
      return false;
    }
    launchFile = aliasFile;
  }

  const std::string oldArchiveTemp = archiveTemporaryDirectory;

  try {
    if (useStandard128K)
      applyArchiveStandard128KConfiguration();

    // Drag & Run uses a VM-thread-only File I/O override. Do not mirror the
    // temporary extraction directory into the user's persistent/configured
    // File I/O settings: the running program still sees its companion files,
    // while the configured working directory remains exactly as the user set it.
    startArchiveAutoRun(launchFile);
  }
  catch (...) {
    if (useStandard128K && archiveTemporaryConfigActive) {
      try {
        rollbackArchiveTemporaryConfiguration();
      }
      catch (...) {
      }
    }
    Ep128EmuArchiveDrop::removeTemporaryDirectory(newTempDirectory);
    throw;
  }

  archiveTemporaryDirectory = newTempDirectory;
  if (!oldArchiveTemp.empty() && oldArchiveTemp != newTempDirectory)
    Ep128EmuArchiveDrop::removeTemporaryDirectory(oldArchiveTemp);
  return true;
#endif
}

void Ep128EmuGUI::processDroppedFiles(const std::string& fileList)
{
  std::string fileName;
  size_t start = 0;
  int fileCount = 0;
  while (start <= fileList.size()) {
    size_t end = fileList.find('\n', start);
    if (end == std::string::npos)
      end = fileList.size();
    std::string p = fileList.substr(start, end - start);
    if (!p.empty() && p[p.size() - 1] == '\r')
      p.resize(p.size() - 1);
    if (!p.empty()) {
      fileCount++;
      if (fileName.empty())
        fileName = p;
    }
    if (end >= fileList.size())
      break;
    start = end + 1;
  }
  if (fileCount != 1) {
    errorMessage("Please drop one file at a time.");
    return;
  }

  std::string ext = ep128emuLowerExtension(fileName);
  try {
    if (Ep128EmuArchiveDrop::isSupportedArchive(fileName)
#ifdef WIN32
        || ext == ".com" || ext == ".trn"
#endif
       ) {
      (void) processDroppedArchive(fileName);
    }
    else if (ext == ".cfg") {
      loadConfigFile(fileName, true);
    }
    else if (ext == ".tap" || ext == ".wav" || ext == ".aif" ||
             ext == ".aiff" || ext == ".au" || ext == ".snd" ||
             ext == ".tzx" || ext == ".cdt") {
      openTapeImageFile(fileName, true);
    }
    else if (ext == ".dsk" || ext == ".img" || ext == ".ima" ||
             ext == ".flp") {
      insertFloppyAImageFile(fileName, true);
    }
    else if (ext == ".ep128s" || ext == ".sna" || ext == ".z80") {
      loadSnapshotFile(fileName, true);
    }
    else {
      errorMessage("Unsupported dropped file type. Supported: ZIP/RAR "
                   "Enterprise programs, CFG, EP128S/SNA/Z80, "
                   "DSK/IMG/IMA/FLP, and tape images.");
    }
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
}

void Ep128EmuGUI::droppedFileCallback(void *userData)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  std::string fileList = gui_.pendingDroppedFiles;
  gui_.pendingDroppedFiles.clear();
  if (!fileList.empty()) {
#ifdef WIN32
    // Deferred by 1 ms from the native OLE drop: Explorer/Parallels must no
    // longer obscure our configuration selector or the resumed emulator.
    ep128emuActivateWindowsDroppedFileWindow(gui_.mainWindow,
                                              gui_.emulatorWindow);
#endif
    gui_.processDroppedFiles(fileList);
  }
#ifdef __APPLE__
  ep128emuClearRememberedMacOSDragSourceApplication();
#endif
}

bool Ep128EmuGUI::browseAudioOutputFile(std::string& fileName)
{
  int fmt = audioRecordFormat;
  if (fmt < 0 || fmt > 2)
    fmt = 0;
  const char *pattern = "WAV PCM16 files\t*.wav";
  const char *title = "Record audio output to WAV PCM16";
  const char *ext = "wav";
  if (fmt == 1) {
    pattern = "FLAC lossless files\t*.flac";
    title = "Record audio output to FLAC lossless";
    ext = "flac";
  }
  else if (fmt == 2) {
    pattern = "MP3 VBR files\t*.mp3";
    title = "Record audio output to MP3 VBR High";
    ext = "mp3";
  }
  if (!browseFile(fileName, soundFileDirectory, pattern,
                  Fl_Native_File_Chooser::BROWSE_SAVE_FILE, title))
    return false;
  Ep128Emu::setFileNameExtension(fileName, ext);
  return true;
}

bool Ep128EmuGUI::browseFile(std::string& fileName, std::string& dirName,
                             const char *pattern, int type, const char *title)
{
  bool    retval = false;
  bool    lockFlag = false;
  try {
#ifdef WIN32
    uintptr_t currentThreadID = uintptr_t(GetCurrentThreadId());
#else
    uintptr_t currentThreadID = uintptr_t(pthread_self());
#endif
    if (currentThreadID != mainThreadID) {
      Fl::lock();
      lockFlag = true;
    }
    browseFileWindow->type(type);
    browseFileWindow->options(
        type == Fl_Native_File_Chooser::BROWSE_SAVE_FILE
            ? Fl_Native_File_Chooser::SAVEAS_CONFIRM
            : Fl_Native_File_Chooser::NO_OPTIONS);
    browseFileWindow->title(title);
    browseFileWindow->filter(pattern);
    browseFileWindow->directory(dirName.c_str());
    if (type != Fl_Native_File_Chooser::BROWSE_DIRECTORY &&
        type != Fl_Native_File_Chooser::BROWSE_MULTI_DIRECTORY &&
        type != Fl_Native_File_Chooser::BROWSE_SAVE_DIRECTORY) {
      std::string tmp;
      std::string tmp2;
      Ep128Emu::splitPath(fileName, tmp2, tmp);
#if !(defined(WIN32) || defined(__APPLE__))
      if (dirName.length() > 0) {
        tmp2 = dirName;
        if (dirName[dirName.length() - 1] != '/' &&
            dirName[dirName.length() - 1] != '\\') {
          tmp2 += '/';
        }
        tmp2 += tmp;
        tmp = tmp2;
      }
#endif
      browseFileWindow->preset_file(tmp.c_str());
    }
    else {
      browseFileWindow->preset_file((char *) 0);
    }
    fileName.clear();
    browseFileStatus = 1;
#if defined(LINUX_FLTK_VERSION) && (LINUX_FLTK_VERSION >= 10303)
    // work around buggy GTK file chooser introduced in FLTK 1.3.3
    // not sending FL_UNFOCUS event to the emulator window
    handleFLTKEvent(this, FL_UNFOCUS);
#endif
    if (currentThreadID != mainThreadID) {
      browseFileWindowShowFlag = true;
      do {
        Fl::unlock();
        lockFlag = false;
        updateDisplay();
        Fl::lock();
        lockFlag = true;
      } while (browseFileWindowShowFlag);
    }
    else {
      browseFileStatus = browseFileWindow->show();
    }
    if (browseFileStatus < 0) {
      const char  *s = browseFileWindow->errmsg();
      if (s == (char *) 0 || s[0] == '\0')
        s = "error selecting file";
      std::string tmp(s);
      if (currentThreadID != mainThreadID) {
        Fl::unlock();
        lockFlag = false;
      }
      errorMessage(tmp.c_str());
      if (currentThreadID != mainThreadID) {
        Fl::lock();
        lockFlag = true;
      }
    }
    else if (browseFileStatus == 0) {
      const char  *s = browseFileWindow->filename();
      if (s != (char *) 0) {
        fileName = s;
        Ep128Emu::stripString(fileName);
        std::string tmp;
        Ep128Emu::splitPath(fileName, dirName, tmp);
        retval = true;
      }
    }
    if (currentThreadID != mainThreadID) {
      Fl::unlock();
      lockFlag = false;
    }
  }
  catch (std::exception& e) {
    if (lockFlag)
      Fl::unlock();
    fileName.clear();
    errorMessage(e.what());
  }
  return retval;
}

void Ep128EmuGUI::writeFile(Ep128Emu::File& f, const char *fileName)
{
  if (!config.compressFiles) {
    f.writeFile(fileName);
    return;
  }
  try {
    mainWindow->label("Compressing file...");
    mainWindow->cursor(FL_CURSOR_WAIT);
    Fl::redraw();
    // should actually use Fl::flush() here, but only Fl::wait() does
    // correctly update the display
    Fl::wait(0.0);
    f.writeFile(fileName, false, true);
  }
  catch (...) {
    mainWindow->label(&(windowTitleBuf[0]));
    mainWindow->cursor(!(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
    throw;
  }
  mainWindow->label(&(windowTitleBuf[0]));
  mainWindow->cursor(!(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
}

void Ep128EmuGUI::setSnapshotRuntimeMemoryActive(bool active)
{
  if (snapshotRuntimeMemoryActive == active)
    return;
  snapshotRuntimeMemoryActive = active;
  if (machineConfigWindow && machineConfigWindow->shown())
    machineConfigWindow->updateWindow();
}

void Ep128EmuGUI::applyEmulatorConfiguration(bool updateWindowFlag_)
{
  bool memoryConfigurationApplied = false;
  if (lockVMThread()) {
    try {
      bool    processPriorityChanged_ = config.vmProcessPriorityChanged;
      bool    memoryConfigurationChanged_ = config.memoryConfigurationChanged;
      bool    updateMenuFlag_ =
          config.soundSettingsChanged | processPriorityChanged_;
      config.applySettings();
      // Physical relay mechanics are a real-time-only option. At every
      // other requested speed, including No limit, use stock tape timing.
      if (typeid(vm) == typeid(Ep128::Ep128VM))
        static_cast<Ep128::Ep128VM&>(vm).setTapeMotorSpinUpDelayMs(
            getEffectiveTapeMotorSpinUpDelayMs());
      if (typeid(vm) == typeid(Ep128::Ep128VM))
        // Mechanical click filtering must never alter real motor timing.
        static_cast<Ep128::Ep128VM&>(vm).setTapeRelayReleaseDelayMs(100U);
      memoryConfigurationApplied = memoryConfigurationChanged_;
      {
        Ep128Emu::VideoDisplay::DisplayParameters dp(display.getDisplayParameters());
        if (dp.pixelPerfect != pixelPerfectEnabled) {
          dp.pixelPerfect = pixelPerfectEnabled;
          display.setDisplayParameters(dp);
        }
      }
#ifdef __APPLE__
      if (processPriorityChanged_)
        vmThread.setHostThreadPriority(config.vm.processPriority);
#endif
#ifdef __APPLE__
      // CoreAudio uses a non-blocking SPSC ring: apply the explicit speed.
      vmThread.setSpeedPercentage(int(config.vm.speedPercentage));
#else
      // PortAudio can pace Windows at 100% with sound enabled. Keep this
      // host timing choice separate from logical speed for rewind capture:
      // 100% MUST retain history, while genuine No limit (0) MUST NOT.
      const bool audioClockDriven = (config.vm.speedPercentage == 100U &&
                                    config.sound.enabled);
      vmThread.setSpeedPercentage(int(config.vm.speedPercentage),
                                  audioClockDriven);
#endif
      if (config.joystickSettingsChanged) {
        joystickInput.setConfiguration(config.joystick);
        config.joystickSettingsChanged = false;
      }
      if (updateWindowFlag_) {
        if (diskConfigWindow->shown())
          diskConfigWindow->updateWindow();
        if (displaySettingsWindow->shown())
          displaySettingsWindow->updateWindow();
        if (keyboardConfigWindow->shown())
          keyboardConfigWindow->updateWindow();
        if (soundSettingsWindow->shown())
          soundSettingsWindow->updateWindow();
        if (machineConfigWindow->shown())
          machineConfigWindow->updateWindow();
        if (debugWindow->shown())
          debugWindow->updateWindow();
      }
      if (updateMenuFlag_)
        updateMenu();
      updateDisplay_windowSize();
    }
    catch (...) {
      unlockVMThread();
      throw;
    }
    unlockVMThread();
  }
  // Updating FLTK windows while the VM lock is held can re-enter GUI paths.
  // Publish the runtime/base authority change only after the VM is released.
  if (memoryConfigurationApplied)
    setSnapshotRuntimeMemoryActive(false);
}

static const char *ep128emuFixedShortcutConflict(int key, int modifiers)
{
  struct FixedShortcut {
    int key;
    int modifiers;
    const char *name;
  };
  static const FixedShortcut fixed[] = {
    { '1', FL_ALT, "Speed 10% (Option+1)" },
    { '2', FL_ALT, "Speed 25% (Option+2)" },
    { '3', FL_ALT, "Speed 50% (Option+3)" },
    { '4', FL_ALT, "Speed 100% alternate (Option+4)" },
    { '5', FL_ALT, "Speed 200% (Option+5)" },
    { '6', FL_ALT, "Speed 400% (Option+6)" },
    { FL_F + 9,  FL_SHIFT, "Display settings (Shift+F9)" },
    { FL_F + 10, FL_SHIFT, "Machine settings (Shift+F10)" },
    { FL_F + 11, FL_SHIFT, "Reset machine configuration (Shift+F11)" },
    { FL_F + 12, FL_SHIFT, "Quit (Shift+F12)" },
    { FL_Page_Down, 0, "Quick configuration 1 (PageDown)" },
    { FL_Page_Up,   0, "Quick configuration 2 (PageUp)" },
    { FL_KP + '0', 0, "Open tape (KP0)" },
    { FL_KP + '1', 0, "Tape stop (KP1)" },
    { FL_KP + '3', 0, "Tape play (KP3)" },
    { FL_KP + '4', 0, "Tape rewind (KP4)" },
    { ',', FL_ALT, "Previous tape marker (Option+Comma)" },
    { '.', FL_ALT, "Next tape marker (Option+Period)" },
    { 'b', FL_ALT, "Debugger (Option+B)" },
    { 'c', FL_ALT, "Screenshot alternate (Option+C)" },
    { 'd', FL_ALT, "Floppy configuration (Option+D)" },
    { 'f', FL_ALT, "Working directory (Option+F)" },
    { 'i', FL_ALT, "Keyboard map (Option+I)" },
    { 'k', FL_ALT, "Stop demo (Option+K)" },
    { 'l', FL_ALT, "Load file (Option+L)" },
    { 'o', FL_ALT, "Tape stop (Option+O)" },
    { 'p', FL_ALT, "Tape play (Option+P)" },
    { 'q', FL_ALT, "Load configuration (Option+Q)" },
    { 'r', FL_ALT, "Tape rewind (Option+R)" },
    { 's', FL_ALT, "Save snapshot (Option+S)" },
    { 't', FL_ALT, "Open tape (Option+T)" },
    { 'u', FL_ALT, "Sound settings (Option+U)" }
  };
  for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
    if (fixed[i].key == key && fixed[i].modifiers == modifiers) {
#ifdef __APPLE__
      return fixed[i].name;
#else
      // This legacy conflict table uses macOS names. Convert only the
      // user-facing message; FL_ALT and all actual mappings stay unchanged.
      static std::string hostLabel;
      hostLabel = fixed[i].name;
      size_t pos = hostLabel.find("Option+");
      if (pos != std::string::npos)
        hostLabel.replace(pos, 7, "Alt+");
      return hostLabel.c_str();
#endif
    }
  }
  return (const char *) 0;
}

bool Ep128EmuGUI::setApplicationShortcuts(const std::string *values,
                                           std::string& error)
{
  int keys[EP_SC_COUNT];
  int mods[EP_SC_COUNT];
  std::string canonical[EP_SC_COUNT];

  error.clear();
  for (int i = 0; i < EP_SC_COUNT; i++) {
    std::string parseError;
    if (!ep128emuParseAppShortcut(values[i], keys[i], mods[i],
                                  canonical[i], parseError)) {
      error = std::string(ep128emuAppShortcutName(i)) + ": " + parseError;
      return false;
    }
    const char *fixed = ep128emuFixedShortcutConflict(keys[i], mods[i]);
    if (fixed) {
      error = std::string(ep128emuAppShortcutName(i)) +
              ": shortcut conflicts with fixed application command: " + fixed;
      return false;
    }
    for (int j = 0; j < i; j++) {
      if (keys[j] == keys[i] && mods[j] == mods[i]) {
        error = std::string("Shortcut conflict: ") +
                ep128emuAppShortcutName(j) + " and " +
                ep128emuAppShortcutName(i) + " both use " + canonical[i] + ".";
        return false;
      }
    }
  }

  for (int i = 0; i < EP_SC_COUNT; i++) {
    appShortcutStrings[i] = canonical[i];
    appShortcutKeys[i] = keys[i];
    appShortcutModifiers[i] = mods[i];
  }
  appShortcutPressedMask = 0U;
  Fl::remove_timeout(&nextFrameRepeatCallback, (void *) this);
  Fl::remove_timeout(&previousFrameRepeatCallback, (void *) this);
  refreshApplicationShortcutMenus();
  return true;
}

void Ep128EmuGUI::resetApplicationShortcuts()
{
  std::string values[EP_SC_COUNT];
  for (int i = 0; i < EP_SC_COUNT; i++)
    values[i] = ep128emuAppShortcutDefault(i);
  std::string error;
  (void) setApplicationShortcuts(values, error);
}

void Ep128EmuGUI::refreshApplicationShortcutMenus()
{
  if (!mainMenuBar)
    return;
  for (int i = 0; i < EP_SC_COUNT; i++) {
    int menuIndex = -1;
    if (i == EP_SC_NEXT_FRAME) {
      menuIndex = ep128emuFindMenuIndexByCallback(
          mainMenuBar, &menuCallback_Machine_NextFrame);
    }
    else if (i == EP_SC_PREVIOUS_FRAME) {
      menuIndex = ep128emuFindMenuIndexByCallback(
          mainMenuBar, &menuCallback_Machine_PreviousFrame);
    }
    else if (i == EP_SC_OLDEST_FRAME) {
      menuIndex = ep128emuFindMenuIndexByCallback(
          mainMenuBar, &menuCallback_Machine_OldestFrame);
    }
    else if (i == EP_SC_LATEST_FRAME) {
      menuIndex = ep128emuFindMenuIndexByCallback(
          mainMenuBar, &menuCallback_Machine_LatestFrame);
    }
    else {
      menuIndex = mainMenuBar->find_index(ep128emuAppShortcutMenuPath(i));
    }
    if (menuIndex < 0)
      continue;

    Fl_Menu_Item *mm =
        const_cast<Fl_Menu_Item *>(mainMenuBar->menu() + menuIndex);
    if ((i == EP_SC_NEXT_FRAME || i == EP_SC_PREVIOUS_FRAME ||
         i == EP_SC_OLDEST_FRAME || i == EP_SC_LATEST_FRAME) &&
        appShortcutModifiers[i] == 0) {
      // Unmodified timeline-navigation keys are deliberately pause-local.
      // Registering them with FLTK would make the menu consume them while the
      // emulated machine is running. Their visible shortcut hints therefore
      // live in the menu label instead of shortcut().
      mm->shortcut(0);
    }
    else {
      mm->shortcut(ep128emuMakeFLTKShortcut(appShortcutKeys[i],
                                            appShortcutModifiers[i]));
    }

    if (i == EP_SC_NEXT_FRAME || i == EP_SC_PREVIOUS_FRAME) {
      const bool isPrevious = (i == EP_SC_PREVIOUS_FRAME);
      const std::string label = ep128emuTimelineMenuLabel(
          (isPrevious ? "1 frame backward" : "1 frame forward"),
          appShortcutKeys[i], appShortcutModifiers[i]);
      mainMenuBar->replace(menuIndex, label.c_str());
    }
    else if (i == EP_SC_OLDEST_FRAME || i == EP_SC_LATEST_FRAME) {
      const bool isOldest = (i == EP_SC_OLDEST_FRAME);
      const std::string label = ep128emuTimelineMenuLabel(
          (isOldest ? "Oldest frame" : "Latest frame"),
          appShortcutKeys[i], appShortcutModifiers[i]);
      mainMenuBar->replace(menuIndex, label.c_str());
    }
  }

  {
    const int i = EP_SC_PREVIOUS_FRAME;
    const int menuIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Previous100);
    if (menuIndex >= 0) {
      const std::string label = ep128emuCoarseTimelineMenuLabel(
          "100 frames backward",
          appShortcutKeys[i], appShortcutModifiers[i]);
      mainMenuBar->replace(menuIndex, label.c_str());
    }
  }
  {
    const int i = EP_SC_NEXT_FRAME;
    const int menuIndex = ep128emuFindMenuIndexByCallback(
        mainMenuBar, &menuCallback_Machine_Next100);
    if (menuIndex >= 0) {
      const std::string label = ep128emuCoarseTimelineMenuLabel(
          "100 frames forward",
          appShortcutKeys[i], appShortcutModifiers[i]);
      mainMenuBar->replace(menuIndex, label.c_str());
    }
  }

  mainMenuBar->redraw();
}

void Ep128EmuGUI::nextFrameRepeatCallback(void *userData)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  const uint32_t mask = (uint32_t(1U) << EP_SC_NEXT_FRAME);
  if (!(gui_.appShortcutPressedMask & mask))
    return;
  Ep128Emu::VMThread::VMThreadStatus vmThreadStatus(gui_.vmThread);
  if (!vmThreadStatus.isPaused) {
    gui_.appShortcutPressedMask &= ~mask;
    return;
  }

  gui_.menuCallback_Machine_NextFrame((Fl_Widget *) 0, (void *) &gui_);

  // 25 frame steps per second while held. This is deliberately slower than
  // normal PAL playback but substantially faster and more responsive than
  // relying on the host keyboard repeat rate.
  Fl::repeat_timeout(0.04, &nextFrameRepeatCallback, userData);
}

void Ep128EmuGUI::previousFrameRepeatCallback(void *userData)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  const uint32_t mask = (uint32_t(1U) << EP_SC_PREVIOUS_FRAME);
  if (!(gui_.appShortcutPressedMask & mask))
    return;
  Ep128Emu::VMThread::VMThreadStatus vmThreadStatus(gui_.vmThread);
  if (!vmThreadStatus.isPaused) {
    gui_.appShortcutPressedMask &= ~mask;
    return;
  }

  gui_.menuCallback_Machine_PreviousFrame((Fl_Widget *) 0, (void *) &gui_);

  // Match held Next Frame: 25 reverse steps per second after the initial
  // 200 ms hold delay.
  Fl::repeat_timeout(0.04, &previousFrameRepeatCallback, userData);
}

void Ep128EmuGUI::syncTimelineKeyState(int shortcutIndex, bool pressed)
{
  if (shortcutIndex != EP_SC_NEXT_FRAME &&
      shortcutIndex != EP_SC_PREVIOUS_FRAME &&
      shortcutIndex != EP_SC_OLDEST_FRAME &&
      shortcutIndex != EP_SC_LATEST_FRAME)
    return;

  int guestKey = config.convertKeyCode(appShortcutKeys[shortcutIndex]);
  if (guestKey < 0)
    return;

  try {
    // Even when this physical key is consumed as a paused host-navigation
    // shortcut, keep VMThread's current host-input shadow accurate. While
    // paused this never mutates the historical guest state; Resume applies
    // the shadow as the new branch input state.
    vmThread.setKeyboardState(uint8_t(guestKey), pressed);
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
}

bool Ep128EmuGUI::handleApplicationShortcut(int keyCode, bool isKeyPress)
{
  int normalizedKey = keyCode;
  if (normalizedKey >= 'A' && normalizedKey <= 'Z')
    normalizedKey = normalizedKey - 'A' + 'a';
  int modifiers = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);

  if (!isKeyPress) {
    bool consumed = false;
    for (int i = 0; i < EP_SC_COUNT; i++) {
      const uint32_t mask = uint32_t(1U) << i;
      bool releasedShortcutKey =
          ((appShortcutPressedMask & mask) &&
           appShortcutKeys[i] == normalizedKey);
#ifdef __APPLE__
      // FLTK's Cocoa keysym for shifted punctuation follows the translated
      // character (charactersIgnoringModifiers still includes Shift). For
      // pause-local frame keys, use physical key state as the authoritative
      // release test so e.g. Hungarian Shift+Comma cannot leave the host
      // shortcut pressed-mask stuck.
      if (!releasedShortcutKey && (appShortcutPressedMask & mask) &&
          (i == EP_SC_NEXT_FRAME || i == EP_SC_PREVIOUS_FRAME) &&
          !Fl::event_key(appShortcutKeys[i]))
        releasedShortcutKey = true;
#endif
      if (releasedShortcutKey) {
        syncTimelineKeyState(i, false);
        appShortcutPressedMask &= ~mask;
        if (i == EP_SC_NEXT_FRAME)
          Fl::remove_timeout(&nextFrameRepeatCallback, (void *) this);
        else if (i == EP_SC_PREVIOUS_FRAME)
          Fl::remove_timeout(&previousFrameRepeatCallback, (void *) this);
        consumed = true;
      }
    }
    if (consumed)
      return true;
  }

  // Shift + the unmodified pause-local frame keys performs a coarse jump
  // through already recorded rewind history. Keep this derived from the
  // configured plain frame keys rather than introducing two more configurable
  // shortcuts: if Previous/Next are remapped, Shift follows the remap.
  if (modifiers == FL_SHIFT) {
    const int previousKey = appShortcutKeys[EP_SC_PREVIOUS_FRAME];
    const int nextKey = appShortcutKeys[EP_SC_NEXT_FRAME];
    const bool previousEnabled =
        (appShortcutModifiers[EP_SC_PREVIOUS_FRAME] == 0);
    const bool nextEnabled =
        (appShortcutModifiers[EP_SC_NEXT_FRAME] == 0);
    const bool previousPhysicalDown =
        (previousEnabled && Fl::event_key(previousKey));
    const bool nextPhysicalDown =
        (nextEnabled && Fl::event_key(nextKey));
    const int jumpDirection = ep128emuPauseLocalFrameJumpDirection(
        normalizedKey, modifiers, previousKey, nextKey,
        previousEnabled, nextEnabled,
        previousPhysicalDown, nextPhysicalDown);
    const int frameIndex =
        (jumpDirection < 0 ? EP_SC_PREVIOUS_FRAME :
         (jumpDirection > 0 ? EP_SC_NEXT_FRAME : -1));

    if (frameIndex >= 0) {
      if (!isKeyPress)
        return false;       // running key-down belonged to the guest

      Ep128Emu::VMThread::VMThreadStatus vmThreadStatus(vmThread);
      if (!vmThreadStatus.isPaused)
        return false;       // preserve Shift+key as a guest key while running

      const uint32_t mask = uint32_t(1U) << frameIndex;
      if (appShortcutPressedMask & mask)
        return true;        // ignore platform auto-repeat
      syncTimelineKeyState(frameIndex, true);
      appShortcutPressedMask |= mask;

      if (frameIndex == EP_SC_PREVIOUS_FRAME) {
        if (!oldAudioRecordingFlag && !oldVideoRecordingFlag)
          (void) vmThread.rewindFrames(100);
      }
      else {
        (void) vmThread.advanceRecordedFrames(100);
      }
      return true;
    }
  }

  for (int i = 0; i < EP_SC_COUNT; i++) {
    if (appShortcutKeys[i] != normalizedKey ||
        appShortcutModifiers[i] != modifiers)
      continue;

    const bool pauseLocalTimelineNavigation =
        ((i == EP_SC_NEXT_FRAME || i == EP_SC_PREVIOUS_FRAME ||
          i == EP_SC_OLDEST_FRAME || i == EP_SC_LATEST_FRAME) &&
         appShortcutModifiers[i] == 0);
    uint32_t mask = uint32_t(1U) << i;
    if (pauseLocalTimelineNavigation) {
      if (!isKeyPress) {
        // The pressed-mask loop above has already consumed key-up when
        // key-down was a host shortcut. Reaching here means key-down belonged
        // to the emulated machine, so key-up must follow the same route even
        // if Pause changed in between.
        if (!ep128emuPauseLocalFrameStepConsumesEvent(false, false, false))
          return false;
      }
      else {
        Ep128Emu::VMThread::VMThreadStatus vmThreadStatus(vmThread);
        if (!ep128emuPauseLocalFrameStepConsumesEvent(
                true, vmThreadStatus.isPaused, false)) {
          // Plain timeline keys remain ordinary emulated keys while running.
          continue;
        }
        syncTimelineKeyState(i, true);
      }
    }

    if (!isKeyPress) {
      appShortcutPressedMask &= ~mask;
      return true;
    }
    if (appShortcutPressedMask & mask) {
      // Ignore platform key-repeat for all application commands, including
      // Next/Previous Frame use their own deterministic timers, so stepping
      // speed is independent of macOS keyboard repeat settings and cannot
      // double-trigger from both sources.
      return true;
    }

    appShortcutPressedMask |= mask;
    if (i == EP_SC_NEXT_FRAME) {
      Fl::remove_timeout(&nextFrameRepeatCallback, (void *) this);
      Fl::add_timeout(0.20, &nextFrameRepeatCallback, (void *) this);
    }
    else if (i == EP_SC_PREVIOUS_FRAME) {
      Fl::remove_timeout(&previousFrameRepeatCallback, (void *) this);
      Fl::add_timeout(0.20, &previousFrameRepeatCallback, (void *) this);
    }

    switch (i) {
    case EP_SC_DISPLAY_CYCLE:
      menuCallback_Options_DpyMode((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_PAUSE:
      menuCallback_Machine_Pause((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_RESET:
      menuCallback_Machine_Reset((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_SCREENSHOT:
      menuCallback_File_Screenshot((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_TAPE_FORCE_TOGGLE:
      menuCallback_Machine_TapeForce((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_TRDOS_ENTER:
      menuCallback_Machine_BetaDiskEnterDOS((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_FORCE_RESET:
      menuCallback_Machine_ColdReset((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_QUICK_SAVE:
      menuCallback_File_QSSave((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_QUICK_LOAD:
      menuCallback_File_QSLoad((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_NEXT_FRAME:
      menuCallback_Machine_NextFrame((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_PREVIOUS_FRAME:
      menuCallback_Machine_PreviousFrame((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_OLDEST_FRAME:
      menuCallback_Machine_OldestFrame((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_LATEST_FRAME:
      menuCallback_Machine_LatestFrame((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_DEMO_START:
      menuCallback_File_RecordDemo((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_AUDIO_START:
      menuCallback_File_RecordSound((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_AUDIO_STOP:
      menuCallback_File_StopSndRecord((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_VIDEO_START:
      menuCallback_File_RecordVideo((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_VIDEO_STOP:
      menuCallback_File_StopAVIRecord((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_REPLACE_DRIVE_A:
      menuCallback_Options_FloppyRpA((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_SPEED_100:
      menuCallback_Machine_Speed_100((Fl_Widget *) 0, (void *) this);
      break;
    case EP_SC_SPEED_NOLIMIT:
      menuCallback_Machine_FullSpeed((Fl_Widget *) 0, (void *) this);
      break;
    default:
      break;
    }
    return true;
  }
  return false;
}

#if defined(__APPLE__) || defined(WIN32)
static void ep128emuSetMenuItemEnabled(Fl_Menu_Bar *bar,
                                       const char *path, bool enabled)
{
  const Fl_Menu_Item *item = bar->find_item(path);
  if (!item) return;
  Fl_Menu_Item *menu = const_cast<Fl_Menu_Item *>(item);
  if (enabled) menu->activate();
  else menu->deactivate();
}
#endif

void Ep128EmuGUI::updateMenu()
{
  // "Machine/Speed/No limit"
  if (config.vm.speedPercentage == 0U)
    getMenuItem(3).set();
  else
    getMenuItem(3).clear();
  // "File/Record audio/Stop"
  if (config.sound.file.length() > 0)
    getMenuItem(1).activate();
  else
    getMenuItem(1).deactivate();
  updatePixelPerfectMenuLabel();
  const Fl_Menu_Item *printerItem =
      mainMenuBar->find_item("Machine/Printer/Capture printer output");
  if (printerItem) {
    Fl_Menu_Item *item = const_cast<Fl_Menu_Item *>(printerItem);
    if (printerCaptureEnabled) item->set();
    else item->clear();
  }
  const Fl_Menu_Item *formatItem = mainMenuBar->find_item(
      printerOutputFormat == 1
          ? "Machine/Printer/Output extension/RAW (.raw)"
          : "Machine/Printer/Output extension/Text (.txt)");
  if (formatItem)
    const_cast<Fl_Menu_Item *>(formatItem)->setonly();
  const Fl_Menu_Item *relaySoundItem =
      mainMenuBar->find_item("Options/Sound/Tape remote relay click");
  if (relaySoundItem) {
    Fl_Menu_Item *item = const_cast<Fl_Menu_Item *>(relaySoundItem);
    if (tapeRelayClickEnabled)
      item->set();
    else
      item->clear();
  }
  if (typeid(vm) == typeid(Ep128::Ep128VM)) {
    char path[96];
    const char *choice = "1000 ms";
    if (tapeMotorSpinUpDelayMs == 0) choice = "0 ms (off)";
    else if (tapeMotorSpinUpDelayMs == 500) choice = "500 ms";
    else if (tapeMotorSpinUpDelayMs == 1500) choice = "1500 ms";
    std::snprintf(path, sizeof(path), "Machine/Tape/Motor spin-up/%s",
                  choice);
    const Fl_Menu_Item *item = mainMenuBar->find_item(path);
    if (item)
      const_cast<Fl_Menu_Item *>(item)->setonly();
  }
  const Fl_Menu_Item *forceItem =
      mainMenuBar->find_item("Machine/Tape/Force motor on");
  if (forceItem) {
    Fl_Menu_Item *m = const_cast<Fl_Menu_Item *>(forceItem);
    if (config.tape.forceMotorOn)
      m->set();
    else
      m->clear();
  }
  // "Options/Process priority"
  int     tmp = config.vm.processPriority + 6;
  tmp = (tmp > 4 ? (tmp < 8 ? tmp : 8) : 4);
  getMenuItem(tmp).setonly();
#if defined(__APPLE__) || defined(WIN32)
  // Recompute from VM-owned controller state, never from remembered menu state.
  // updateMenu() can be invoked while applyEmulatorConfiguration owns VMThread
  // lock; do NOT re-lock VMThread here. Expansion mutations are GUI-thread only.
  const Fl_Menu_Item *info = mainMenuBar->find_item(
      "Options/Notifications/Show informational dialogs");
  if (info) {
    Fl_Menu_Item *m = const_cast<Fl_Menu_Item *>(info);
    if (showInformationalDialogs) m->set();
    else m->clear();
  }
  if (typeid(vm) == typeid(ZX128::ZX128VM)) {
    ZX128::ZX128VM& zx = static_cast<ZX128::ZX128VM&>(vm);
    ZX128::BetaDiskBus *beta = zx.getBetaDiskForTesting();
    const bool betaOn = (beta != 0);
    const bool speccyOn = (zx.getSpeccyDOSForTesting() != 0);
    const bool betaMounted = betaOn &&
        beta->isDiskMounted();
    const bool speccyMounted = speccyOn &&
        !speccyDOSMountedSDDPath.empty();
    const bool idleInput = (betaDOSInputStep < 0 && archiveAutoRunPhase < 0);
    const bool canEnable = !betaOn && !speccyOn;
    // All paths below match the exact labels in createMenus().
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/SpeccyDOS 4.2 - WD1770 (bundled ROM)", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/SpeccyDOS 4.2 - WD1772 (bundled ROM)", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/Load external SpeccyDOS 4.x ROM...", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/Insert SDD drive A...", speccyOn && !speccyMounted);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/Write protect drive A",
        speccyOn && (!speccyMounted ||
          zx.getSpeccyDOSForTesting()->getDrive(0).isNativeSDDWritable()));
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/Eject drive A", speccyMounted);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/SpeccyDOS (experimental)/Disable SpeccyDOS", speccyOn);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Enable TR-DOS 5.03 (recommended)", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Enable TR-DOS 5.04T (verified)", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Enable TR-DOS 6.04", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Load external 16K TR-DOS ROM...", canEnable);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Enter TR-DOS", betaOn && idleInput);
    const Fl_Menu_Item* autoItem = mainMenuBar->find_item(
        "Machine/Beta Disk (experimental)/Auto-enter TR-DOS after reset or TRD insert");
    if (autoItem) {
      Fl_Menu_Item* mm=const_cast<Fl_Menu_Item*>(autoItem);
      if (betaDOSAutoEnterOnReset) mm->set(); else mm->clear();
    }
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Insert TRD drive A...", betaOn && !betaMounted && idleInput);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Write protect drive A",
        betaOn && idleInput && (!betaMounted || beta->isWritableCopyMounted()));
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Eject TRD drive A", betaMounted && idleInput);
    ep128emuSetMenuItemEnabled(mainMenuBar, "Machine/Beta Disk (experimental)/Disable Beta Disk", betaOn && idleInput);
    const Fl_Menu_Item* spProt=mainMenuBar->find_item("Machine/SpeccyDOS (experimental)/Write protect drive A");
    if (spProt) {
      const bool wp=speccyMounted ? zx.getSpeccyDOSForTesting()->getDrive(0).getIsWriteProtected() : speccyDOSWriteProtect;
      Fl_Menu_Item* m=const_cast<Fl_Menu_Item*>(spProt);
      if (wp) m->set(); else m->clear();
    }
    const Fl_Menu_Item* bProt=mainMenuBar->find_item("Machine/Beta Disk (experimental)/Write protect drive A");
    if (bProt) {
      const bool wp=betaMounted ? beta->isWriteProtected() : betaDiskWriteProtect;
      Fl_Menu_Item* m=const_cast<Fl_Menu_Item*>(bProt);
      if (wp) m->set(); else m->clear();
    }
    ep128emuSetMenuItemEnabled(mainMenuBar, "File/Save snapshot (" EP128EMU_ALT_KEY_TEXT "S)", !betaOn && !speccyOn);
    ep128emuSetMenuItemEnabled(mainMenuBar, "File/Quick snapshot/Save", !betaOn && !speccyOn);
    ep128emuSetMenuItemEnabled(mainMenuBar, "File/Record demo", !betaOn && !speccyOn);
  }
#endif
}

void Ep128EmuGUI::errorMessageCallback(void *userData, const char *msg)
{
  reinterpret_cast<Ep128EmuGUI *>(userData)->errorMessage(msg);
}

void Ep128EmuGUI::fileNameCallback(void *userData, std::string& fileName)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  try {
    gui_.guestFileIOPickerActive.store(true, std::memory_order_release);
#ifdef WIN32
    ep128emuWindowsGuiTrace("GUEST-FILEIO-CHOOSER-BEGIN", -1, 0, 0);
#endif
    std::string tmp(gui_.config.fileio.workingDirectory);
    gui_.browseFile(fileName, tmp, "All files\t*",
                    Fl_Native_File_Chooser::BROWSE_FILE,
                    "Open file");
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
  gui_.guestFileIOPickerActive.store(false, std::memory_order_release);
#ifdef WIN32
  ep128emuWindowsGuiTrace("GUEST-FILEIO-CHOOSER-END", -1, 0, 0);
#endif
}

void Ep128EmuGUI::videoFileNameCallback(void *userData, std::string& fileName)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  try {
    gui_.guestFileIOPickerActive.store(true, std::memory_order_release);
    std::string tmp(gui_.config.fileio.workingDirectory);
    gui_.browseFile(fileName, tmp, "AVI files\t*.avi",
                    Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                    "Select next video output file");
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
  gui_.guestFileIOPickerActive.store(false, std::memory_order_release);
}

void Ep128EmuGUI::fltkCheckCallback(void *userData)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  try {
    if (gui_.flDisplay->checkEvents())
      gui_.emulatorWindow->redraw();
  }
  catch (...) {
  }
}

void Ep128EmuGUI::screenshotCallback(void *userData,
                                     const unsigned char *buf, int w_, int h_)
{
  reinterpret_cast<Ep128EmuGUI *>(userData)->
      saveScreenshot(buf, w_, h_, false);
}

void Ep128EmuGUI::screenshotAsCallback(void *userData,
                                       const unsigned char *buf, int w_, int h_)
{
  reinterpret_cast<Ep128EmuGUI *>(userData)->
      saveScreenshot(buf, w_, h_, true);
}

void Ep128EmuGUI::saveScreenshot(const unsigned char *buf, int w_, int h_,
                                 bool saveAs)
{
  try {
    std::string fName;
    if (saveAs) {
      fName = screenshotFileName;
      if (!browseFile(fName, screenshotDirectory, "PNG files\t*.png",
                      Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                      "Save screenshot as"))
        return;
      const std::string selectedName(fName);
      Ep128Emu::setFileNameExtension(fName, "png");
      // FLTK's overwrite confirmation takes place before extension
      // normalization; confirm the normalized target as well.
      if (selectedName != fName) {
        std::FILE *prior = Ep128Emu::fileOpen(fName.c_str(), "rb");
        if (prior) {
          std::fclose(prior);
          if (fl_choice("Replace the existing PNG file?\n%s",
                        "Cancel", "Replace", 0, fName.c_str()) != 1)
            return;
        }
      }
      screenshotFileName = fName;
    }
    else {
      const std::time_t now = std::time((std::time_t *) 0);
      const std::tm *parts = std::localtime(&now);
      if (!parts)
        throw Ep128Emu::Exception("Cannot determine screenshot timestamp");
      char stamp[32];
      if (!std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", parts))
        throw Ep128Emu::Exception("Cannot format screenshot timestamp");
      const char *machine = ep128emuMachineShortName(vm);
      // Automatic F12 captures ALWAYS go into the host Pictures directory.
      // gui.screenshotDirectory is the persisted last Save As chooser location,
      // and may still point to an old FileIO/working directory. Never use that
      // legacy value to decide the destination of an automatic capture.
      std::string dir;
#ifdef WIN32
      const wchar_t *home = _wgetenv(L"USERPROFILE");
      if (home && *home) {
        std::wstring pictures(home);
        pictures += L"\\Pictures";
        const DWORD attrs = GetFileAttributesW(pictures.c_str());
        dir = ep128emuFFUTF8(attrs != INVALID_FILE_ATTRIBUTES &&
                            (attrs & FILE_ATTRIBUTE_DIRECTORY)
                            ? pictures : std::wstring(home));
      }
#else
      const char *home = std::getenv("HOME");
      if (home && *home) {
        std::string pictures = std::string(home) + "/Pictures";
        struct stat st;
        dir = (::stat(pictures.c_str(), &st) == 0 && S_ISDIR(st.st_mode)
               ? pictures : std::string(home));
      }
#endif
      if (dir.empty())
        throw Ep128Emu::Exception("Cannot resolve user Pictures directory");
      if (dir[dir.size() - 1] != '/' && dir[dir.size() - 1] != '\\')
        dir += '/';
      // If another process already created this timestamp/sequence, the
      // exclusive PNG open fails with EEXIST and we select the next suffix.
      for (unsigned int attempt = 0; attempt < 10000U; ++attempt) {
        char base[96];
        std::snprintf(base, sizeof(base), "%s_%s_%06u.png", machine,
                      stamp, screenshotSequence++);
        fName = dir + base;
        try {
          Ep128Emu::writePNGImage(fName.c_str(), buf, w_, h_, 256,
                                  true, 32768, true);
          return;
        }
        catch (const std::exception&) {
          if (errno != EEXIST)
            throw;
        }
      }
      throw Ep128Emu::Exception("Cannot find a free screenshot filename");
    }
    mainWindow->label("Saving screenshot...");
    mainWindow->cursor(FL_CURSOR_WAIT);
    Fl::redraw();
    Fl::wait(0.0);
    try {
      Ep128Emu::writePNGImage(fName.c_str(), buf, w_, h_, 256, true, 32768);
    }
    catch (...) {
      mainWindow->label(&(windowTitleBuf[0]));
      mainWindow->cursor(
          !(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
      throw;
    }
    mainWindow->label(&(windowTitleBuf[0]));
    mainWindow->cursor(
        !(displayMode & 1) ? FL_CURSOR_DEFAULT : FL_CURSOR_NONE);
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
}

void Ep128EmuGUI::pollJoystickInput(void *userData)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(userData));
  while (true) {
    int     e = gui_.joystickInput.getEvent();
    if (!e)
      break;
    int     keyCode = (e > 0 ? e : (-e));
    bool    isKeyPress = (e > 0);
    // NOTE: since this function is called from the emulation thread, it
    // could be unsafe to access the configuration from here; however, the
    // emulation thread is locked while the keyboard map is updated (see
    // applyEmulatorConfiguration())
    int     n = gui_.config.convertKeyCode(keyCode);
    if (n >= 0)
      gui_.vm.setKeyboardState(uint8_t(n), isKeyPress);
  }
}

bool Ep128EmuGUI::closeDemoFile(bool stopDemo_)
{
  if (!demoRecordFile)
    return true;
  if (stopDemo_ && !lockVMThread())
    return false;
  std::string     fName(demoRecordFileName);
  demoRecordFileName.clear();
  Ep128Emu::File  *f = demoRecordFile;
  demoRecordFile = (Ep128Emu::File *) 0;
  if (stopDemo_) {
    try {
      vm.stopDemo();
    }
    catch (std::exception& e) {
      unlockVMThread();
      delete f;
      errorMessage(e.what());
      return true;
    }
    catch (...) {
      unlockVMThread();
      delete f;
      throw;
    }
    unlockVMThread();
  }
  try {
    writeFile(*f, fName.c_str());
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
  delete f;
  return true;
}

void Ep128EmuGUI::saveQuickConfig(int n)
{
  const char  *fName = (char *) 0;
  if (typeid(vm) == typeid(Ep128::Ep128VM))
    fName = (n == 1 ? "epvmcfg1.cfg" : "epvmcfg2.cfg");
  else if (typeid(vm) == typeid(ZX128::ZX128VM))
    fName = (n == 1 ? "zxvmcfg1.cfg" : "zxvmcfg2.cfg");
  else if (typeid(vm) == typeid(CPC464::CPC464VM))
    fName = (n == 1 ? "cpvmcfg1.cfg" : "cpvmcfg2.cfg");
  else
    fName = (n == 1 ? "tvvmcfg1.cfg" : "tvvmcfg2.cfg");
  try {
    Ep128Emu::ConfigurationDB tmpCfg;
    tmpCfg.createKey("vm.cpuClockFrequency", config.vm.cpuClockFrequency);
    tmpCfg.createKey("vm.videoClockFrequency", config.vm.videoClockFrequency);
    tmpCfg.createKey("vm.soundClockFrequency", config.vm.soundClockFrequency);
    tmpCfg.createKey("vm.speedPercentage", config.vm.speedPercentage);
    tmpCfg.createKey("vm.enableMemoryTimingEmulation",
                     config.vm.enableMemoryTimingEmulation);
    std::string machineTag("ep128emu-machine: ");
    machineTag += ep128emuMachineShortName(vm);
    tmpCfg.saveState(fName, true, machineTag.c_str());
  }
  catch (std::exception& e) {
    errorMessage(e.what());
  }
}

// ----------------------------------------------------------------------------

void Ep128EmuGUI::menuCallback_RecentDisk(Fl_Widget *o, void *v)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  int n = ep128emuRecentMenuIndex(o);
  if (n < 0 || gui_.recentDiskImages[n].empty())
    return;
  if (!ep128emuFileExists(gui_.recentDiskImages[n])) {
    gui_.removeRecentFile(gui_.recentDiskImages, n);
    gui_.errorMessage("Recent file no longer exists and was removed.");
    return;
  }
  try {
    gui_.insertFloppyAImageFile(gui_.recentDiskImages[n], true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_RecentTape(Fl_Widget *o, void *v)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  int n = ep128emuRecentMenuIndex(o);
  if (n < 0 || gui_.recentTapeImages[n].empty())
    return;
  if (!ep128emuFileExists(gui_.recentTapeImages[n])) {
    gui_.removeRecentFile(gui_.recentTapeImages, n);
    gui_.errorMessage("Recent file no longer exists and was removed.");
    return;
  }
  try {
    gui_.openTapeImageFile(gui_.recentTapeImages[n], true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_RecentSnapshot(Fl_Widget *o, void *v)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  int n = ep128emuRecentMenuIndex(o);
  if (n < 0 || gui_.recentSnapshots[n].empty())
    return;
  if (!ep128emuFileExists(gui_.recentSnapshots[n])) {
    gui_.removeRecentFile(gui_.recentSnapshots, n);
    gui_.errorMessage("Recent file no longer exists and was removed.");
    return;
  }
  try {
    gui_.loadSnapshotFile(gui_.recentSnapshots[n], true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_RecentConfig(Fl_Widget *o, void *v)
{
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  int n = ep128emuRecentMenuIndex(o);
  if (n < 0 || gui_.recentConfigurations[n].empty())
    return;
  if (!ep128emuFileExists(gui_.recentConfigurations[n])) {
    gui_.removeRecentFile(gui_.recentConfigurations, n);
    gui_.errorMessage("Recent file no longer exists and was removed.");
    return;
  }
  try {
    gui_.loadConfigFile(gui_.recentConfigurations[n], true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_RecentClearAll(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  for (int i = 0; i < 10; i++) {
    gui_.recentDiskImages[i].clear();
    gui_.recentTapeImages[i].clear();
    gui_.recentSnapshots[i].clear();
    gui_.recentConfigurations[i].clear();
  }
  gui_.refreshRecentMenus();
}

void Ep128EmuGUI::menuCallback_File_LoadSnapshot(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.snapshotDirectory,
                        "Snapshot files\t*.{ep128s,sna,z80}",
                        Fl_Native_File_Chooser::BROWSE_FILE,
                        "Load snapshot")) {
      gui_.loadSnapshotFile(tmp, true);
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_LoadFile(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.loadFileDirectory, "All files\t*",
                        Fl_Native_File_Chooser::BROWSE_FILE,
                        "Load ep128emu binary file")) {
      Ep128Emu::File f(tmp.c_str());
      int fileMachineType = f.getMachineType();
      if (!gui_.prepareMachineTypedLoad(fileMachineType, tmp, 3, false))
        return;
      bool runtimeMemoryState = ep128emuFileHasRuntimeMemoryState(f);
      bool configState =
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CONFIG_DB);
      if (gui_.lockVMThread()) {
        try {
          gui_.vm.registerChunkTypes(f);
          gui_.config.registerChunkType(f);
          f.processAllChunks();
          if (runtimeMemoryState)
            gui_.setSnapshotRuntimeMemoryActive(true);
          else if (configState && gui_.snapshotRuntimeMemoryActive)
            gui_.config.memoryConfigurationChanged = true;
          gui_.vmThread.clearRewindHistory();
          gui_.applyEmulatorConfiguration(true);
        }
        catch (...) {
          gui_.unlockVMThread();
          throw;
        }
        gui_.unlockVMThread();
      }
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_LoadConfig(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.configDirectory,
                        "Configuration files\t*.cfg",
                        Fl_Native_File_Chooser::BROWSE_FILE,
                        "Load ASCII format configuration file")) {
      gui_.loadConfigFile(tmp, true);
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_SaveConfig(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.configDirectory,
                        "Configuration files\t*.cfg",
                        Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                        "Save configuration as ASCII text file")) {
      Ep128Emu::setFileNameExtension(tmp, "cfg");
      std::string machineTag("ep128emu-machine: ");
      machineTag += ep128emuMachineShortName(gui_.vm);
      if (gui_.archiveTemporaryConfigActive) {
        // Archive Drag & Run may temporarily replace the live configuration
        // with Standard 128K. ASCII export must preserve the user's retained
        // base configuration, just like File -> Configuration -> Save does.
        Ep128Emu::ConfigurationDB::saveStateBufferAsASCII(
            gui_.archiveOriginalConfigState, tmp.c_str(), false,
            machineTag.c_str());
      }
      else {
        gui_.config.saveState(tmp.c_str(), false, machineTag.c_str());
      }
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_SaveMainCfg(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::File  f;
    f.addMachineTypeMetadata(ep128emuMachineType(gui_.vm));
    if (gui_.archiveTemporaryConfigActive)
      f.addChunk(Ep128Emu::File::EP128EMU_CHUNKTYPE_CONFIG_DB,
                 gui_.archiveOriginalConfigState);
    else
      gui_.config.saveState(f);
    const char  *fName = "tvc_cfg.dat";
    if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
      fName = "ep128cfg.dat";
    else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
      fName = "zx128cfg.dat";
    else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
      fName = "cpc_cfg.dat";
    f.writeFile(fName, true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_RevertCfg(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    const char  *fName = "tvc_cfg.dat";
    if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
      fName = "ep128cfg.dat";
    else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
      fName = "zx128cfg.dat";
    else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
      fName = "cpc_cfg.dat";
    Ep128Emu::File  f(fName, true);
    int fileMachineType = f.getMachineType();
    if (fileMachineType >= 0) {
      std::string restartFileName = ep128emuHomeFilePath(fName);
      if (!gui_.prepareMachineTypedLoad(
              fileMachineType, restartFileName, 3, false))
        return;
    }
    gui_.config.registerChunkType(f);
    f.processAllChunks();
    if (gui_.snapshotRuntimeMemoryActive)
      gui_.config.memoryConfigurationChanged = true;
    gui_.finishArchiveFileIOOverride(true, true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_QSFileName(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.snapshotDirectory,
                        "Snapshot files\t*.ep128s",
#ifdef WIN32
                        Fl_Native_File_Chooser::BROWSE_FILE,
#else
                        Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
#endif
                        "Select quick snapshot file")) {
      Ep128Emu::setFileNameExtension(tmp, "ep128s");
      gui_.quickSnapshotFileName = tmp;
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_QSLoad(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string fName(gui_.quickSnapshotFileName);
    bool useHomeDirectory = false;
    if (fName.empty()) {
      if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
        fName = "qs_ep128.dat";
      else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
        fName = "qs_zx128.dat";
      else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
        fName = "qs_cpc.dat";
      else
        fName = "qs_tvc.dat";
      useHomeDirectory = true;
    }

    Ep128Emu::File f(fName.c_str(), useHomeDirectory);
    int fileMachineType = f.getMachineType();
    if (fileMachineType < 0)
      throw Ep128Emu::Exception(
          "quick snapshot does not contain a supported machine state");

    std::string restartFileName(fName);
    if (useHomeDirectory) {
      restartFileName = Ep128Emu::getEp128EmuHomeDirectory();
#ifdef WIN32
      restartFileName += '\\';
#else
      restartFileName += '/';
#endif
      restartFileName += fName;
    }
    if (!gui_.prepareMachineTypedLoad(
            fileMachineType, restartFileName, 1, false))
      return;

    if (gui_.lockVMThread()) {
      try {
        gui_.vm.registerChunkTypes(f);
        bool runtimeMemoryState = ep128emuFileHasRuntimeMemoryState(f);
        f.processAllChunks();
        if (runtimeMemoryState)
          gui_.setSnapshotRuntimeMemoryActive(true);
        gui_.vmThread.clearRewindHistory();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_QSSave(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        const char *expansion = ep128emuSpectrumExpansionName(gui_.vm);
        if (expansion[0])
          throw std::runtime_error(std::string("Snapshot unavailable while ")
                                   + expansion + " is active. Disable it first.");
        const char  *fName = gui_.quickSnapshotFileName.c_str();
        bool        useHomeDirectory = false;
        if (fName[0] == '\0') {
          if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
            fName = "qs_ep128.dat";
          else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
            fName = "qs_zx128.dat";
          else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
            fName = "qs_cpc.dat";
          else
            fName = "qs_tvc.dat";
          useHomeDirectory = true;
        }
        Ep128Emu::File  f;
        gui_.vm.saveState(f);
        f.writeFile(fName, useHomeDirectory);
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_SaveSnapshot(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    // Incompatible expansion snapshots must be rejected before Save As.
    if (!gui_.lockVMThread())
      throw std::runtime_error("Snapshot: VM is busy; please retry");
    const char *blockedBy = ep128emuSpectrumExpansionName(gui_.vm);
    gui_.unlockVMThread();
    if (blockedBy[0])
      throw std::runtime_error(std::string("Snapshot unavailable while ")
                               + blockedBy + " is active. Disable it first.");
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.snapshotDirectory, "Snapshot files\t*.ep128s",
                        Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                        "Save snapshot")) {
      Ep128Emu::setFileNameExtension(tmp, "ep128s");
      gui_.loadFileDirectory = gui_.snapshotDirectory;
      if (gui_.lockVMThread()) {
        try {
          Ep128Emu::File  f;
          gui_.vm.saveState(f);
          gui_.writeFile(f, tmp.c_str());
        }
        catch (...) {
          gui_.unlockVMThread();
          throw;
        }
        gui_.unlockVMThread();
      }
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_RecordDemo(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.demoDirectory, "Demo files\t*.ep128d",
                        Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                        "Record demo")) {
      Ep128Emu::setFileNameExtension(tmp, "ep128d");
      gui_.loadFileDirectory = gui_.demoDirectory;
      if (gui_.lockVMThread()) {
        if (gui_.closeDemoFile(true)) {
          try {
            gui_.demoRecordFile = new Ep128Emu::File();
            gui_.demoRecordFileName = tmp;
            gui_.vm.recordDemo(*(gui_.demoRecordFile));
          }
          catch (...) {
            gui_.unlockVMThread();
            if (gui_.demoRecordFile) {
              delete gui_.demoRecordFile;
              gui_.demoRecordFile = (Ep128Emu::File *) 0;
            }
            gui_.demoRecordFileName.clear();
            throw;
          }
        }
        gui_.unlockVMThread();
      }
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_StopDemo(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.stopDemo();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_RecordSound(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseAudioOutputFile(tmp)) {
      gui_.config["sound.file"] = tmp;
      gui_.applyEmulatorConfiguration(true);
      gui_.syncRecordingStatusState();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_StopSndRecord(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["sound.file"] = std::string("");
    gui_.applyEmulatorConfiguration(true);
    gui_.syncRecordingStatusState();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_RecordVideo(Fl_Widget *o, void *v)
{
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    int format = gui_.config.videoCapture.format;
    if (format < 0 || format > 3)
      format = 0;
    bool modernFormat = (format == 0 || format == 1);
    // Fail before selecting a filename/starting an AVI capture that cannot
    // be finalized as MP4: older Win32/Win7 packages lack the FFmpeg helper.
    // AVI capture remains available independently of FFmpeg.
    if (modernFormat && findFFmpegExecutable().empty()) {
      gui_.errorMessage("MP4 export requires ep128emu-ffmpeg.exe next to "
                        "the emulator. Choose an AVI video recording "
                        "format instead; no recording was started.");
      return;
    }
    const char *pattern = (modernFormat ? "MP4 files\t*.mp4"
                                        : "AVI files\t*.avi");
    const char *title = "Record video output";
    if (format == 0)
      title = "Record video output to H.264 Compatible MP4";
    else if (format == 1)
      title = "Record video output to H.264 Pixel Graphics MP4";
    else
      title = "Record video output to AVI file";

    std::string tmp;
    if (!gui_.browseFile(tmp, gui_.soundFileDirectory, pattern,
                         Fl_Native_File_Chooser::BROWSE_SAVE_FILE, title)) {
      return;
    }
    if (tmp.length() < 1) {
      gui_.menuCallback_File_StopAVIRecord(o, v);
      return;
    }

    std::string captureFileName;
    std::string outputFileName;
    if (modernFormat) {
      Ep128Emu::setFileNameExtension(tmp, "mp4");
      outputFileName = tmp;
      captureFileName = tmp + ".ep128emu-tmp.avi";
      (void) std::remove(captureFileName.c_str());
    }
    else {
      Ep128Emu::setFileNameExtension(tmp, "avi");
      captureFileName = tmp;
    }

    if (gui_.lockVMThread()) {
      try {
        gui_.vm.openVideoCapture(gui_.config.videoCapture.frameRate,
                                 (format == 3),
                                 &Ep128EmuGUI::errorMessageCallback,
                                 &Ep128EmuGUI::videoFileNameCallback, v,
                                 (format == 1)); // Frame-complete Pixel Graphics
        gui_.vm.setVideoCaptureFile(captureFileName);
        gui_.videoCaptureOutputFileName = outputFileName;
        gui_.videoCaptureTempFileName =
            (modernFormat ? captureFileName : std::string(""));
        gui_.activeVideoCaptureFormat = format;
        gui_.getMenuItem(2).activate();         // "File/Record video/Stop"
        gui_.syncRecordingStatusState();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.menuCallback_File_StopAVIRecord(o, v);
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_StopAVIRecord(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  std::string outputFileName = gui_.videoCaptureOutputFileName;
  std::string tempFileName = gui_.videoCaptureTempFileName;
  int format = gui_.activeVideoCaptureFormat;
  gui_.videoCaptureOutputFileName = "";
  gui_.videoCaptureTempFileName = "";
  gui_.activeVideoCaptureFormat = -1;
  gui_.syncRecordingStatusState();
  try {
    if (gui_.lockVMThread()) {
      try {
        gui_.vm.closeVideoCapture();
        gui_.getMenuItem(2).deactivate();       // "File/Record video/Stop"
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }

    if ((format == 0 || format == 1) &&
        !outputFileName.empty() && !tempFileName.empty()) {
      gui_.mainWindow->label("Finalizing MP4...");
      gui_.mainWindow->cursor(FL_CURSOR_WAIT);
      Fl::redraw();
      Fl::wait(0.0);

      std::string conversionError;
      bool success = convertVideoCaptureToMP4(
          tempFileName, outputFileName, (format == 1), conversionError);
      if (success) {
        (void) std::remove(tempFileName.c_str());
      }
      else {
        (void) std::remove(outputFileName.c_str());
        conversionError += "\nTemporary file: " + tempFileName;
        gui_.errorMessage(conversionError.c_str());
      }

      gui_.mainWindow->cursor(FL_CURSOR_DEFAULT);
      gui_.updateDisplay_windowTitle();
      Fl::redraw();
    }
  }
  catch (std::exception& e) {
    gui_.mainWindow->cursor(FL_CURSOR_DEFAULT);
    gui_.updateDisplay_windowTitle();
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_File_Screenshot(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.flDisplay->setScreenshotCallback(&screenshotCallback, v);
}

void Ep128EmuGUI::menuCallback_File_ScreenshotAs(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.flDisplay->setScreenshotCallback(&screenshotAsCallback, v);
}

// Called on each graceful shutdown or host machine switch BEFORE asking the
// worker to quit. Only a writable native SDD requires additional I/O.
// The check holds the existing VMThread lock so no guest write can race it.
// A failed sync preserves the live drive and its dirty cache for retry.
bool Ep128EmuGUI::checkWritableSDDBeforeShutdown()
{
  // No mounted SDD means no host media can have pending native writes;
  // avoid acquiring VMThread for ordinary ZX/Enterprise quit or switch.
  if (ep128emuMachineType(vm) != 1 || speccyDOSMountedSDDPath.empty())
    return true;
  if (!lockVMThread())
    throw std::runtime_error("SpeccyDOS: VM is busy; shutdown cancelled");
  try {
    ZX128::ZX128VM& zx = static_cast<ZX128::ZX128VM&>(vm);
    ZX128::SpeccyDOSBus *bus = zx.getSpeccyDOSForTesting();
    if (bus) {
      Ep128Emu::FloppyDrive& disk = bus->getDrive(0);
      if (!ep128emuSDDCanGracefullyStop(disk))
        throw std::runtime_error(
            "SpeccyDOS: original SDD write/sync failed.\n"
            "Quit or machine switch cancelled; SDD remains mounted.\n"
            "Restore disk access and retry Eject or Quit.");
    }
  }
  catch (...) {
    unlockVMThread();
    throw;
  }
  unlockVMThread();
  return true;
}

void Ep128EmuGUI::menuCallback_File_Quit(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!gui_.checkWritableSDDBeforeShutdown())
      return;
    gui_.vmThread.quit(false);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::requestMachineRestart(int machineType)
{
  try {
    if (!checkWritableSDDBeforeShutdown())
      return;
  }
  catch (std::exception& e) {
    errorMessage(e.what());
    return;
  }
  restartMachineType = machineType;
  restartLoadType = 0;
  restartLoadFileName.clear();
  try {
    vmThread.quit(false);
  }
  catch (std::exception& e) {
    restartMachineType = -1;
    errorMessage(e.what());
  }
}

void Ep128EmuGUI::requestMachineRestartAndLoad(
    int machineType, int loadType, const std::string& fileName)
{
  try {
    if (!checkWritableSDDBeforeShutdown())
      return;
  }
  catch (std::exception& e) {
    errorMessage(e.what());
    return;
  }
  restartMachineType = machineType;
  restartLoadType = loadType;
  restartLoadFileName = fileName;
  try {
    vmThread.quit(false);
  }
  catch (std::exception& e) {
    restartMachineType = -1;
    restartLoadType = 0;
    restartLoadFileName.clear();
    errorMessage(e.what());
  }
}

bool Ep128EmuGUI::prepareMachineTypedLoad(
    int fileMachineType, const std::string& fileName, int loadType,
    bool addRecent)
{
  if (fileMachineType < 0)
    return true;
  if (fileMachineType > 3)
    throw Ep128Emu::Exception("unsupported machine type in file");

  int currentMachineType = ep128emuMachineType(vm);
  if (currentMachineType == fileMachineType)
    return true;
  if (currentMachineType < 0)
    throw Ep128Emu::Exception("cannot determine current machine type");

  char msg[512];
  std::snprintf(msg, sizeof(msg),
                "This file is for %s, but the current machine is %s.\n\n"
                "Switch to %s and load it?",
                Ep128Emu::File::getMachineTypeName(fileMachineType),
                Ep128Emu::File::getMachineTypeName(currentMachineType),
                Ep128Emu::File::getMachineTypeName(fileMachineType));
  int n = fl_choice("%s", "Cancel", "Switch && Load", (const char *) 0, msg);
  if (n == 1) {
    if (addRecent) {
      if (loadType == 1)
        addRecentFile(recentSnapshots, fileName);
      else if (loadType == 2)
        addRecentFile(recentConfigurations, fileName);
    }
    requestMachineRestartAndLoad(fileMachineType, loadType, fileName);
  }
  return false;
}

void Ep128EmuGUI::menuCallback_Machine_RewindCapture(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.rewindCaptureUserEnabled = !gui_.rewindCaptureUserEnabled;
  gui_.applyRewindHistoryLimit();
}

void Ep128EmuGUI::menuCallback_Machine_RewindHistory16(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.rewindHistoryLimitMiB = 16;
  gui_.applyRewindHistoryLimit();
}

void Ep128EmuGUI::menuCallback_Machine_RewindHistory32(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.rewindHistoryLimitMiB = 32;
  gui_.applyRewindHistoryLimit();
}

void Ep128EmuGUI::menuCallback_Machine_RewindHistory64(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.rewindHistoryLimitMiB = 64;
  gui_.applyRewindHistoryLimit();
}

void Ep128EmuGUI::menuCallback_Machine_RestartAsEP(Fl_Widget *o, void *v)
{
  (void) o;
  (reinterpret_cast<Ep128EmuGUI *>(v))->requestMachineRestart(0);
}

void Ep128EmuGUI::menuCallback_Machine_RestartAsZX(Fl_Widget *o, void *v)
{
  (void) o;
  (reinterpret_cast<Ep128EmuGUI *>(v))->requestMachineRestart(1);
}

void Ep128EmuGUI::menuCallback_Machine_RestartAsCPC(Fl_Widget *o, void *v)
{
  (void) o;
  (reinterpret_cast<Ep128EmuGUI *>(v))->requestMachineRestart(2);
}

void Ep128EmuGUI::menuCallback_Machine_RestartAsTVC(Fl_Widget *o, void *v)
{
  (void) o;
  (reinterpret_cast<Ep128EmuGUI *>(v))->requestMachineRestart(3);
}

void Ep128EmuGUI::menuCallback_Machine_Pause(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::VMThread::VMThreadStatus vmThreadStatus(gui_.vmThread);
    gui_.vmThread.pause(!vmThreadStatus.isPaused);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_NextFrame(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    // Pause is a frozen timeline browser: Next traverses only recorded future.
    // New execution starts exclusively through Resume/F10.
    (void) gui_.vmThread.advanceRecordedFrames(1);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_PreviousFrame(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!gui_.oldAudioRecordingFlag && !gui_.oldVideoRecordingFlag)
      (void) gui_.vmThread.rewindFrame();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Previous100(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!gui_.oldAudioRecordingFlag && !gui_.oldVideoRecordingFlag)
      (void) gui_.vmThread.rewindFrames(100);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Next100(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    (void) gui_.vmThread.advanceRecordedFrames(100);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_OldestFrame(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldAudioRecordingFlag || gui_.oldVideoRecordingFlag)
      return;
    Ep128Emu::VMThread::VMThreadStatus status(gui_.vmThread);
    if (status.isPaused && status.rewindFramesAvailable > 0)
      (void) gui_.vmThread.rewindFrames(status.rewindFramesAvailable);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_LatestFrame(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::VMThread::VMThreadStatus status(gui_.vmThread);
    if (status.isPaused &&
        status.rewindTimelineLength > status.rewindTimelinePosition) {
      (void) gui_.vmThread.advanceRecordedFrames(
          status.rewindTimelineLength - status.rewindTimelinePosition);
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_FullSpeed(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
        gui_.config["vm.speedPercentage"];
    if ((unsigned int) cv != 0U) {
      gui_.savedSpeedPercentage = (unsigned int) cv;
      cv = 0U;
    }
    else
      cv = gui_.savedSpeedPercentage;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_10(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 10U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_25(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 25U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_50(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 50U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_100(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 100U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_200(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 200U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Speed_400(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["vm.speedPercentage"] = 400U;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

#if defined(__APPLE__) || defined(WIN32)
// EF07H: optional bundled ROMs are resolved relative to the running .app.
// Original external ROM chooser remains for future firmware experiments.
// All device changes happen under the existing VM thread lock.
void Ep128EmuGUI::enableSpeccyDOSROM(const std::vector<uint8_t>& rom)
{
  if (!lockVMThread())
    throw std::runtime_error("SpeccyDOS: VM is busy; please retry");
  try {
    ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(vm);
    if (zx.getBetaDiskForTesting())
      throw std::runtime_error("SpeccyDOS: disable Beta Disk / TR-DOS first "
                               "(mutually exclusive expansions)");
    if (zx.getSpeccyDOSForTesting())
      throw std::runtime_error("SpeccyDOS: disable the active SpeccyDOS ROM "
                               "before loading another");
    if (!zx.installSpeccyDOSFirmware(rom))
      throw std::runtime_error("SpeccyDOS: invalid 8K ROM firmware");
    vmThread.clearRewindHistory();
  }
  catch (...) {unlockVMThread();throw;}
  unlockVMThread();
  speccyDOSMountedSDDPath.clear();
  updateDisplay_windowTitle();
  updateMenu();
  if (showInformationalDialogs)
    fl_message("SpeccyDOS ROM enabled; Spectrum cold-reset complete.\n"
             "Load an SDD when ready. Snapshot/rewind remain disabled\n"
             "while the expansion is active.");
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSBundled1770(Fl_Widget *o,void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {g.enableSpeccyDOSROM(Ep128EmuSpeccyTrial::readBundledROM(false));}
  catch (std::exception& e) {g.errorMessage(e.what());}
}
void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSBundled1772(Fl_Widget *o,void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {g.enableSpeccyDOSROM(Ep128EmuSpeccyTrial::readBundledROM(true));}
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSROM(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& g = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string path;
    if (!g.browseFile(path, g.romImageDirectory, "ROM files\t*.rom",
                      Fl_Native_File_Chooser::BROWSE_FILE,
                      "Select original SpeccyDOS 8K firmware ROM")) return;
    g.enableSpeccyDOSROM(Ep128EmuSpeccyTrial::readROM(path));
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSDisk(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string path;
    if (!g.browseFile(path,g.diskImageDirectory,"SpeccyDOS SDD images\t*.sdd",
                      Fl_Native_File_Chooser::BROWSE_FILE,
                      "Insert existing SDD into drive A")) return;
    if (!g.lockVMThread())
      throw std::runtime_error("SpeccyDOS: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::SpeccyDOSBus* dos=zx.getSpeccyDOSForTesting();
      if (!dos || !g.speccyDOSMountedSDDPath.empty())
        throw std::runtime_error("SpeccyDOS: enable ROM and eject existing disk first");
      Ep128Emu::FloppyDrive& drive=dos->getDrive(0);
      // Default is RW; OS-level read-only image falls back to native RO.
      try { drive.setDiskImageFile(path,80,2,16,256,false,true); }
      catch (...) { drive.setDiskImageFile(path,80,2,16,256,true); }
      drive.setUserWriteProtected(g.speccyDOSWriteProtect);
      g.vmThread.clearRewindHistory();
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.speccyDOSMountedSDDPath=path;
    g.updateMenu();
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSWriteProtect(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!g.lockVMThread())
      throw std::runtime_error("SpeccyDOS: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::SpeccyDOSBus* dos=zx.getSpeccyDOSForTesting();
      if (!dos) throw std::runtime_error("SpeccyDOS: enable ROM first");
      Ep128Emu::FloppyDrive& drive=dos->getDrive(0);
      if (!g.speccyDOSMountedSDDPath.empty() && !drive.isNativeSDDWritable())
        throw std::runtime_error("SpeccyDOS: file is read-only; eject to change mode");
      g.speccyDOSWriteProtect=!g.speccyDOSWriteProtect;
      if (!g.speccyDOSMountedSDDPath.empty())
        drive.setUserWriteProtected(g.speccyDOSWriteProtect);
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.updateMenu();
  }
  catch (std::exception& e) {g.updateMenu();g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSEject(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!g.lockVMThread())
      throw std::runtime_error("SpeccyDOS: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      if (zx.getSpeccyDOSForTesting()) {
        Ep128Emu::FloppyDrive& drive=zx.getSpeccyDOSForTesting()->getDrive(0);
        if (!drive.syncNativeSDD())
          throw std::runtime_error("SpeccyDOS: original SDD write/sync failed; eject cancelled");
        drive.setDiskImageFile("",80,2,16,256);
        g.vmThread.clearRewindHistory();
      }
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.speccyDOSMountedSDDPath.clear();
    g.updateMenu();
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_SpeccyDOSDisable(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (!g.lockVMThread())
      throw std::runtime_error("SpeccyDOS: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      if (zx.getSpeccyDOSForTesting() &&
          !zx.getSpeccyDOSForTesting()->getDrive(0).syncNativeSDD())
        throw std::runtime_error("SpeccyDOS: original SDD write/sync failed; disable cancelled");
      zx.removeSpeccyDOS();
      g.vmThread.clearRewindHistory();
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.speccyDOSMountedSDDPath.clear();
    g.updateDisplay_windowTitle();
    g.updateMenu();
    if (g.showInformationalDialogs)
      fl_message("SpeccyDOS disabled. Normal Spectrum mode restored;\n"
               "Snapshots are available again; rewind needs newly recorded "
               "frames at normal speed before Pause.");
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}
// EF08K: private, external-only Beta Disk. The VM owns the controller and
// original host firmware bytes; the GUI owns only an unlinkable trial copy.
void Ep128EmuGUI::enableBetaDiskROM(const std::vector<uint8_t>& rom)
{
  if (!lockVMThread())
    throw std::runtime_error("Beta Disk: VM is busy; please retry");
  try {
    ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(vm);
    if (zx.getSpeccyDOSForTesting())
      throw std::runtime_error("Beta Disk: disable SpeccyDOS first (mutually exclusive)");
    if (zx.getBetaDiskForTesting())
      throw std::runtime_error("Beta Disk: disable the existing ROM before loading another");
    if (!zx.installBetaDiskFirmware(rom))
      throw std::runtime_error("Beta Disk: invalid 16K firmware or conflicting expansion");
    vmThread.clearRewindHistory();
  }
  catch (...) {unlockVMThread();throw;}
  unlockVMThread();
  updateDisplay_windowTitle();
  updateMenu();
  if (showInformationalDialogs)
    fl_message("Beta Disk enabled; Spectrum cold reset complete.\n"
             "Insert a read-only TRD in drive A, then enter TR-DOS.\n"
             "Snapshots, demos and rewind are unavailable while active.");
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskBundled503(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {g.enableBetaDiskROM(Ep128EmuBetaTrial::readBundledROM(503));}
  catch (std::exception& e) {g.errorMessage(e.what());}
}
void Ep128EmuGUI::menuCallback_Machine_BetaDiskBundled504(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {g.enableBetaDiskROM(Ep128EmuBetaTrial::readBundledROM(504));}
  catch (std::exception& e) {g.errorMessage(e.what());}
}
void Ep128EmuGUI::menuCallback_Machine_BetaDiskBundled604(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {g.enableBetaDiskROM(Ep128EmuBetaTrial::readBundledROM(604));}
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskROM(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string path;
    if (!g.browseFile(path,g.romImageDirectory,"Raw ROM files\t*.rom",
                      Fl_Native_File_Chooser::BROWSE_FILE,
                      "Select original Beta Disk/TR-DOS 16K raw ROM")) return;
    g.enableBetaDiskROM(Ep128EmuBetaTrial::readROM(path));
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

// The manual action and the reset auto-entry share the EXACT same ROM,
// K-prompt and cursor guard. Call only while the VM thread is locked.
static const char *ep128emuBetaDOSPromptError(ZX128::ZX128VM& zx)
{
  ZX128::BetaDiskBus *beta=zx.getBetaDiskForTesting();
  if (!beta) return "TR-DOS: enable a Beta Disk ROM first";
  if (beta->isROMSelected())
    return "TR-DOS: already executing the Beta ROM";
  const bool is128=(zx.getMemoryPage(1)==0x05 && zx.getMemoryPage(2)==0x02);
  const uint8_t romPage=zx.getMemoryPage(0);
  if ((is128 && romPage!=0x81) || (!is128 && romPage!=0x80))
    return "TR-DOS: select original 48 BASIC first (Spectrum 128 menu)";
  const uint16_t pc=zx.getProgramCounter();
  const uint16_t editLine=uint16_t(zx.readMemory(0x5C59,true)) |
                          uint16_t(zx.readMemory(0x5C5A,true)<<8);
  const uint16_t cursor=uint16_t(zx.readMemory(0x5C5B,true)) |
                        uint16_t(zx.readMemory(0x5C5C,true)<<8);
  const bool basicIdleROM=((pc>=0x1500 && pc<=0x17FF) ||
                           (pc>=0x10A0 && pc<=0x10C0));
  if (!basicIdleROM || editLine<0x5B00 || editLine>0xEFFF ||
      editLine!=cursor || zx.readMemory(editLine,true)!=0x0D ||
      (zx.readMemory(0x5C3B,true)&0xC0)!=0)
    return "TR-DOS: enter only from a blank Spectrum 48 BASIC K prompt. "
           "Press ENTER on an empty BASIC line and try again.";
  return (const char*)0;
}

// Only send original ZX48 BASIC keyboard tokens when guest is demonstrably
// idle at its K prompt. Never call setProgramCounter or forge a Z80 stack.
void Ep128EmuGUI::menuCallback_Machine_BetaDiskEnterDOS(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (typeid(g.vm)!=typeid(ZX128::ZX128VM))
      throw std::runtime_error("TR-DOS: switch to ZX Spectrum before entering DOS");
    if (g.betaDOSInputStep>=0 || g.archiveAutoRunPhase>=0)
      throw std::runtime_error("TR-DOS: another automated keyboard sequence is running");
    Ep128Emu::VMThread::VMThreadStatus status(g.vmThread);
    if (status.isPaused)
      throw std::runtime_error("TR-DOS: resume emulation before entering DOS");
    if (!g.lockVMThread())
      throw std::runtime_error("TR-DOS: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      const char *reason=ep128emuBetaDOSPromptError(zx);
      if (reason) throw std::runtime_error(reason);
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.cancelBetaDOSAutoEnter();
    g.betaDOSInputStep=0;
    g.betaDOSInputPressed=false;
    g.updateMenu();
    Fl::add_timeout(0.020,&Ep128EmuGUI::betaDOSInputCallback,(void *)&g);
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}
// A reset, a successful TRD insert, or enabling the preference arms the same
// bounded guest-state wait. No ROM/PC override, no forced 128->48 BASIC switch.
void Ep128EmuGUI::cancelBetaDOSAutoEnter()
{
  Fl::remove_timeout(&Ep128EmuGUI::betaDOSAutoEnterCallback, (void *)this);
  betaDOSAutoEnterPolls=0;
}

void Ep128EmuGUI::armBetaDOSAutoEnterIfReady()
{
  cancelBetaDOSAutoEnter();
  if (!betaDOSAutoEnterOnReset ||
      typeid(vm)!=typeid(ZX128::ZX128VM) ||
      betaDOSInputStep>=0 || archiveAutoRunPhase>=0)
    return;
  betaDOSAutoEnterPolls=1;
  Fl::add_timeout(0.30,&Ep128EmuGUI::betaDOSAutoEnterCallback,(void*)this);
}

void Ep128EmuGUI::betaDOSAutoEnterCallback(void *userData)
{
  Ep128EmuGUI *g=(Ep128EmuGUI*)userData;
  if (g) g->betaDOSAutoEnterTick();
}

void Ep128EmuGUI::betaDOSAutoEnterTick()
{
  if (betaDOSAutoEnterPolls<=0) return;
  if (!betaDOSAutoEnterOnReset ||
      typeid(vm)!=typeid(ZX128::ZX128VM) ||
      betaDOSInputStep>=0 || archiveAutoRunPhase>=0 ||
      betaDOSAutoEnterPolls>=100) {
    cancelBetaDOSAutoEnter();
    return;
  }
  Ep128Emu::VMThread::VMThreadStatus status(vmThread);
  if (status.isPaused) {cancelBetaDOSAutoEnter();return;}
  if (lockVMThread(50)) {
    bool mounted=false, prompt=false;
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(vm);
      ZX128::BetaDiskBus *b=zx.getBetaDiskForTesting();
      mounted=(b!=0 && b->isDiskMounted());
      prompt=mounted && (ep128emuBetaDOSPromptError(zx)==0);
    }
    catch (...) {unlockVMThread();cancelBetaDOSAutoEnter();return;}
    unlockVMThread();
    if (!mounted) {cancelBetaDOSAutoEnter();return;}
    if (prompt) {
      cancelBetaDOSAutoEnter();
      menuCallback_Machine_BetaDiskEnterDOS((Fl_Widget*)0,(void*)this);
      return;
    }
  }
  betaDOSAutoEnterPolls++;
  Fl::add_timeout(0.15,&Ep128EmuGUI::betaDOSAutoEnterCallback,(void*)this);
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskAutoEnter(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI*>(v));
  const Fl_Menu_Item *item=g.mainMenuBar->find_item(
      "Machine/Beta Disk (experimental)/Auto-enter TR-DOS after reset or TRD insert");
  g.betaDOSAutoEnterOnReset=(item && item->value()!=0);
  if (!g.betaDOSAutoEnterOnReset) g.cancelBetaDOSAutoEnter();
  else g.armBetaDOSAutoEnterIfReady();  // Turning ON with a disk already mounted.
  g.updateMenu();
}

void Ep128EmuGUI::betaDOSInputCallback(void *userData)
{
  Ep128EmuGUI *g=reinterpret_cast<Ep128EmuGUI *>(userData);
  if(g)g->betaDOSInputTick();
}
void Ep128EmuGUI::betaDOSInputTick()
{
  if(betaDOSInputStep<0)return;
  if(size_t(betaDOSInputStep)>=Ep128EmuBetaTrial::enterDOSKeyCount) {
    betaDOSInputStep=-1;
    betaDOSInputPressed=false;
    updateMenu();
    emulatorWindow->take_focus();
    return;
  }
  const Ep128EmuBetaTrial::GuestKey& k=
      Ep128EmuBetaTrial::enterDOSKeys[betaDOSInputStep];
  if(!betaDOSInputPressed) {
    vmThread.setKeyboardState(k.first,true);
    if(k.second!=255)vmThread.setKeyboardState(k.second,true);
    betaDOSInputPressed=true;
    Fl::add_timeout(0.080,&Ep128EmuGUI::betaDOSInputCallback,(void *)this);
  }
  else {
    vmThread.setKeyboardState(k.first,false);
    if(k.second!=255)vmThread.setKeyboardState(k.second,false);
    betaDOSInputPressed=false;
    ++betaDOSInputStep;
    Fl::add_timeout(0.060,&Ep128EmuGUI::betaDOSInputCallback,(void *)this);
  }
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskTRD(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (g.betaDOSInputStep>=0 || g.archiveAutoRunPhase>=0)
      throw std::runtime_error("Beta Disk: wait for pending keyboard action");
    std::string path;
    if (!g.browseFile(path,g.diskImageDirectory,"TR-DOS images\t*.trd",
                      Fl_Native_File_Chooser::BROWSE_FILE,
                      "Insert existing TRD into drive A")) return;
    if (!g.lockVMThread())
      throw std::runtime_error("Beta Disk: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::BetaDiskBus* beta=zx.getBetaDiskForTesting();
      if (!beta || beta->isDiskMounted())
        throw std::runtime_error("Beta Disk: enable ROM and eject existing disk first");
      const Ep128EmuBetaTrial::StagedTRD media=
          Ep128EmuBetaTrial::inspectTRDWritableExisting(path);
      std::vector<uint8_t> firmware(16384U);
      for (size_t i=0;i<firmware.size();++i)
        firmware[i]=beta->readROM(uint16_t(i));
      if (Ep128EmuBetaTrial::writableROMApproved(firmware)) {
        try { beta->mountWritableTRD(media.path,media.tracks,media.sides); }
        catch (...) { beta->mountProtectedExisting(media.path,media.tracks,media.sides); }
      }
      else beta->mountProtectedExisting(media.path,media.tracks,media.sides);
      beta->setWriteProtected(g.betaDiskWriteProtect || !beta->isWritableCopyMounted());
      g.vmThread.clearRewindHistory();
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.updateMenu();
    // A newly mounted TRD completes the user's ROM + disk setup.
    // Trigger only the existing bounded, guarded 48 BASIC K-prompt entry.
    g.armBetaDOSAutoEnterIfReady();
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskWriteProtect(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (g.betaDOSInputStep>=0 || g.archiveAutoRunPhase>=0)
      throw std::runtime_error("Beta Disk: wait for pending keyboard action");
    if (!g.lockVMThread())
      throw std::runtime_error("Beta Disk: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::BetaDiskBus* beta=zx.getBetaDiskForTesting();
      if (!beta) throw std::runtime_error("Beta Disk: enable ROM first");
      if (beta->isDiskMounted() && !beta->isWritableCopyMounted())
        throw std::runtime_error("Beta Disk: file or ROM is read-only; eject first");
      g.betaDiskWriteProtect=!g.betaDiskWriteProtect;
      if (beta->isDiskMounted())
        beta->setWriteProtected(g.betaDiskWriteProtect);
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    g.updateMenu();
  }
  catch (std::exception& e) {g.updateMenu();g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskEject(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if(g.betaDOSInputStep>=0)
      throw std::runtime_error("TR-DOS: wait until DOS entry finishes");
    if (!g.lockVMThread())
      throw std::runtime_error("Beta Disk: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::BetaDiskBus *beta=zx.getBetaDiskForTesting();
      if (!beta)
        throw std::runtime_error("Beta Disk: firmware is not enabled");
      beta->ejectReadOnly();
      g.vmThread.clearRewindHistory();
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    Ep128EmuBetaTrial::removeTemporary(g.betaDiskTemporaryDiskFile);
    g.betaDiskTemporaryDiskFile.clear();
    g.updateMenu();
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

void Ep128EmuGUI::menuCallback_Machine_BetaDiskDisable(Fl_Widget *o, void *v)
{
  (void)o;
  Ep128EmuGUI& g=*(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if(g.betaDOSInputStep>=0)
      throw std::runtime_error("TR-DOS: wait until DOS entry finishes");
    if (!g.lockVMThread())
      throw std::runtime_error("Beta Disk: VM is busy; please retry");
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      if (!zx.getBetaDiskForTesting())
        throw std::runtime_error("Beta Disk: firmware is not enabled");
      ZX128::BetaDiskBus *beta=zx.getBetaDiskForTesting();
      if (beta->isWritableCopyMounted()) beta->ejectReadOnly();
      zx.removeBetaDisk();  // only after private output is durably ejected
      g.vmThread.clearRewindHistory();
    }
    catch (...) {g.unlockVMThread();throw;}
    g.unlockVMThread();
    Ep128EmuBetaTrial::removeTemporary(g.betaDiskTemporaryDiskFile);
    g.betaDiskTemporaryDiskFile.clear();
    g.updateDisplay_windowTitle();
    g.updateMenu();
    if (g.showInformationalDialogs)
      fl_message("Beta Disk disabled and Spectrum cold reset complete.\n"
               "Snapshots are available again; rewind needs newly recorded "
               "frames at normal speed before Pause.");
  }
  catch (std::exception& e) {g.errorMessage(e.what());}
}

#endif // __APPLE__ || WIN32

void Ep128EmuGUI::menuCallback_Machine_OpenTape(Fl_Widget *o, void *v)
{
#ifdef WIN32
  ep128emuWindowsGuiTrace("CALLBACK-ALT-T-TAPE", -1, 0, 0);
#endif
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    std::string tmp;
    if (gui_.browseFile(tmp, gui_.tapeImageDirectory,
                        "Tape files\t*.{tap,wav,aif,aiff,au,snd,tzx,cdt}",
                        Fl_Native_File_Chooser::BROWSE_FILE,
                        "Select tape image file")) {
      gui_.openTapeImageFile(tmp, true);
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

unsigned int Ep128EmuGUI::getEffectiveTapeMotorSpinUpDelayMs() const
{
  // Use the selected speed, not measured FPS (which fluctuates).
  // Archive Drag & Run temporarily overrides VMThread speed to No limit.
  if (config.vm.speedPercentage != 100U || archiveAutoRunSpeedOverrideActive)
    return 0U;
  return uint32_t(tapeMotorSpinUpDelayMs);
}

void Ep128EmuGUI::setTapeMotorSpinUpDelay(int ms)
{
  if (typeid(vm) != typeid(Ep128::Ep128VM))
    return;
  if (ms == tapeMotorSpinUpDelayMs)
    return;
  if (!lockVMThread())
    return;
  try {
    tapeMotorSpinUpDelayMs = ms;
    static_cast<Ep128::Ep128VM&>(vm).setTapeMotorSpinUpDelayMs(
        getEffectiveTapeMotorSpinUpDelayMs());
    // Rewind history is no longer valid after changing host tape timing.
    vmThread.clearRewindHistory();
  }
  catch (...) {
    unlockVMThread();
    throw;
  }
  unlockVMThread();
  updateMenu();
}

void Ep128EmuGUI::menuCallback_Machine_TapeSpin0(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setTapeMotorSpinUpDelay(0);
}

void Ep128EmuGUI::menuCallback_Machine_TapeSpin500(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setTapeMotorSpinUpDelay(500);
}

void Ep128EmuGUI::menuCallback_Machine_TapeSpin1000(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setTapeMotorSpinUpDelay(1000);
}

void Ep128EmuGUI::menuCallback_Machine_TapeSpin1500(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setTapeMotorSpinUpDelay(1500);
}

void Ep128EmuGUI::menuCallback_Machine_TapeForce(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["tape.forceMotorOn"] = !gui_.config.tape.forceMotorOn;
    gui_.applyEmulatorConfiguration(true);
    gui_.updateMenu();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::closePrinterOutputFile()
{
  if (printerOutputFile) {
    std::fflush(printerOutputFile);
    std::fclose(printerOutputFile);
    printerOutputFile = (std::FILE *) 0;
  }
}

void Ep128EmuGUI::pollPrinterOutput()
{
  // On a machine restart the old Enterprise GUI is still processing FLTK
  // callbacks after VMThread::quit(false), including nested error dialogs.
  // Do not poll its printer once teardown has started.
  // FileIO/TASMON F1 invokes the native chooser FROM the VM thread. The
  // VM cannot acknowledge a second GUI-side lock until that picker returns.
  // Keep captured printer data queued and resume polling immediately after.
  if (exitFlag || guestFileIOPickerActive.load(std::memory_order_acquire) ||
      typeid(vm) != typeid(Ep128::Ep128VM) || !printerCaptureEnabled)
    return;
  // The guest FileIO callback can enter its native chooser AFTER the atomic
  // check above, while this GUI thread already holds the FLTK lock. Waiting
  // two seconds in lockVMThread() then deadlocks the chooser against FLTK
  // and produces a false VM-unresponsive modal error. Printer capture is a
  // best-effort periodic drain: bound each attempt to 10 ms, silently defer
  // on contention, and leave all queued bytes untouched for the next poll.
  // Do not weaken normal explicit GUI operations' VM lock/error handling.
  if (vmThread.lock(10) != 0)
    return;
  std::string bytes;
  uint64_t overflows = 0U;
  Ep128::Ep128VM& ep = static_cast<Ep128::Ep128VM&>(vm);
  ep.drainPrinterOutput(bytes);
  overflows = ep.getPrinterOverflowCount();
  unlockVMThread();
  if (bytes.empty() && overflows == printerOverflowReported)
    return;
  if (!bytes.empty() && !printerOutputFile) {
    // FLTK macOS native Save dialog does not expose a reliable selected
    // filter index (FLTK 1.4 API explicitly documents the limitation).
    // Make extension an explicit persistent menu preference, not a guessed
    // property of the chooser's active file filter.
    const bool raw = (printerOutputFormat == 1);
    const char *ext = (raw ? "raw" : "txt");
    std::string name(raw ? "ep128-printer.raw" : "ep128-printer.txt");
    if (!browseFile(name, printerOutputDirectory,
                    raw ? "RAW printer bytes\t*.raw" : "Text files\t*.txt",
                    Fl_Native_File_Chooser::BROWSE_SAVE_FILE,
                    raw ? "Save RAW printer bytes (.raw)"
                        : "Save printer text bytes (.txt)")) {
      // Cancel means ignore this print job; virtual READY remains asserted
      // so guest software never waits indefinitely for an absent printer.
      printerCaptureEnabled = false;
      if (lockVMThread()) {
        ep.setPrinterCaptureEnabled(false);
        unlockVMThread();
      }
      updateMenu();
      return;
    }
    const std::string chooserName(name);
    Ep128Emu::setFileNameExtension(name, ext);
    // The system's Save-as overwrite confirmation may refer to the name
    // *before* suffix normalization, as in the PNG screenshot workflow.
    // Never silently truncate a differently named existing destination.
    if (name != chooserName) {
      std::FILE *existing = fl_fopen(name.c_str(), "rb");
      if (existing) {
        std::fclose(existing);
        if (fl_choice("Replace the existing printer output file?\n%s",
                      "Cancel", "Replace", 0, name.c_str()) != 1) {
          // The already drained first bytes are intentionally discarded, as
          // when the native chooser is canceled. Never silently overwrite.
          printerCaptureEnabled = false;
          if (lockVMThread()) {
            ep.setPrinterCaptureEnabled(false);
            unlockVMThread();
          }
          updateMenu();
          return;
        }
      }
    }
    printerOutputFile = fl_fopen(name.c_str(), "wb");
    if (!printerOutputFile) {
      std::string msg("Cannot open printer output file: ");
      msg += name;
      printerCaptureEnabled = false;
      if (lockVMThread()) {
        ep.setPrinterCaptureEnabled(false);
        unlockVMThread();
      }
      updateMenu();
      errorMessage(msg.c_str());
      return;
    }
  }
  if (printerOutputFile && !bytes.empty()) {
    if (std::fwrite(bytes.data(), 1, bytes.size(), printerOutputFile) !=
          bytes.size() || std::fflush(printerOutputFile) != 0) {
      closePrinterOutputFile();
      printerCaptureEnabled = false;
      if (lockVMThread()) {
        ep.setPrinterCaptureEnabled(false);
        unlockVMThread();
      }
      updateMenu();
      errorMessage("Printer output write failed; capture disabled.");
      return;
    }
  }
  if (overflows != printerOverflowReported) {
    printerOverflowReported = overflows;
    errorMessage("Printer buffer overflow: some printer bytes were lost.");
  }
}

void Ep128EmuGUI::setPrinterOutputFormat(int fmt)
{
  if (printerOutputFormat == fmt)
    return;
  if (printerOutputFile)
    pollPrinterOutput();   // flush the old file before switching formats
  closePrinterOutputFile();
  printerOutputFormat = fmt;
  updateMenu();
}

void Ep128EmuGUI::menuCallback_Machine_PrinterFormatText(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setPrinterOutputFormat(0);
}

void Ep128EmuGUI::menuCallback_Machine_PrinterFormatRaw(Fl_Widget *, void *v)
{
  reinterpret_cast<Ep128EmuGUI *>(v)->setPrinterOutputFormat(1);
}

void Ep128EmuGUI::menuCallback_Machine_PrinterCapture(Fl_Widget *, void *v)
{
  Ep128EmuGUI& gui_ = *reinterpret_cast<Ep128EmuGUI *>(v);
  const Fl_Menu_Item *m =
      gui_.mainMenuBar->find_item("Machine/Printer/Capture printer output");
  const bool enable = (m && m->value() != 0);
  if (!enable)
    gui_.closePrinterOutputFile();
  gui_.printerCaptureEnabled = enable;
  if (gui_.lockVMThread()) {
    static_cast<Ep128::Ep128VM&>(gui_.vm).setPrinterCaptureEnabled(enable);
    gui_.unlockVMThread();
  }
  gui_.updateMenu();
}

void Ep128EmuGUI::menuCallback_Machine_PrinterNewFile(Fl_Widget *, void *v)
{
  Ep128EmuGUI& gui_ = *reinterpret_cast<Ep128EmuGUI *>(v);
  gui_.pollPrinterOutput(); // preserve all previously acknowledged bytes
  gui_.closePrinterOutputFile();
  gui_.printerCaptureEnabled = true;
  if (gui_.lockVMThread()) {
    static_cast<Ep128::Ep128VM&>(gui_.vm).setPrinterCaptureEnabled(true);
    gui_.unlockVMThread();
  }
  gui_.updateMenu();
}

void Ep128EmuGUI::menuCallback_Machine_TapePlay(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.tapePlay();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
    return;
  }
  gui_.tapeButtonState = 1;
}

void Ep128EmuGUI::menuCallback_Machine_TapeStop(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.tapeStop();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
    return;
  }
  gui_.tapeButtonState = 0;
}

void Ep128EmuGUI::menuCallback_Machine_TapeRecord(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.tapeRecord();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
    return;
  }
  gui_.tapeButtonState = 2;
}

void Ep128EmuGUI::menuCallback_Machine_TapeRewind(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.tapeSeek(0.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapePrvCP(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.tapeSeekToCuePoint(false, 30.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeBwd10s(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldTapePosition >= 0L)
      gui_.vmThread.tapeSeek(double(gui_.oldTapePosition) / 10.0 - 10.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeBwd60s(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldTapePosition >= 0L)
      gui_.vmThread.tapeSeek(double(gui_.oldTapePosition) / 10.0 - 60.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeNxtCP(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldTapePosition >= 0L)
    gui_.vmThread.tapeSeekToCuePoint(true, 30.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeFwd10s(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldTapePosition >= 0L)
      gui_.vmThread.tapeSeek(double(gui_.oldTapePosition) / 10.0 + 10.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeFwd60s(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.oldTapePosition >= 0L)
      gui_.vmThread.tapeSeek(double(gui_.oldTapePosition) / 10.0 + 60.0);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_AddCP(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        gui_.vm.tapeAddCuePoint();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_DeleteCP(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        gui_.vm.tapeDeleteNearestCuePoint();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_DeleteCPs(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        gui_.vm.tapeDeleteAllCuePoints();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_TapeClose(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["tape.imageFile"] = std::string("");
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_Reset(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.reset(false);
    gui_.armBetaDOSAutoEnterIfReady();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_ColdReset(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.vmThread.reset(true);
    gui_.armBetaDOSAutoEnterIfReady();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_ResetFreqs(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config.vmConfigurationChanged = true;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_ResetAll(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.cancelBetaDOSAutoEnter();
    gui_.finishArchiveFileIOOverride(false, false);
    bool resetCompleted = false;
    if (gui_.lockVMThread()) {
      try {
        gui_.vm.stopDemo();
        gui_.config.vmConfigurationChanged = true;
        gui_.config.memoryConfigurationChanged = true;
        gui_.config.applySettings();
        gui_.vm.reset(true);
        resetCompleted = true;
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
    // GUI window refresh must happen after the VM lock is released.
    if (resetCompleted)
      gui_.setSnapshotRuntimeMemoryActive(false);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_QuickCfgL1(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    const char  *fName = "tvvmcfg1.cfg";
    if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
      fName = "epvmcfg1.cfg";
    else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
      fName = "zxvmcfg1.cfg";
    else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
      fName = "cpvmcfg1.cfg";
    int fileMachineType =
        Ep128Emu::ConfigurationDB::getMachineTypeMetadata(fName, true);
    std::string restartFileName = ep128emuHomeFilePath(fName);
    if (!gui_.prepareMachineTypedLoad(
            fileMachineType, restartFileName, 2, false))
      return;
    gui_.config.loadState(fName, true);
    if (gui_.snapshotRuntimeMemoryActive)
      gui_.config.memoryConfigurationChanged = true;
    gui_.finishArchiveFileIOOverride(true, true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_QuickCfgL2(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    const char  *fName = "tvvmcfg2.cfg";
    if (typeid(gui_.vm) == typeid(Ep128::Ep128VM))
      fName = "epvmcfg2.cfg";
    else if (typeid(gui_.vm) == typeid(ZX128::ZX128VM))
      fName = "zxvmcfg2.cfg";
    else if (typeid(gui_.vm) == typeid(CPC464::CPC464VM))
      fName = "cpvmcfg2.cfg";
    int fileMachineType =
        Ep128Emu::ConfigurationDB::getMachineTypeMetadata(fName, true);
    std::string restartFileName = ep128emuHomeFilePath(fName);
    if (!gui_.prepareMachineTypedLoad(
            fileMachineType, restartFileName, 2, false))
      return;
    gui_.config.loadState(fName, true);
    if (gui_.snapshotRuntimeMemoryActive)
      gui_.config.memoryConfigurationChanged = true;
    gui_.finishArchiveFileIOOverride(true, true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Machine_QuickCfgS1(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.saveQuickConfig(1);
}

void Ep128EmuGUI::menuCallback_Machine_QuickCfgS2(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.saveQuickConfig(2);
}

void Ep128EmuGUI::menuCallback_Machine_Configure(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.machineConfigWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_DpySize1(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  if (gui_.pixelPerfectEnabled) {
    gui_.pixelPerfectEnabled = false;
    gui_.applyPixelPerfectDisplayState();
  }
  gui_.resizeWindow(384, 288);
}

void Ep128EmuGUI::menuCallback_Options_DpySize2(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  if (gui_.pixelPerfectEnabled) {
    gui_.pixelPerfectEnabled = false;
    gui_.applyPixelPerfectDisplayState();
  }
  gui_.resizeWindow(768, 576);
}

void Ep128EmuGUI::menuCallback_Options_DpySize3(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  if (gui_.pixelPerfectEnabled) {
    gui_.pixelPerfectEnabled = false;
    gui_.applyPixelPerfectDisplayState();
  }
  gui_.resizeWindow(1152, 864);
}

void Ep128EmuGUI::menuCallback_Options_DpyMode(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  switch (gui_.displayMode) {
  case 0:
    gui_.displayMode = 2;
    break;
  case 2:
    gui_.displayMode = 3;
    break;
  case 3:
    gui_.displayMode = 1;
    break;
  default:
    gui_.displayMode = 0;
    break;
  }
}

void Ep128EmuGUI::menuCallback_Options_DpyPixelPerfect(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.pixelPerfectEnabled = true;
  gui_.applyPixelPerfectDisplayState();
}

void Ep128EmuGUI::menuCallback_Options_MenuSizeStandard(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.largeMenuEnabled = false;
  gui_.applyMenuSize();
}

void Ep128EmuGUI::menuCallback_Options_MenuSizeLarge(
    Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.largeMenuEnabled = true;
  gui_.applyMenuSize();
}

void Ep128EmuGUI::menuCallback_Options_DpyConfig(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.displaySettingsWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_RuntimeHUD(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  const Fl_Menu_Item *m =
      gui_.mainMenuBar->find_item("Options/Display/Runtime status display");
  gui_.runtimeHUDEnabled = (m && m->value() != 0);
  Ep128Emu::VMThread::VMThreadStatus status(gui_.vmThread);
  gui_.updateRuntimeStatusDisplay(status);
  gui_.updateRecordingStatusDisplay();
  gui_.updateDisplay_windowTitle();
  gui_.mainWindow->redraw();
}

void Ep128EmuGUI::menuCallback_Options_InformationDialogs(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  const Fl_Menu_Item *item = gui_.mainMenuBar->find_item(
      "Options/Notifications/Show informational dialogs");
  gui_.showInformationalDialogs = (item && item->value() != 0);
  gui_.updateMenu();
}

void Ep128EmuGUI::menuCallback_Options_TapeRelayClick(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  const Fl_Menu_Item *m =
      gui_.mainMenuBar->find_item("Options/Sound/Tape remote relay click");
  gui_.tapeRelayClickEnabled = (m && m->value() != 0);
}

void Ep128EmuGUI::menuCallback_Options_SndIncVol(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
        gui_.config["sound.volume"];
    cv = double(cv) * 1.2589;
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_SndDecVol(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
        gui_.config["sound.volume"];
    cv = double(cv) * 0.7943;
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_SndConfig(Fl_Widget *o, void *v)
{
#ifdef WIN32
  ep128emuWindowsGuiTrace("CALLBACK-ALT-U-SOUND", -1, 0, 0);
#endif
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.soundSettingsWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_FloppyRmA(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["floppy.a.imageFile"] = "";
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRmB(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["floppy.b.imageFile"] = "";
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRmC(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["floppy.c.imageFile"] = "";
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRmD(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["floppy.d.imageFile"] = "";
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRmv(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    gui_.config["floppy.a.imageFile"] = "";
    gui_.config["floppy.b.imageFile"] = "";
    gui_.config["floppy.c.imageFile"] = "";
    gui_.config["floppy.d.imageFile"] = "";
    gui_.applyEmulatorConfiguration(true);
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRpA(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
            gui_.config["floppy.a.imageFile"];
        std::string tmp = std::string(cv);
        cv = "";
        gui_.applyEmulatorConfiguration();
        cv = tmp;
        gui_.applyEmulatorConfiguration();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRpB(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
            gui_.config["floppy.b.imageFile"];
        std::string tmp = std::string(cv);
        cv = "";
        gui_.applyEmulatorConfiguration();
        cv = tmp;
        gui_.applyEmulatorConfiguration();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRpC(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
            gui_.config["floppy.c.imageFile"];
        std::string tmp = std::string(cv);
        cv = "";
        gui_.applyEmulatorConfiguration();
        cv = tmp;
        gui_.applyEmulatorConfiguration();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRpD(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
            gui_.config["floppy.d.imageFile"];
        std::string tmp = std::string(cv);
        cv = "";
        gui_.applyEmulatorConfiguration();
        cv = tmp;
        gui_.applyEmulatorConfiguration();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyRpl(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    if (gui_.lockVMThread()) {
      try {
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cvA =
            gui_.config["floppy.a.imageFile"];
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cvB =
            gui_.config["floppy.b.imageFile"];
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cvC =
            gui_.config["floppy.c.imageFile"];
        Ep128Emu::ConfigurationDB::ConfigurationVariable& cvD =
            gui_.config["floppy.d.imageFile"];
        std::string tmpA = std::string(cvA);
        std::string tmpB = std::string(cvB);
        std::string tmpC = std::string(cvC);
        std::string tmpD = std::string(cvD);
        cvA = "";
        cvB = "";
        cvC = "";
        cvD = "";
        gui_.applyEmulatorConfiguration();
        cvA = tmpA;
        cvB = tmpB;
        cvC = tmpC;
        cvD = tmpD;
        gui_.applyEmulatorConfiguration();
      }
      catch (...) {
        gui_.unlockVMThread();
        throw;
      }
      gui_.unlockVMThread();
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_FloppyCfg(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  if (typeid(gui_.vm) != typeid(ZX128::ZX128VM))
    gui_.diskConfigWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_PPriority(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  int     n = -2;
  for (int i = 4; i <= 8; i++) {
    if (gui_.getMenuItem(i).value() != 0)
      n = i - 6;
  }
  try {
    gui_.config["vm.processPriority"] = n;
    gui_.applyEmulatorConfiguration();
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Options_AppShortcuts(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI& gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.shortcutConfigWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_KbdConfig(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.keyboardConfigWindow->show();
}

void Ep128EmuGUI::menuCallback_Options_FileIODir(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  try {
    Ep128Emu::ConfigurationDB::ConfigurationVariable& cv =
        gui_.config["fileio.workingDirectory"];
    std::string tmp;
    std::string tmp2 = std::string(cv);
    if (gui_.browseFile(tmp, tmp2, "*",
                        Fl_Native_File_Chooser::BROWSE_DIRECTORY,
                        "Select working directory for the emulated machine")) {
      // An explicit FileIO directory selection ends an archive runtime
      // override.  If Standard 128K is active, first return to the user's
      // persistent machine configuration so this explicit user change is not
      // discarded by the session-only persistence guard on exit.
      if (gui_.archiveTemporaryConfigActive)
        gui_.rollbackArchiveTemporaryConfiguration();
      cv = tmp;
      gui_.finishArchiveFileIOOverride(false, false);
    }
  }
  catch (std::exception& e) {
    gui_.errorMessage(e.what());
  }
}

void Ep128EmuGUI::menuCallback_Debug_OpenDebugger(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  if (!gui_.debugWindow->active()) {
    if (gui_.debugWindowShowFlag)
      return;
    if (!gui_.lockVMThread())
      return;
    gui_.debugWindowOpenFlag = true;
  }
  gui_.debugWindow->show();
}

void Ep128EmuGUI::menuCallback_Help_About(Fl_Widget *o, void *v)
{
  (void) o;
  Ep128EmuGUI&  gui_ = *(reinterpret_cast<Ep128EmuGUI *>(v));
  gui_.aboutWindow->show();
}

