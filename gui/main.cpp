
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
#include "ep128vm.hpp"
#include "zx128vm.hpp"
#include "cpc464vm.hpp"
#include "tvc64vm.hpp"
#include "system.hpp"
#include "guicolor.hpp"
#ifdef __APPLE__
#  include "macos_activity.hpp"
#  include "macos_about.hpp"
#  include <FL/Fl_Sys_Menu_Bar.H>
#  include <mach-o/dyld.h>
#  include <spawn.h>
#endif
#include <vector>

#include <typeinfo>
#include <stdexcept>

#ifdef __APPLE__
extern char **environ;
#endif

#ifdef WIN32
#  include <windows.h>
#  include "win_trial_paths.hpp"
#else
#  include <dirent.h>
#endif

#ifdef __APPLE__
static void macOSAboutCallback(Fl_Widget *, void *)
{
  ep128emuShowMacOSAboutPanel();
}
#endif

#ifdef __APPLE__
static bool restartAsMacOSMachine(int machineType, int loadType,
                                  const std::string& loadFileName)
{
  static const char *machineArgs[4] = { "-ep128", "-zx", "-cpc", "-tvc" };
  static const char *loadArgs[4] = {
    (const char *) 0, "-snapshot", "-cfg", "-loadfile"
  };
  if (machineType < 0 || machineType > 3 || loadType < 0 || loadType > 3)
    return false;
  if (loadType != 0 && loadFileName.empty())
    return false;
  uint32_t pathSize = 0;
  (void) _NSGetExecutablePath((char *) 0, &pathSize);
  if (pathSize < 2)
    return false;
  std::vector<char> executablePath(size_t(pathSize) + 1, '\0');
  if (_NSGetExecutablePath(&(executablePath.front()), &pathSize) != 0)
    return false;
  char *childArgv[5];
  childArgv[0] = &(executablePath.front());
  childArgv[1] = const_cast<char *>(machineArgs[machineType]);
  if (loadType != 0) {
    childArgv[2] = const_cast<char *>(loadArgs[loadType]);
    childArgv[3] = const_cast<char *>(loadFileName.c_str());
    childArgv[4] = (char *) 0;
  }
  else {
    childArgv[2] = (char *) 0;
  }
  pid_t childPID = 0;
  return (posix_spawn(&childPID, childArgv[0], (posix_spawn_file_actions_t *) 0,
                      (posix_spawnattr_t *) 0, childArgv, environ) == 0);
}
#endif

#ifdef WIN32
// Spawn the next emulated machine only after the current VM/audio/GUI has
// been torn down. Use the current EXE, not PATH or a shell, and quote
// Windows Unicode paths for configurations, snapshots and binary files.
static std::wstring ep128emuWinQuoteArg(const std::wstring& arg)
{
  std::wstring result(L"\"");
  size_t backslashes = 0;
  for (size_t i = 0; i < arg.size(); i++) {
    wchar_t c = arg[i];
    if (c == L'\\') {
      backslashes++;
      continue;
    }
    if (c == L'"')
      result.append(backslashes * 2 + 1, L'\\');
    else
      result.append(backslashes, L'\\');
    backslashes = 0;
    result += c;
  }
  result.append(backslashes * 2, L'\\');
  result += L'"';
  return result;
}

static std::wstring ep128emuWinPathToWide(const std::string& path)
{
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                              path.c_str(), -1, (wchar_t *) 0, 0);
  UINT codePage = CP_UTF8;
  if (n <= 0) {
    // Some legacy FLTK paths may still be encoded in the system ACP.
    codePage = CP_ACP;
    n = MultiByteToWideChar(codePage, 0, path.c_str(), -1,
                            (wchar_t *) 0, 0);
  }
  if (n <= 0)
    return std::wstring();
  std::vector<wchar_t> buf(size_t(n), L'\0');
  if (!MultiByteToWideChar(codePage, 0, path.c_str(), -1, &buf[0], n))
    return std::wstring();
  return std::wstring(&buf[0]);
}

