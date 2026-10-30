#ifndef EP128EMU_WIN_RAR_FALLBACK_HPP
#define EP128EMU_WIN_RAR_FALLBACK_HPP
#ifdef _WIN32
#include <windows.h>
#include <string>
#include <cstddef>
namespace Ep128EmuArchiveDrop {
  bool winExtractLegacyRARMember(const std::wstring& archivePath,
      const std::wstring& memberName, HANDLE destination,
      size_t expectedSize, std::string& errorMessage);
}
#endif
#endif
