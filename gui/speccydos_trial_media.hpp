// EF07F: macOS experimental SpeccyDOS media staging.
// Never mount or modify the user's original SDD; only an owned temp copy.
#ifndef EP128EMU_SPECCYDOS_TRIAL_MEDIA_HPP
#define EP128EMU_SPECCYDOS_TRIAL_MEDIA_HPP
#ifdef __APPLE__
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>

namespace Ep128EmuSpeccyTrial {
inline std::vector<uint8_t> readROM(const std::string& path) {
  std::FILE *f=std::fopen(path.c_str(),"rb");
  if (!f) throw std::runtime_error("SpeccyDOS: cannot open ROM file");
  std::vector<uint8_t> data(8193,0);
  size_t n=std::fread(data.data(),1,data.size(),f);
  int err=std::ferror(f);std::fclose(f);
  if (err || n!=8192)
    throw std::runtime_error("SpeccyDOS requires an 8192-byte raw ROM (WD1770/WD1772 v4.2)");
  data.resize(8192);
  return data;
}
// Resolve the ROM relative to the running .app, independent of CWD,
// Downloads, Developer paths or per-user ROM preferences. ROM bytes are
// provisioned locally in the bundle and MUST NOT be added to Git.
inline std::string bundledROMPathFromExecutable(const std::string& exe,
                                                bool wd1772) {
  const std::string marker("/Contents/MacOS/");
  const size_t n=exe.rfind(marker);
  if (n==std::string::npos)
    throw std::runtime_error("SpeccyDOS: bundled ROM requires a macOS .app");
  return exe.substr(0,n)+"/Contents/Resources/SpeccyDOS/"+
         (wd1772?"WD1772.rom":"WD1770.rom");
}
inline std::string bundledROMPath(bool wd1772) {
  uint32_t n=0;
  (void) _NSGetExecutablePath((char*)0,&n);
  if (n<2U || n>65536U)
    throw std::runtime_error("SpeccyDOS: cannot determine application location");
  std::vector<char> exe(size_t(n)+1U,'\0');
  if (_NSGetExecutablePath(exe.data(),&n)!=0)
    throw std::runtime_error("SpeccyDOS: cannot determine executable path");
  return bundledROMPathFromExecutable(exe.data(),wd1772);
}
inline uint64_t romFingerprint(const std::vector<uint8_t>& b) {
  uint64_t h=UINT64_C(14695981039346656037);
  for (size_t i=0;i<b.size();++i) {h^=uint64_t(b[i]);h*=UINT64_C(1099511628211);}
  return h;
}
inline std::vector<uint8_t> readBundledROMFromExecutable(
    const std::string& exe,bool wd1772) {
  const std::string path=bundledROMPathFromExecutable(exe,wd1772);
  const std::vector<uint8_t> rom=readROM(path);
  const uint64_t expected=wd1772?UINT64_C(0xbfdbd2b384688a7e):
                                  UINT64_C(0x74c0e0f7db77e957);
  if (romFingerprint(rom)!=expected)
    throw std::runtime_error("SpeccyDOS: bundled ROM is not the validated 4.2 firmware");
  return rom;
}
inline std::vector<uint8_t> readBundledROM(bool wd1772) {
  const std::string path=bundledROMPath(wd1772);
  const std::vector<uint8_t> rom=readROM(path);
  const uint64_t expected=wd1772?UINT64_C(0xbfdbd2b384688a7e):
                                  UINT64_C(0x74c0e0f7db77e957);
  if (romFingerprint(rom)!=expected)
    throw std::runtime_error("SpeccyDOS: bundled ROM is not the validated 4.2 firmware");
  return rom;
}
inline void removeTemporary(const std::string& path) {
  if (!path.empty()) (void) ::unlink(path.c_str());
}
inline std::string stageSDDReadOnly(const std::string& source) {
  std::FILE *f=std::fopen(source.c_str(),"rb");
  if (!f) throw std::runtime_error("SpeccyDOS: cannot open SDD file");
  std::vector<uint8_t> input(655361,0);
  size_t n=std::fread(input.data(),1,input.size(),f);
  int err=std::ferror(f);std::fclose(f);
  if (err || n!=655360)
    throw std::runtime_error("EF07F supports only 80 tracks x 2 sides x 16 sectors x 256 bytes SDD");
  // TOSEC/MAME SDD is side-major; FloppyDrive's raw geometry is
  // track-major. Reorder a private clone, exactly as in EF07E E2E.
  std::vector<uint8_t> clone(n);
  for (size_t t=0;t<80;t++) for (size_t side=0;side<2;side++) {
    size_t from=(side*80+t)*4096, to=(t*2+side)*4096;
    std::memcpy(clone.data()+to,input.data()+from,4096);
  }
  const char *tmpdir=std::getenv("TMPDIR");
  std::string pattern=(tmpdir && *tmpdir ? tmpdir : "/tmp");
  if (pattern[pattern.size()-1]!='/')pattern+='/';
  pattern+="ep128emu-ef07f-sdd-XXXXXX";
  std::vector<char> path(pattern.begin(),pattern.end());path.push_back('\0');
  int fd=::mkstemp(path.data());
  if (fd<0) throw std::runtime_error("SpeccyDOS: cannot create private SDD copy");
  std::string output(path.data());
  std::FILE *o=::fdopen(fd,"wb");
  if (!o) {::close(fd);removeTemporary(output);throw std::runtime_error("SpeccyDOS: temporary file error");}
  size_t written=std::fwrite(clone.data(),1,clone.size(),o);
  int closeError=std::fclose(o);
  if (written!=clone.size() || closeError!=0 || ::chmod(output.c_str(),0400)!=0) {
    removeTemporary(output);
    throw std::runtime_error("SpeccyDOS: could not protect the temporary disk image");
  }
  return output;
}
} // namespace Ep128EmuSpeccyTrial
#elif defined(WIN32)
// Windows EF07H host adapter: bundled firmware beside EXE and an owned,
// read-only temporary image; no access to the original SDD for writing.
#include <windows.h>
#include <FL/fl_utf8.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Ep128EmuSpeccyTrial {
inline std::wstring widePath(const std::string& path) {
  const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,0,0);
  if(n<=1) throw std::runtime_error("SpeccyDOS: invalid UTF-8 path");
  std::vector<wchar_t> b(size_t(n),L'\0');
  if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,b.data(),n)!=n)
    throw std::runtime_error("SpeccyDOS: UTF-8 filename conversion failed");
  return std::wstring(b.data());
}
inline std::string utf8Path(const std::wstring& path) {
  const int n=WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,0,0,0,0);
  if(n<=1) throw std::runtime_error("SpeccyDOS: UTF-16 filename conversion failed");
  std::vector<char> b(size_t(n),'\0');
  if(WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,b.data(),n,0,0)!=n)
    throw std::runtime_error("SpeccyDOS: UTF-16 filename conversion failed");
  return std::string(b.data());
}
inline std::vector<uint8_t> readROM(const std::string& path) {
  std::FILE* f=fl_fopen(path.c_str(),"rb");
  if(!f) throw std::runtime_error("SpeccyDOS: cannot open ROM file");
  std::vector<uint8_t> data(8193,0);
  size_t n=std::fread(data.data(),1,data.size(),f);
  int err=std::ferror(f); std::fclose(f);
  if(err || n!=8192) throw std::runtime_error("SpeccyDOS requires an exact 8192-byte raw firmware ROM");
  data.resize(8192);
  return data;
}
inline std::string bundledROMPathFromExecutable(const std::string& exe,bool wd1772) {
  const size_t slash=exe.find_last_of("\\/");
  if(slash==std::string::npos) throw std::runtime_error("SpeccyDOS: executable folder unavailable");
  return exe.substr(0,slash)+"\\SpeccyDOS\\"+(wd1772?"WD1772.rom":"WD1770.rom");
}
inline std::string bundledROMPath(bool wd1772) {
  std::vector<wchar_t> path(32768,L'\0');
  DWORD n=GetModuleFileNameW(0,path.data(),DWORD(path.size()));
  if(!n || n>=path.size()) throw std::runtime_error("SpeccyDOS: cannot determine executable path");
  return bundledROMPathFromExecutable(utf8Path(std::wstring(path.data())),wd1772);
}
inline uint64_t romFingerprint(const std::vector<uint8_t>& b) {
  uint64_t h=UINT64_C(14695981039346656037);
  for(size_t i=0;i<b.size();++i){h^=uint64_t(b[i]);h*=UINT64_C(1099511628211);}
  return h;
}
inline std::vector<uint8_t> readBundledROMFromExecutable(const std::string& exe,bool wd1772) {
  const std::vector<uint8_t> b=readROM(bundledROMPathFromExecutable(exe,wd1772));
  const uint64_t expect=wd1772?UINT64_C(0xbfdbd2b384688a7e):UINT64_C(0x74c0e0f7db77e957);
  if(romFingerprint(b)!=expect) throw std::runtime_error("SpeccyDOS: bundled ROM is not validated 4.2 firmware");
  return b;
}
inline std::vector<uint8_t> readBundledROM(bool wd1772) {
  const std::vector<uint8_t> b=readROM(bundledROMPath(wd1772));
  const uint64_t expect=wd1772?UINT64_C(0xbfdbd2b384688a7e):UINT64_C(0x74c0e0f7db77e957);
  if(romFingerprint(b)!=expect) throw std::runtime_error("SpeccyDOS: bundled ROM is not validated 4.2 firmware");
  return b;
}
inline void removeTemporary(const std::string& path) {
  if(path.empty()) return;
  const std::wstring w=widePath(path);
  (void) SetFileAttributesW(w.c_str(),FILE_ATTRIBUTE_NORMAL);
  (void) DeleteFileW(w.c_str());
}
inline std::string stageSDDReadOnly(const std::string& source) {
  std::FILE* f=fl_fopen(source.c_str(),"rb");
  if(!f) throw std::runtime_error("SpeccyDOS: cannot open SDD file");
  std::vector<uint8_t> input(655361,0);
  const size_t n=std::fread(input.data(),1,input.size(),f);
  const int err=std::ferror(f);std::fclose(f);
  if(err || n!=655360) throw std::runtime_error("SpeccyDOS requires exact 80 tracks x 2 sides x 16 sectors x 256B SDD");
  std::vector<uint8_t> clone(n);
  // SDD side-major -> internal FDD track-major, same Mac EF07F rule.
  for(size_t t=0;t<80;t++)for(size_t side=0;side<2;side++) {
    const size_t from=(side*80+t)*4096,to=(t*2+side)*4096;
    std::memcpy(clone.data()+to,input.data()+from,4096);
  }
  std::vector<wchar_t> tmp(32768,L'\0');
  DWORD sz=GetTempPathW(DWORD(tmp.size()),tmp.data());
  if(!sz || sz>=tmp.size()) throw std::runtime_error("SpeccyDOS: cannot locate temporary folder");
  wchar_t name[MAX_PATH]={0};
  if(!GetTempFileNameW(tmp.data(),L"E7F",0,name)) throw std::runtime_error("SpeccyDOS: cannot reserve private SDD image");
  const std::string path=utf8Path(name);
  HANDLE h=CreateFileW(name,GENERIC_WRITE,FILE_SHARE_READ,0,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
  if(h==INVALID_HANDLE_VALUE) {removeTemporary(path);throw std::runtime_error("SpeccyDOS: cannot open private SDD image");}
  DWORD written=0;
  const bool ok=WriteFile(h,clone.data(),DWORD(clone.size()),&written,0)!=FALSE &&
                written==clone.size() && FlushFileBuffers(h)!=FALSE;
  CloseHandle(h);
  if(!ok || !SetFileAttributesW(name,FILE_ATTRIBUTE_READONLY)) {
    removeTemporary(path);
    throw std::runtime_error("SpeccyDOS: cannot write/protect private SDD image");
  }
  return path;
}
} // namespace Ep128EmuSpeccyTrial
#endif // __APPLE__ / WIN32
#endif