static bool restartAsWindowsMachine(int machineType, int loadType,
                                    const std::string& loadFileName)
{
  static const wchar_t *machineArgs[4] =
      { L"-ep128", L"-zx", L"-cpc", L"-tvc" };
  static const wchar_t *loadArgs[4] =
      { (const wchar_t *) 0, L"-snapshot", L"-cfg", L"-loadfile" };
  if (machineType < 0 || machineType > 3 || loadType < 0 || loadType > 3)
    return false;
  if (loadType != 0 && loadFileName.empty())
    return false;
  std::vector<wchar_t> executablePath(32768U, L'\0');
  DWORD n = GetModuleFileNameW((HMODULE) 0, &executablePath[0],
                               DWORD(executablePath.size()));
  if (n == 0 || size_t(n) >= executablePath.size())
    return false;
  std::wstring exe(&executablePath[0]);
  std::wstring cmd = ep128emuWinQuoteArg(exe) + L" " +
                     machineArgs[machineType];
  if (loadType != 0) {
    std::wstring filePath = ep128emuWinPathToWide(loadFileName);
    if (filePath.empty())
      return false;
    cmd += L" ";
    cmd += loadArgs[loadType];
    cmd += L" ";
    cmd += ep128emuWinQuoteArg(filePath);
  }
  STARTUPINFOW startup;
  PROCESS_INFORMATION child;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&child, sizeof(child));
  startup.cb = sizeof(startup);
  BOOL ok = CreateProcessW(exe.c_str(), &cmd[0],
                           (LPSECURITY_ATTRIBUTES) 0,
                           (LPSECURITY_ATTRIBUTES) 0,
                           FALSE, 0, (LPVOID) 0, (LPCWSTR) 0,
                           &startup, &child);
  if (!ok)
    return false;
  // The menu command that requested the machine switch had the user's
  // input focus. Explicitly allow its child to become foreground, even
  // when the previous instance closes before the next GUI is shown.
  (void) AllowSetForegroundWindow(child.dwProcessId);
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  return true;
}
#endif

