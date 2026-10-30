// EF08K: macOS Beta Disk optional external ROM and private read-only TRD media.
// No firmware bytes or user source files become part of the application.
#ifndef EP128EMU_BETADISK_TRIAL_MEDIA_HPP
#define EP128EMU_BETADISK_TRIAL_MEDIA_HPP
#ifdef __APPLE__
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Ep128EmuBetaTrial {
struct StagedTRD {
  std::string path;
  int tracks;
  int sides;
  StagedTRD(): path(), tracks(0), sides(0) {}
};
inline void removeTemporary(const std::string& path) {
  if (!path.empty()) (void)::unlink(path.c_str());
}
inline std::vector<uint8_t> readExactRegularFile(const std::string& path,
                                                 size_t maxBytes) {
  int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if (fd<0) throw std::runtime_error("Beta Disk: cannot open regular input file");
  struct stat st;
  if (::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 ||
      uint64_t(st.st_size)>uint64_t(maxBytes)) {
    ::close(fd);
    throw std::runtime_error("Beta Disk: invalid file type or size");
  }
  const size_t n=size_t(st.st_size);
  std::vector<uint8_t> data(n);
  size_t pos=0;
  while(pos<n) {
    ssize_t r=::read(fd,&data[pos],n-pos);
    if(r<0 && errno==EINTR)continue;
    if(r<=0) {
      ::close(fd);
      throw std::runtime_error("Beta Disk: incomplete input read");
    }
    pos+=size_t(r);
  }
  uint8_t extra=0;
  ssize_t r=::read(fd,&extra,1);
  const int rc=::close(fd);
  if(r!=0 || rc!=0)throw std::runtime_error("Beta Disk: input changed during read");
  return data;
}
inline std::vector<uint8_t> readROM(const std::string& path) {
  std::vector<uint8_t> rom=readExactRegularFile(path,16385);
  if (rom.size()!=16384U)
    throw std::runtime_error("Beta Disk: select a raw 16,384-byte ROM, not a ZIP");
  return rom;
}
inline StagedTRD stageTRDReadOnly(const std::string& source) {
  // On-disk .TRD is track-major. Never mutate/reorder original contents.
  // 327680 bytes would be ambiguous (80x1 / 40x2); refuse silently guessing.
  std::vector<uint8_t> contents=readExactRegularFile(source,655361);
  StagedTRD result;
  if(contents.size()==655360U) {result.tracks=80;result.sides=2;}
  else if(contents.size()==163840U) {result.tracks=40;result.sides=1;}
  else throw std::runtime_error(
    "Beta Disk: only unambiguous 80T/2S or 40T/1S raw TRD supported; 320K requires a geometry selector");
  const char *tmpdir=std::getenv("TMPDIR");
  std::string prefix=(tmpdir && *tmpdir ? tmpdir : "/tmp");
  if(prefix[prefix.size()-1]!='/')prefix+='/';
  prefix+="ep128emu-ef08k-trd-XXXXXX";
  std::vector<char> p(prefix.begin(),prefix.end());p.push_back('\0');
  int fd=::mkstemp(p.data());
  if(fd<0)throw std::runtime_error("Beta Disk: cannot create private TRD copy");
  result.path=p.data();
  size_t pos=0;
  while(pos<contents.size()) {
    ssize_t n=::write(fd,&contents[pos],contents.size()-pos);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)break;
    pos+=size_t(n);
  }
  const bool writeOk=(pos==contents.size());
  const int chmodOk=(writeOk ? ::fchmod(fd,0400) : -1);
  const int closeOk=::close(fd);
  if(!writeOk || chmodOk!=0 || closeOk!=0) {
    removeTemporary(result.path);
    throw std::runtime_error("Beta Disk: private TRD staging failed");
  }
  return result;
}

