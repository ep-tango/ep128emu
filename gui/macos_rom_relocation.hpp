#ifndef EP128EMU_MACOS_ROM_RELOCATION_HPP
#define EP128EMU_MACOS_ROM_RELOCATION_HPP

// Restore only missing, previously saved macOS ep128emu ROM paths after
// copying a config between different user accounts. Never migrate custom
// folders or modify persistent config files. Windows T004 handles its own
// specific retired-install migrations.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "rom_archive_catalog.hpp"

namespace Ep128EmuMacROM {
  inline bool readableRegularFile(const std::string& path) {
    if (path.empty()) return false;
    struct stat s;
    if (stat(path.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) return false;
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
  }

  inline std::string relocateMissing(const std::string& oldPath,
                                     const std::string& homePath) {
    if (oldPath.empty() || homePath.empty() || readableRegularFile(oldPath))
      return oldPath;
    // Only the two known per-user ep128emu ROM repositories are eligible.
    // Other missing paths must remain visible so users can locate their
    // deliberately chosen firmware, rather than silently substitute it.
    if (oldPath.compare(0, 7, "/Users/") != 0) return oldPath;
    const size_t ownerEnd = oldPath.find('/', 7);
    if (ownerEnd == std::string::npos || ownerEnd == 7) return oldPath;
    const std::string rest = oldPath.substr(ownerEnd);
    const char *known[] = {
      "/Documents/ep128emu/roms/",
      "/Library/Application Support/ep128emu/roms/"
    };
    std::string name;
    for (unsigned i = 0; i < sizeof(known)/sizeof(known[0]); ++i) {
      std::string prefix(known[i]);
      if (rest.compare(0, prefix.size(), prefix) == 0) {
        name = rest.substr(prefix.size());
        break;
      }
    }
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string::npos)
      return oldPath;
    const std::string localRoots[] = {
      homePath + "/Documents/ep128emu/roms/",
      homePath + "/Library/Application Support/ep128emu/roms/"
    };
    for (unsigned i = 0; i < sizeof(localRoots)/sizeof(localRoots[0]); ++i) {
      const std::string candidate = localRoots[i] + name;
      if (readableRegularFile(candidate)) return candidate;
    }
    return oldPath;
  }

  template<class Config> inline bool hasMissingConfiguredROM(const Config& cfg) {
    for (size_t i=0;i<68;++i) {
      const std::string& path=cfg.memory.rom[i].file;
      if (!path.empty() && !readableRegularFile(path)) return true;
    }
    return false;
  }

  template<class Config> inline bool hasMissingDownloadableROM(const Config& cfg) {
    for (size_t i=0;i<68;++i) {
      const std::string& path=cfg.memory.rom[i].file;
      if (!path.empty() && !readableRegularFile(path) &&
          Ep128EmuStandardROMs::providedByArchive(path)) return true;
    }
    return false;
  }

  // The running EP configuration may be complete while ZX/CPC/EXDOS presets
  // still require standard ROMs not referenced by that active configuration.
  // Look only in known user ROM roots and directories of readable configured
  // standard ROMs; do NOT pretend unsupported EXOS 2.4/TVC files exist.
  // Manual menu: inventory the two standard macOS repositories before
  // offering to download. A file present in either known location is usable;
  // never overwrite custom or previously provisioned firmware.
  inline size_t countMissingStandardROMs(const std::string& dataHome) {
    size_t count=0;
    const char *const *names=Ep128EmuStandardROMs::archiveNames(count);
    if (dataHome.empty()) return count; // an unknown root is not complete
    const std::string modern=dataHome+"/roms/";
    std::string legacy;
    if (const char *home=std::getenv("HOME")) {
      if (*home) legacy=std::string(home)+"/Documents/ep128emu/roms/";
    }
    size_t missing=0;
    for (size_t i=0;i<count;++i) {
      if (readableRegularFile(modern+names[i])) continue;
      if (!legacy.empty() && readableRegularFile(legacy+names[i])) continue;
      ++missing;
    }
    return missing;
  }

  template<class Config> inline bool hasIncompleteStandardROMPackage(
      const Config& cfg, const std::string& dataHome) {
    if (dataHome.empty()) return false;
    std::vector<std::string> roots;
    roots.push_back(dataHome + "/roms");
    if (const char* home=std::getenv("HOME")) {
      if (*home) roots.push_back(std::string(home)+"/Documents/ep128emu/roms");
    }
    for (size_t i=0;i<68;++i) {
      const std::string& path=cfg.memory.rom[i].file;
      if (path.empty() || !readableRegularFile(path) ||
          !Ep128EmuStandardROMs::providedByArchive(path)) continue;
      const size_t slash=path.find_last_of("/\\");
      if (slash!=std::string::npos)
        roots.push_back(path.substr(0,slash));
    }
    size_t count=0;
    const char *const *filenames=Ep128EmuStandardROMs::archiveNames(count);
    for (size_t i=0;i<count;++i) {
      bool present=false;
      for (size_t d=0;d<roots.size();++d) {
        if (readableRegularFile(roots[d]+"/"+filenames[i])) {
          present=true;
          break;
        }
      }
      if (!present) return true;
    }
    return false;
  }

  // Return the first configured filename absent from the 72-ROM archive.
  // A download success must not be advertised as fixing this requirement.
  template<class Config> inline std::string firstUnavailableMissingROM(
      const Config& cfg) {
    for (size_t i=0;i<68;++i) {
      const std::string& path=cfg.memory.rom[i].file;
      if (!path.empty() && !readableRegularFile(path) &&
          !Ep128EmuStandardROMs::providedByArchive(path)) {
        const size_t slash=path.find_last_of("/\\");
        return path.substr(slash==std::string::npos ? 0 : slash+1);
      }
    }
    return std::string();
  }

  template<class Config> inline bool repair(Config& cfg) {
    const char *home = std::getenv("HOME");
    if (!home || !*home) return false;
    const std::string homePath(home);
    bool changed = false;
    for (size_t i = 0; i < 68; ++i) {
      std::string& file = cfg.memory.rom[i].file;
      const std::string resolved = relocateMissing(file, homePath);
      if (resolved != file) {
        file = resolved;
        changed = true;
      }
    }
#ifdef ENABLE_SDEXT
    {
      std::string& file = cfg.sdext.romFile;
      const std::string resolved = relocateMissing(file, homePath);
      if (resolved != file) {
        file = resolved;
        changed = true;
      }
    }
#endif
    if (changed) cfg.memoryConfigurationChanged = true;
    return changed;
  }
 }
#ifdef __APPLE__
// Implemented by gui/gui.cpp; shared with command-line -cfg child startup.
bool ep128emuOfferMacROMDownload();
#endif
#endif