#ifdef WIN32
// T004: Migrate only provably missing paths from known historical trial
// installations. Never rewrite existing user files or a custom location.
// The original binary config is backed up before any automatic correction.
static bool repairKnownWindowsTrialConfiguration(
    Ep128Emu::EmulatorConfiguration& cfg,
    int machineType, const char *configFileName,
    bool allowMissingFactoryROMs)
{
  const std::string root = Ep128EmuWinTrial::programDirectory();
  if (root.empty())
    return false;

  std::vector<std::string> romPaths(68);
  std::vector<int> romOffsets(68);
  bool romChanged = false;
  for (size_t i = 0; i < 68U; i++) {
    romPaths[i] = Ep128EmuWinTrial::relocateMissing(
        cfg.memory.rom[i].file, root);
    romOffsets[i] = cfg.memory.rom[i].offset;
    if (romPaths[i] != cfg.memory.rom[i].file)
      romChanged = true;
  }
  std::string memConfig =
      Ep128EmuWinTrial::relocateMissing(cfg.memory.configFile, root);
  std::string fileIODir = Ep128EmuWinTrial::relocateMissing(
      cfg.fileio.workingDirectory, root, true);
#ifdef ENABLE_SDEXT
  std::string sdROM =
      Ep128EmuWinTrial::relocateMissing(cfg.sdext.romFile, root);
#endif

  bool noROMs = true;
  for (size_t i = 0; i < 68U; i++) {
    if (!romPaths[i].empty()) {
      noROMs = false;
      break;
    }
  }
  if (allowMissingFactoryROMs && noROMs && memConfig.empty()) {
    // These precisely mirror upstream makecfg default ZX128/CPC128/TVC128
    // ROM assignments, and are applied only to otherwise *empty* machine
    // configurations. Existing custom maps are never replaced.
    struct ROMDefault { size_t slot; const char *name; int offset; };
    static const ROMDefault zx[] = {
        {0, "zx128.rom", 0}, {1, "zx128.rom", 16384}};
    static const ROMDefault cpc[] = {
        {0, "cpc6128.rom", 16384}, {7, "cpc_amsdos.rom", 0},
        {16, "cpc6128.rom", 0}};
    static const ROMDefault tvc[] = {
        {0, "tvc22_sys.rom", 0}, {2, "tvc22_ext.rom", 0},
        {3, "tvc_dos12d.rom", 0}};
    const ROMDefault *defaults = (const ROMDefault *) 0;
    size_t count = 0;
    if (machineType == 1) {
      defaults = zx; count = sizeof(zx) / sizeof(zx[0]);
    }
    else if (machineType == 2) {
      defaults = cpc; count = sizeof(cpc) / sizeof(cpc[0]);
    }
    else if (machineType == 3) {
      defaults = tvc; count = sizeof(tvc) / sizeof(tvc[0]);
    }
    if (defaults) {
      bool filesAvailable = true;
      for (size_t i = 0; i < count; i++) {
        std::string romPath = root + "\\roms\\" + defaults[i].name;
        if (!Ep128EmuWinTrial::exists(romPath))
          filesAvailable = false;
      }
      if (filesAvailable) {
        for (size_t i = 0; i < count; i++) {
          romPaths[defaults[i].slot] = root + "\\roms\\" + defaults[i].name;
          romOffsets[defaults[i].slot] = defaults[i].offset;
        }
        romChanged = true;
      }
    }
  }

  bool otherChanged =
      (memConfig != cfg.memory.configFile ||
       fileIODir != cfg.fileio.workingDirectory);
#ifdef ENABLE_SDEXT
  otherChanged = otherChanged || (sdROM != cfg.sdext.romFile);
#endif
  if (!romChanged && !otherChanged)
    return false;

  if (!Ep128EmuWinTrial::backupUserConfig(
          configFileName, Ep128Emu::getEp128EmuHomeDirectory(),
          ".pre-025-rompath.bak")) {
    (void) MessageBoxW((HWND) 0,
                       L"Could not back up the original machine settings. "
                       L"Automatic trial ROM path repair was cancelled.",
                       L"ep128emu ROM path recovery",
                       MB_OK | MB_ICONWARNING);
    return false;
  }
  for (size_t i = 0; i < 68U; i++) {
    cfg.memory.rom[i].file = romPaths[i];
    cfg.memory.rom[i].offset = romOffsets[i];
  }
  const bool memConfigChanged = (memConfig != cfg.memory.configFile);
  cfg.memory.configFile = memConfig;
  if (romChanged || memConfigChanged)
    cfg.memoryConfigurationChanged = true;
  if (fileIODir != cfg.fileio.workingDirectory) {
    cfg.fileio.workingDirectory = fileIODir;
    cfg.fileioSettingsChanged = true;
  }
#ifdef ENABLE_SDEXT
  if (sdROM != cfg.sdext.romFile) {
    cfg.sdext.romFile = sdROM;
    cfg.memoryConfigurationChanged = true;
  }
#endif
  return true;
}
#endif

static void cfgErrorFunc(void *userData, const char *msg)
{
  (void) userData;
#ifndef WIN32
  std::fprintf(stderr, "WARNING: %s\n", msg);
#else
  (void) MessageBoxA((HWND) 0, (LPCSTR) msg, (LPCSTR) "ep128emu error",
                     MB_OK | MB_ICONWARNING);
#endif
}

static bool fileHasRuntimeMemoryState(const Ep128Emu::File& f)
{
  return (f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_MEMORY_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZXMEM_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZX_SNA_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_ZX_Z80_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CPCMEM_STATE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CPC_SNA_FILE) ||
          f.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_TVCMEM_STATE));
}

int8_t getSnapshotType(const Ep128Emu::File& f)
{
  int machineType = f.getMachineType();
  if (machineType < 0)
    throw Ep128Emu::Exception("invalid snapshot file");
  if (machineType > 3)
    throw Ep128Emu::Exception("unsupported machine type in snapshot file");
  return int8_t(machineType);
}