// 0.26 trial: persistent, *user-named* writable TRD work copy. This stage
// does not enable FDC writes; that requires its own transaction/error QA.
// Never open or modify the input image in write mode.
// Existing, user-selected .TRD: no intermediate file or mandatory backup.
inline StagedTRD inspectTRDWritableExisting(const std::string& filename) {
  const std::vector<uint8_t> bytes=readExactRegularFile(filename,655361);
  StagedTRD out;
  if(bytes.size()==655360U) {out.tracks=80;out.sides=2;}
  else if(bytes.size()==163840U) {out.tracks=40;out.sides=1;}
  else throw std::runtime_error("Beta Disk: unsupported TRD geometry (use 640K or 160K)");
  out.path=filename;
  return out;
}
inline StagedTRD stageTRDWritableCopy(const std::string& source,
                                     const std::string& destination) {
  if (destination.empty() || destination==source)
    throw std::runtime_error("Beta Disk: separate output TRD path required");
  const std::vector<uint8_t> bytes=readExactRegularFile(source,655361);
  StagedTRD result;
  if(bytes.size()==655360U) {result.tracks=80;result.sides=2;}
  else if(bytes.size()==163840U) {result.tracks=40;result.sides=1;}
  else throw std::runtime_error("Beta Disk: unsupported writable TRD geometry");
  int fd=::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)
    throw std::runtime_error("Beta Disk: output exists or cannot be created");
  bool ok=true;
  size_t pos=0;
  while(pos<bytes.size()) {
    ssize_t n=::write(fd,bytes.data()+pos,bytes.size()-pos);
    if(n<0 && errno==EINTR)continue;
    if(n<=0){ok=false;break;}
    pos+=size_t(n);
  }
  if(ok && ::fsync(fd)!=0)ok=false;
  if(::close(fd)!=0)ok=false;
  try {
    if(!ok || readExactRegularFile(destination,655361)!=bytes)
      throw std::runtime_error("Beta Disk: writable TRD copy/verify failed");
    const size_t slash=destination.rfind('/');
    const std::string parent=(slash==std::string::npos ? "." :
                              (slash==0 ? "/" : destination.substr(0,slash)));
    int dir=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0)throw std::runtime_error("Beta Disk: copy parent directory unavailable");
    const bool syncOK=(::fsync(dir)==0);
    const bool closeOK=(::close(dir)==0);
    if(!syncOK || !closeOK)
      throw std::runtime_error("Beta Disk: cannot sync new TRD directory entry");
  }
  catch(...) {
    (void)::unlink(destination.c_str()); // only our O_EXCL-created new output
    throw;
  }
  result.path=destination;
  return result;
}
} // namespace Ep128EmuBetaTrial
#elif defined(WIN32)
#include "speccydos_trial_media.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace Ep128EmuBetaTrial {
struct StagedTRD {
  std::string path;
  int tracks,sides;
  StagedTRD():path(),tracks(0),sides(0){}
};
inline void removeTemporary(const std::string& path) {
  if(path.empty()) return;
  std::wstring w=Ep128EmuSpeccyTrial::widePath(path);
  (void) SetFileAttributesW(w.c_str(),FILE_ATTRIBUTE_NORMAL);
  (void) DeleteFileW(w.c_str());
}
inline std::vector<uint8_t> readExactRegularFile(const std::string& path,size_t maxBytes) {
  const std::wstring name=Ep128EmuSpeccyTrial::widePath(path);
  HANDLE h=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT,0);
  if(h==INVALID_HANDLE_VALUE)
    throw std::runtime_error("Beta Disk: cannot open regular input file");
  BY_HANDLE_FILE_INFORMATION info;
  LARGE_INTEGER length;
  const bool ok=GetFileInformationByHandle(h,&info)!=FALSE &&
    !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
    GetFileSizeEx(h,&length)!=FALSE && length.QuadPart>=0 &&
    uint64_t(length.QuadPart)<=uint64_t(maxBytes);
  if(!ok) {CloseHandle(h);throw std::runtime_error("Beta Disk: invalid file type or size");}
  std::vector<uint8_t> bytes(size_t(length.QuadPart));
  DWORD count=0;
  const bool readOk=(bytes.empty() ||
    (ReadFile(h,bytes.data(),DWORD(bytes.size()),&count,0)!=FALSE && count==bytes.size()));
  LARGE_INTEGER after;
  const bool stable=GetFileSizeEx(h,&after)!=FALSE && after.QuadPart==length.QuadPart;
  CloseHandle(h);
  if(!readOk || !stable)throw std::runtime_error("Beta Disk: incomplete or changed input file");
  return bytes;
}
inline std::vector<uint8_t> readROM(const std::string& path) {
  std::vector<uint8_t> b=readExactRegularFile(path,16385);
  if(b.size()!=16384)throw std::runtime_error("Beta Disk: select exact 16384-byte raw ROM");
  return b;
}
inline StagedTRD stageTRDReadOnly(const std::string& source) {
  std::vector<uint8_t> bytes=readExactRegularFile(source,655361);
  StagedTRD result;
  if(bytes.size()==655360) {result.tracks=80;result.sides=2;}
  else if(bytes.size()==163840) {result.tracks=40;result.sides=1;}
  else throw std::runtime_error("Beta Disk: only unambiguous 80T/2S or 40T/1S TRD supported");
  wchar_t dir[32768]={0};
  DWORD n=GetTempPathW(DWORD(sizeof(dir)/sizeof(dir[0])),dir);
  if(!n || n>=sizeof(dir)/sizeof(dir[0]))throw std::runtime_error("Beta Disk: no temp directory");
  wchar_t name[MAX_PATH]={0};
  if(!GetTempFileNameW(dir,L"E8K",0,name))throw std::runtime_error("Beta Disk: cannot create temporary TRD");
  result.path=Ep128EmuSpeccyTrial::utf8Path(name);
  HANDLE h=CreateFileW(name,GENERIC_WRITE,FILE_SHARE_READ,0,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
  if(h==INVALID_HANDLE_VALUE){removeTemporary(result.path);throw std::runtime_error("Beta Disk: cannot open private TRD");}
  DWORD written=0;
  const bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&written,0)!=FALSE &&
                written==bytes.size() && FlushFileBuffers(h)!=FALSE;
  CloseHandle(h);
  if(!ok || !SetFileAttributesW(name,FILE_ATTRIBUTE_READONLY)) {
    removeTemporary(result.path);
    throw std::runtime_error("Beta Disk: cannot stage protected TRD");
  }
  return result;
}

