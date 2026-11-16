#ifndef EP128EMU_WIN_TRIAL_PATHS_HPP
#define EP128EMU_WIN_TRIAL_PATHS_HPP

#ifdef WIN32
#  include <windows.h>
#  include <string>
#  include <vector>
#  include <cctype>

namespace Ep128EmuWinTrial {
  inline std::wstring toWide(const std::string& s) {
    if (s.empty())
      return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                s.c_str(), -1, (wchar_t*) 0, 0);
    if (n < 1)
      return std::wstring();
    std::vector<wchar_t> data(size_t(n), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             s.c_str(), -1, &data[0], n))
      return std::wstring();
    return std::wstring(&data[0]);
  }

  inline std::string toUTF8(const std::wstring& s) {
    if (s.empty())
      return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                                (char*) 0, 0, (LPCSTR) 0, (LPBOOL) 0);
    if (n < 1)
      return std::string();
    std::vector<char> data(size_t(n), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &data[0], n,
                             (LPCSTR) 0, (LPBOOL) 0))
      return std::string();
    return std::string(&data[0]);
  }

  inline std::string programDirectory() {
    std::vector<wchar_t> path(32768U, L'\0');
    DWORD n = GetModuleFileNameW((HMODULE) 0, &path[0], DWORD(path.size()));
    if (n == 0 || size_t(n) >= path.size())
      return std::string();
    std::wstring s(&path[0]);
    std::wstring::size_type i = s.find_last_of(L"\\/");
    return (i == std::wstring::npos ? std::string()
                                   : toUTF8(s.substr(0, i)));
  }

  inline bool exists(const std::string& path, bool directory = false) {
    std::wstring w = toWide(path);
    if (w.empty())
      return false;
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           (directory ? ((a & FILE_ATTRIBUTE_DIRECTORY) != 0)
                      : ((a & FILE_ATTRIBUTE_DIRECTORY) == 0));
  }

  inline std::string normalize(const std::string& path) {
    std::string s(path);
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '\\')
        s[i] = '/';
      else
        s[i] = char(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return s;
  }

  // Migrate missing files ONLY from obsolete T001/T002/T003 trial install
  // roots to the actual executable directory. Do not rewrite existing
  // paths, user-owned custom locations, or paths containing "..".
  inline std::string relocateMissing(const std::string& oldPath,
                                      const std::string& currentRoot,
                                      bool isDirectory = false) {
    if (oldPath.empty() || currentRoot.empty() ||
        exists(oldPath, isDirectory))
      return oldPath;
    const std::string oldNorm = normalize(oldPath);
    const std::string::size_type i = oldNorm.find("/ep128emu-2.0.12-");
    if (i == std::string::npos)
      return oldPath;
    const std::string::size_type first = i + std::string("/ep128emu-2.0.12-").size();
    if (first >= oldNorm.size())
      return oldPath;
    const std::string::size_type slash = oldNorm.find('/', first);
    if (slash == std::string::npos)
      return oldPath;
    std::string oldTrial = oldNorm.substr(first, slash - first);
    // The pre-0.24 fixed Windows x64 installation lived at
    // ep128emu-2.0.12-trial. This is an exact historical root name,
    // not an invitation to rebase arbitrary user-specified ROM locations.
    if (oldTrial != "t001" && oldTrial != "t002" &&
        oldTrial != "t003" && oldTrial != "trial")
      return oldPath;
    std::string relative = oldNorm.substr(slash + 1);
    if (relative.find("../") != std::string::npos ||
        relative.find("/..") != std::string::npos ||
        relative.find(':') != std::string::npos ||
        relative.find("//") != std::string::npos ||
        relative.empty())
      return oldPath;
    bool authorized = (isDirectory &&
                       (relative == "roms" || relative == "files" ||
                        relative == "config" || relative == "disk" ||
                        relative == "tape" || relative == "snapshot" ||
                        relative == "demo")) ||
                      relative.compare(0, 5, "roms/") == 0 ||
                      relative.compare(0, 6, "files/") == 0 ||
                      relative.compare(0, 7, "config/") == 0 ||
                      relative.compare(0, 5, "disk/") == 0 ||
                      relative.compare(0, 5, "tape/") == 0 ||
                      relative.compare(0, 9, "snapshot/") == 0 ||
                      relative.compare(0, 5, "demo/") == 0;
    if (!authorized)
      return oldPath;
    std::string candidate(currentRoot + "\\" + relative);
    return exists(candidate, isDirectory) ? candidate : oldPath;
  }

  inline bool backupUserConfig(const char *fileName,
                               const std::string& homeDirectory,
                               const char *backupSuffix = ".pre-t004.bak") {
    if (!fileName || !*fileName || homeDirectory.empty())
      return false;
    std::string source(homeDirectory + "\\" + fileName);
    if (!exists(source))
      return true; // no previous file to preserve
    std::wstring wideSource = toWide(source);
    if (!backupSuffix || !*backupSuffix ||
        std::string(backupSuffix).find_first_of("/\\:") != std::string::npos)
      return false;
    const std::string backupFile = source + backupSuffix;
    std::wstring backupPath = toWide(backupFile);
    if (wideSource.empty() || backupPath.empty())
      return false;
    if (exists(backupFile))
      return true; // always preserve first pre-migration backup
    return CopyFileW(wideSource.c_str(), backupPath.c_str(), TRUE) != FALSE;
  }
}
#endif
#endif