int main(int argc, char **argv)
{
  Fl_Window *w = (Fl_Window *) 0;
  Ep128Emu::VirtualMachine  *vm = (Ep128Emu::VirtualMachine *) 0;
  Ep128Emu::AudioOutput     *audioOutput = (Ep128Emu::AudioOutput *) 0;
#ifdef ENABLE_MIDI_PORT
  Ep128Emu::MIDIPort        *midiPort = (Ep128Emu::MIDIPort *) 0;
#endif
  Ep128Emu::EmulatorConfiguration   *config =
      (Ep128Emu::EmulatorConfiguration *) 0;
  Ep128Emu::VMThread        *vmThread = (Ep128Emu::VMThread *) 0;
  Ep128EmuGUI               *gui_ = (Ep128EmuGUI *) 0;
  const char      *cfgFileName = "ep128cfg.dat";
  Ep128Emu::File  *snapshotFile = (Ep128Emu::File *) 0;
  int       snapshotNameIndex = 0;
  int       loadFileNameIndex = 0;
  int       colorScheme = 0;
  int8_t    machineType = -1;   // 0: EP (default), 1: ZX, 2: CPC, 3: TVC
  int8_t    retval = 0;
  int       restartMachineType = -1;
  int       restartLoadType = 0;
  std::string restartLoadFileName;
#ifdef DISABLE_OPENGL_DISPLAY
  bool      glEnabled = false;
#else
  bool      glEnabled = true;
  bool      glCanDoSingleBuf = false;
  bool      glCanDoDoubleBuf = false;
#endif
  bool      configLoaded = false;
  bool      startupSnapshotRuntimeMemoryActive = false;
#ifdef __APPLE__
  void      *macOSRealtimeActivity = (void *) 0;
#endif

#ifdef WIN32
  timeBeginPeriod(1U);
#else
  // set machine type if the program name in argv[0] begins with
  // "zx", "cpc" or "tvc"
  for (size_t i = 0; argv[0][i] != '\0'; i++) {
    if (i == 0 || argv[0][i] == '/') {
      if (argv[0][i] == '/')
        i++;
      if (std::strncmp(argv[0] + i, "zx", 2) == 0)
        machineType = 1;
      else if (std::strncmp(argv[0] + i, "cpc", 3) == 0)
        machineType = 2;
      else if (std::strncmp(argv[0] + i, "tvc", 3) == 0)
        machineType = 3;
      else
        machineType = -1;
    }
  }
#endif
  try {
    // find out machine type to be emulated
    for (int i = 1; i < argc; i++) {
      if (std::strcmp(argv[i], "-cfg") == 0) {
        if (++i >= argc)
          throw Ep128Emu::Exception("missing configuration file name");
      }
      else if (std::strcmp(argv[i], "-loadfile") == 0) {
        if (++i >= argc)
          throw Ep128Emu::Exception("missing binary file name");
        loadFileNameIndex = i;
      }
      else if (std::strcmp(argv[i], "-snapshot") == 0) {
        if (++i >= argc)
          throw Ep128Emu::Exception("missing snapshot file name");
        snapshotNameIndex = i;
      }
      else if (std::strcmp(argv[i], "-colorscheme") == 0) {
        if (++i >= argc)
          throw Ep128Emu::Exception("missing color scheme number");
        colorScheme = int(std::atoi(argv[i]));
        colorScheme = (colorScheme >= 0 && colorScheme <= 3 ? colorScheme : 0);
      }
      else if (std::strcmp(argv[i], "-ep128") == 0) {
        machineType = 0;
      }
      else if (std::strcmp(argv[i], "-zx") == 0) {
        machineType = 1;
      }
      else if (std::strcmp(argv[i], "-cpc") == 0) {
        machineType = 2;
      }
      else if (std::strcmp(argv[i], "-tvc") == 0) {
        machineType = 3;
      }
#ifndef DISABLE_OPENGL_DISPLAY
      else if (std::strcmp(argv[i], "-opengl") == 0) {
        glEnabled = true;
      }
#endif
      else if (std::strcmp(argv[i], "-no-opengl") == 0) {
        glEnabled = false;
      }
      else if (std::strcmp(argv[i], "-h") == 0 ||
               std::strcmp(argv[i], "-help") == 0 ||
               std::strcmp(argv[i], "--help") == 0) {
        std::fprintf(stderr, "Usage: %s [OPTIONS...]\n", argv[0]);
        std::fprintf(stderr, "The allowed options are:\n");
        std::fprintf(stderr,
                     "    -h | -help | --help "
                     "print this message\n");
        std::fprintf(stderr,
                     "    -ep128 | -zx | -cpc | -tvc\n                        "
                     "select the type of machine to be emulated\n");
        std::fprintf(stderr,
                     "    -cfg <FILENAME>     "
                     "load ASCII format configuration file\n");
        std::fprintf(stderr,
                     "    -snapshot <FNAME>   "
                     "load snapshot or demo file on startup\n");
#ifndef DISABLE_OPENGL_DISPLAY
        std::fprintf(stderr,
                     "    -opengl             "
                     "use OpenGL video driver (this is the default)\n");
#endif
        std::fprintf(stderr,
                     "    -no-opengl          "
                     "use software video driver\n");
        std::fprintf(stderr,
                     "    -colorscheme <N>    "
                     "use GUI color scheme N (0, 1, 2, or 3)\n");
        std::fprintf(stderr,
                     "    OPTION=VALUE        "
                     "set configuration variable 'OPTION' to 'VALUE'\n");
        std::fprintf(stderr,
                     "    OPTION              "
                     "set boolean configuration variable 'OPTION' to true\n");
#ifdef WIN32
        timeEndPeriod(1U);
#endif
        return 0;
      }
    }

    // Typed ASCII configuration files can select the machine at startup.
    // Legacy untyped configuration files remain compatible and do not select one.
    for (int i = 1; i < argc; i++) {
      if (std::strcmp(argv[i], "-cfg") == 0 && (i + 1) < argc) {
        int cfgMachineType =
            Ep128Emu::ConfigurationDB::getMachineTypeMetadata(argv[++i], false);
        if (cfgMachineType >= 0) {
          if (machineType < 0)
            machineType = int8_t(cfgMachineType);
          else if (machineType != cfgMachineType) {
            std::string msg("configuration file is for ");
            msg += Ep128Emu::File::getMachineTypeName(cfgMachineType);
            msg += ", but the selected machine is ";
            msg += Ep128Emu::File::getMachineTypeName(machineType);
            throw std::runtime_error(msg);
          }
        }
      }
      else if ((std::strcmp(argv[i], "-snapshot") == 0 ||
                std::strcmp(argv[i], "-loadfile") == 0 ||
                std::strcmp(argv[i], "-colorscheme") == 0) &&
               (i + 1) < argc) {
        i++;
      }
    }

    Fl::lock();
#ifdef __APPLE__
    // First tape REM engagement may occur immediately during guest startup.
    // Warm the host sound silently before creating or running the guest VM.
    (void) ep128emuPrepareMacOSRelayClick();
    // Replace FLTK's generic macOS About panel so the native application menu
    // shows this build identity and the exact toolkit patch version.
    Fl_Sys_Menu_Bar::about(&macOSAboutCallback, (void *) 0);
#endif
    Ep128Emu::setGUIColorScheme(colorScheme);
    audioOutput = new Ep128Emu::AudioOutput_PortAudio();
#ifndef DISABLE_OPENGL_DISPLAY
    if (glEnabled) {
      glCanDoSingleBuf = bool(Fl_Gl_Window::can_do(FL_RGB | FL_SINGLE));
      glCanDoDoubleBuf = bool(Fl_Gl_Window::can_do(FL_RGB | FL_DOUBLE));
      if (glCanDoSingleBuf | glCanDoDoubleBuf)
        w = new Ep128Emu::OpenGLDisplay(32, 32, 384, 288, "");
      else
        glEnabled = false;
    }
#endif
    if (!glEnabled)
      w = new Ep128Emu::FLTKDisplay(32, 32, 384, 288, "");
    w->end();
    if (snapshotNameIndex > 0) {
      snapshotFile = new Ep128Emu::File(argv[snapshotNameIndex], false);
      int snapshotMachineType = getSnapshotType(*snapshotFile);
      if (machineType < 0)
        machineType = int8_t(snapshotMachineType);
      else if (machineType != snapshotMachineType) {
        std::string msg("snapshot is for ");
        msg += Ep128Emu::File::getMachineTypeName(snapshotMachineType);
        msg += ", but the selected machine is ";
        msg += Ep128Emu::File::getMachineTypeName(machineType);
        throw std::runtime_error(msg);
      }
    }
    if (loadFileNameIndex > 0) {
      Ep128Emu::File loadFile(argv[loadFileNameIndex], false);
      int loadMachineType = loadFile.getMachineType();
      if (loadMachineType > 3)
        throw Ep128Emu::Exception("unsupported machine type in binary file");
      if (loadMachineType >= 0) {
        if (machineType < 0)
          machineType = int8_t(loadMachineType);
        else if (machineType != loadMachineType) {
          std::string msg("binary file is for ");
          msg += Ep128Emu::File::getMachineTypeName(loadMachineType);
          msg += ", but the selected machine is ";
          msg += Ep128Emu::File::getMachineTypeName(machineType);
          throw std::runtime_error(msg);
        }
      }
    }
    if (machineType < 0)
      machineType = 0;
    if (machineType == 1) {
      cfgFileName = "zx128cfg.dat";
      vm = new ZX128::ZX128VM(*(dynamic_cast<Ep128Emu::VideoDisplay *>(w)),
                              *audioOutput);
    }
    else if (machineType == 2) {
      cfgFileName = "cpc_cfg.dat";
      vm = new CPC464::CPC464VM(*(dynamic_cast<Ep128Emu::VideoDisplay *>(w)),
                                *audioOutput);
    }
    else if (machineType == 3) {
      cfgFileName = "tvc_cfg.dat";
      vm = new TVC64::TVC64VM(*(dynamic_cast<Ep128Emu::VideoDisplay *>(w)),
                              *audioOutput);
    }
    else {
      vm = new Ep128::Ep128VM(*(dynamic_cast<Ep128Emu::VideoDisplay *>(w)),
                              *audioOutput);
    }
#ifdef ENABLE_MIDI_PORT
    midiPort = new Ep128Emu::MIDIPort(*vm);
#endif
    config = new Ep128Emu::EmulatorConfiguration(
        *vm, *(dynamic_cast<Ep128Emu::VideoDisplay *>(w)), *audioOutput
#ifdef ENABLE_MIDI_PORT
        , *midiPort
#endif
        );
    config->setErrorCallback(&cfgErrorFunc, (void *) 0);
    // load base configuration (if available)
    {
      Ep128Emu::File  *f = (Ep128Emu::File *) 0;
#ifndef WIN32
      std::string baseDir(Ep128Emu::getEp128EmuHomeDirectory());
      bool    makecfgNeeded = false;
      bool    makecfgDone = false;
#endif
      try {
        try {
          f = new Ep128Emu::File(cfgFileName, true);
        }
        catch (Ep128Emu::Exception& e) {
#ifndef WIN32
          makecfgNeeded = true;
        }
        while (true) {
          if (makecfgNeeded)
#endif
          {
            std::string cmdLine = "\"";
            cmdLine += argv[0];
            size_t  i = cmdLine.length();
            while (i > 1) {
              i--;
              if (cmdLine[i] == '/' || cmdLine[i] == '\\') {
                i++;
                break;
              }
            }
            cmdLine.resize(i);
#ifdef WIN32
            cmdLine += "makecfg\"";
#else
#  ifndef __APPLE__
            cmdLine += "epmakecfg\" -c \"";
#  else
            cmdLine += "epmakecfg\" -f \"";
#  endif
            cmdLine += baseDir;
            cmdLine += '"';
            makecfgDone = true;
#endif
            std::system(cmdLine.c_str());
          }
          f = new Ep128Emu::File(cfgFileName, true);
#ifndef WIN32
          config->registerChunkType(*f);
          f->processAllChunks();
          delete f;
          f = (Ep128Emu::File *) 0;
          if (makecfgDone)
            break;
          // check if there is a valid ROM image on segment 0x00
          makecfgNeeded = config->memory.rom[0x00].file.empty();
          if (!makecfgNeeded) {
            std::FILE *tmp =
                Ep128Emu::fileOpen(config->memory.rom[0x00].file.c_str(), "rb");
            makecfgNeeded = (!tmp);
            if (!makecfgNeeded) {
              std::fclose(tmp);
              break;
            }
            // get install directory from existing configuration
            std::string romName;
            Ep128Emu::splitPath(config->memory.rom[0x00].file,
                                baseDir, romName);
            DIR     *d = (DIR *) 0;
            if (baseDir.length() > 6 && !romName.empty() &&
                std::strcmp(baseDir.c_str() + (baseDir.length() - 6), "/roms/")
                == 0) {
              baseDir.resize(baseDir.length() - 6);
              d = opendir(baseDir.c_str());
            }
            if (d)
              closedir(d);
            else
              baseDir = Ep128Emu::getEp128EmuHomeDirectory();
          }
          // invalid configuration, try running makecfg if not done yet
          delete config;
          config = (Ep128Emu::EmulatorConfiguration *) 0;
          makecfgNeeded = true;
          config = new Ep128Emu::EmulatorConfiguration(
              *vm, *(dynamic_cast<Ep128Emu::VideoDisplay *>(w)), *audioOutput
#ifdef ENABLE_MIDI_PORT
              , *midiPort
#endif
              );
          config->setErrorCallback(&cfgErrorFunc, (void *) 0);
#endif
        }
#ifdef WIN32
        config->registerChunkType(*f);
        f->processAllChunks();
        delete f;
#endif
      }
      catch (...) {
        if (f)
          delete f;
      }
    }
    configLoaded = true;
    // check command line for any additional configuration
    for (int i = 1; i < argc; i++) {
      if (std::strcmp(argv[i], "-ep128") == 0 ||
          std::strcmp(argv[i], "-zx") == 0 ||
          std::strcmp(argv[i], "-cpc") == 0 ||
          std::strcmp(argv[i], "-tvc") == 0 ||
#ifndef DISABLE_OPENGL_DISPLAY
          std::strcmp(argv[i], "-opengl") == 0 ||
#endif
          std::strcmp(argv[i], "-no-opengl") == 0)
        continue;
      if (std::strcmp(argv[i], "-cfg") == 0) {
        if (++i >= argc)
          throw Ep128Emu::Exception("missing configuration file name");
        config->loadState(argv[i], false);
      }
      else if (std::strcmp(argv[i], "-snapshot") == 0 ||
               std::strcmp(argv[i], "-loadfile") == 0 ||
               std::strcmp(argv[i], "-colorscheme") == 0) {
        i++;
      }
      else {
        const char  *s = argv[i];
#ifdef __APPLE__
        if (std::strncmp(s, "-psn_", 5) == 0)
          continue;
#endif
        if (*s == '-')
          s++;
        if (*s == '-')
          s++;
        const char  *p = std::strchr(s, '=');
        if (!p)
          (*config)[s] = bool(true);
        else {
          std::string optName;
          while (s != p) {
            optName += (*s);
            s++;
          }
          p++;
          (*config)[optName] = p;
        }
      }
    }
#ifndef DISABLE_OPENGL_DISPLAY
    if (glEnabled) {
      if (config->display.bufferingMode == 0 && !glCanDoSingleBuf) {
        config->display.bufferingMode = 1;
        config->displaySettingsChanged = true;
      }
      if (config->display.bufferingMode != 0 && !glCanDoDoubleBuf) {
        config->display.bufferingMode = 0;
        config->displaySettingsChanged = true;
      }
    }
#endif
#ifdef WIN32
    // Repair stale paths before ROM loading. Skip the missing-ROM factory
    // fallback for explicit -cfg user choices; only default base configs
    // are eligible for previously uninitialized ZX/CPC/TVC ROM slots.
    bool userSpecifiedCfg = false;
    for (int i = 1; i < argc; i++)
      userSpecifiedCfg = userSpecifiedCfg || (std::strcmp(argv[i], "-cfg") == 0);
    (void) repairKnownWindowsTrialConfiguration(
        *config, int(machineType), cfgFileName, !userSpecifiedCfg);
#endif
    config->applySettings();
#ifdef __APPLE__
    // Request latency-critical scheduling while the emulator is active.
    // Normal idle system sleep remains allowed.
    macOSRealtimeActivity = ep128emuBeginMacOSRealtimeActivity();
#endif
    if (snapshotFile) {
      bool runtimeMemoryState = fileHasRuntimeMemoryState(*snapshotFile);
      vm->registerChunkTypes(*snapshotFile);
      snapshotFile->processAllChunks();
      if (runtimeMemoryState)
        startupSnapshotRuntimeMemoryActive = true;
      delete snapshotFile;
      snapshotFile = (Ep128Emu::File *) 0;
    }
    if (loadFileNameIndex > 0) {
      Ep128Emu::File loadFile(argv[loadFileNameIndex], false);
      bool runtimeMemoryState = fileHasRuntimeMemoryState(loadFile);
      bool configState =
          loadFile.hasChunkType(Ep128Emu::File::EP128EMU_CHUNKTYPE_CONFIG_DB);
      vm->registerChunkTypes(loadFile);
      config->registerChunkType(loadFile);
      loadFile.processAllChunks();
      if (runtimeMemoryState)
        startupSnapshotRuntimeMemoryActive = true;
      else if (configState && startupSnapshotRuntimeMemoryActive)
        config->memoryConfigurationChanged = true;
      bool memoryConfigurationChanged = config->memoryConfigurationChanged;
      config->applySettings();
      if (memoryConfigurationChanged)
        startupSnapshotRuntimeMemoryActive = false;
    }
    vmThread = new Ep128Emu::VMThread(*vm);
    gui_ = new Ep128EmuGUI(*(dynamic_cast<Ep128Emu::VideoDisplay *>(w)),
                           *audioOutput, *vm, *vmThread, *config);
    if (startupSnapshotRuntimeMemoryActive)
      gui_->setSnapshotRuntimeMemoryActive(true);
    gui_->run();
    restartMachineType = gui_->getRestartMachineType();
    restartLoadType = gui_->getRestartLoadType();
    restartLoadFileName = gui_->getRestartLoadFileName();
  }
  catch (std::exception& e) {
    if (snapshotFile) {
      delete snapshotFile;
      snapshotFile = (Ep128Emu::File *) 0;
    }
    if (gui_) {
      gui_->errorMessage(e.what());
    }
    else {
#ifndef WIN32
      std::fprintf(stderr, " *** error: %s\n", e.what());
#else
      (void) MessageBoxA((HWND) 0, (LPCSTR) e.what(), (LPCSTR) "ep128emu error",
                         MB_OK | MB_ICONWARNING);
#endif
    }
    retval = int8_t(-1);
  }
  if (gui_)
    delete gui_;
  if (vmThread)
    delete vmThread;
  if (config) {
    if (configLoaded) {
      try {
        Ep128Emu::File  f;
        f.addMachineTypeMetadata(machineType);
        config->saveState(f);
        f.writeFile(cfgFileName, true);
      }
      catch (...) {
      }
    }
    delete config;
  }
#ifdef ENABLE_MIDI_PORT
  if (midiPort)
    delete midiPort;
#endif
  if (vm)
    delete vm;
  if (w)
    delete w;
  if (audioOutput)
    delete audioOutput;
#ifdef WIN32
  timeEndPeriod(1U);
#endif
#ifdef __APPLE__
  if (retval == 0 && restartMachineType >= 0 &&
      !restartAsMacOSMachine(restartMachineType, restartLoadType,
                             restartLoadFileName)) {
    std::fprintf(stderr, "WARNING: failed to restart ep128emu for machine type %d\n",
                 restartMachineType);
    retval = int8_t(-1);
  }
#elif defined(WIN32)
  if (retval == 0 && restartMachineType >= 0 &&
      !restartAsWindowsMachine(restartMachineType, restartLoadType,
                               restartLoadFileName)) {
    (void) MessageBoxW((HWND) 0,
                      L"Could not start ep128emu in the selected machine mode.",
                      L"ep128emu machine switch", MB_OK | MB_ICONWARNING);
    retval = int8_t(-1);
  }
#endif
  return int(retval);
}