// 0.26 trial: persistent, user-named writable TRD work copy; never opens
// input writable and never replaces an existing destination.
// Existing, user-selected .TRD: no intermediate file or mandatory backup.
inline StagedTRD inspectTRDWritableExisting(const std::string& filename) {
  const std::vector<uint8_t> bytes=readExactRegularFile(filename,655361);
  StagedTRD out;
  if(bytes.size()==655360U) {out.tracks=80;out.sides=2;}
  else if(bytes.size()==163840U) {out.tracks=40;out.sides=1;}
  else throw std::runtime_error("Beta Disk: unsupported TRD geometry (use 640K or 160K)");
  out.path=filename;
  return out;
}
inline StagedTRD stageTRDWritableCopy(const std::string& source,
                                     const std::string& destination) {
  if(destination.empty() || destination==source)
    throw std::runtime_error("Beta Disk: separate output TRD path required");
  const std::vector<uint8_t> bytes=readExactRegularFile(source,655361);
  StagedTRD result;
  if(bytes.size()==655360U) {result.tracks=80;result.sides=2;}
  else if(bytes.size()==163840U) {result.tracks=40;result.sides=1;}
  else throw std::runtime_error("Beta Disk: unsupported writable TRD geometry");
  const std::wstring name=Ep128EmuSpeccyTrial::widePath(destination);
  HANDLE h=CreateFileW(name.c_str(),GENERIC_WRITE,0,0,CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,0);
  if(h==INVALID_HANDLE_VALUE)
    throw std::runtime_error("Beta Disk: output exists or cannot be created");
  DWORD written=0;
  const bool ok=(WriteFile(h,bytes.data(),DWORD(bytes.size()),&written,0)!=FALSE &&
                 written==bytes.size() && FlushFileBuffers(h)!=FALSE);
  const bool closed=CloseHandle(h)!=FALSE;
  try {
    if(!ok || !closed || readExactRegularFile(destination,655361)!=bytes)
      throw std::runtime_error("Beta Disk: writable TRD copy/verify failed");
  }
  catch(...) {
    (void)DeleteFileW(name.c_str()); // only our CREATE_NEW destination
    throw;
  }
  result.path=destination;
  return result;
}
} // namespace Ep128EmuBetaTrial
#endif // __APPLE__ || WIN32
#endif
