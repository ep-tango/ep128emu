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

  // Preserve user-specified paths. Obsolete pre-public trial relocation
  // is intentionally retired; missing paths remain unchanged rather
  // than being silently redirected to another installation directory.
  inline std::string relocateMissing(const std::string& oldPath,
                                      const std::string& currentRoot,
                                      bool isDirectory = false) {
    (void) currentRoot;
    (void) isDirectory;
    return oldPath;
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
