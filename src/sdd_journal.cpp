#include "sdd_journal.hpp"
#include <cstring>
#include <vector>
#ifdef WIN32
# include <windows.h>
#else
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <cerrno>
#endif

namespace Ep128Emu { namespace SDDJournal {
namespace {
  const size_t wireLength = 8 + 4 + 8 + 256 + 256 + 4;
  uint32_t hash32(const uint8_t *p, size_t n) {
    uint32_t h=2166136261U;
    for(size_t i=0;i<n;++i) h=(h^p[i])*16777619U;
    return h;
  }
  void write32(uint8_t *p,uint32_t v) {
    for(int i=0;i<4;++i) p[i]=uint8_t(v>>(i*8));
  }
  uint32_t read32(const uint8_t *p) {
    uint32_t v=0;
    for(int i=0;i<4;++i) v|=uint32_t(p[i])<<(i*8);
    return v;
  }
  void write64(uint8_t *p,uint64_t v) {
    for(int i=0;i<8;++i) p[i]=uint8_t(v>>(i*8));
  }
  uint64_t read64(const uint8_t *p) {
    uint64_t v=0;
    for(int i=0;i<8;++i) v|=uint64_t(p[i])<<(i*8);
    return v;
  }
  void encode(uint8_t *wire,const Record& r) {
    std::memcpy(wire,"EPSDDJ01",8);
    write32(wire+8,r.offset);
    write64(wire+12,r.otherSectorsHash);
    std::memcpy(wire+20,r.previous,256);
    std::memcpy(wire+276,r.desired,256);
    write32(wire+532,hash32(wire,532));
  }
  bool decode(const uint8_t *wire,Record& r) {
    if(std::memcmp(wire,"EPSDDJ01",8)!=0 ||
       hash32(wire,532)!=read32(wire+532)) return false;
    uint32_t pos=read32(wire+8);
    if(pos >= 655360U || pos%256U != 0U || pos+256U>655360U) return false;
    r.offset=pos;
    r.otherSectorsHash=read64(wire+12);
    std::memcpy(r.previous,wire+20,256);
    std::memcpy(r.desired,wire+276,256);
    return true;
  }
#ifdef WIN32
  std::wstring wide(const std::string& path) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,0,0);
    if(n<=0) return std::wstring();
    std::vector<wchar_t> w(static_cast<size_t>(n), wchar_t(0));
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,w.data(),n))
      return std::wstring();
    return std::wstring(w.data());
  }
  bool writeNew(const std::string& path,const uint8_t *p,size_t size) {
    std::wstring w=wide(path);if(w.empty())return false;
    HANDLE h=CreateFileW(w.c_str(),GENERIC_WRITE,0,0,CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,0);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;
    bool ok=WriteFile(h,p,DWORD(size),&n,0)!=FALSE && n==size;
    if(ok)ok=FlushFileBuffers(h)!=FALSE;
    if(!CloseHandle(h))ok=false;
    return ok;
  }
  bool readExact(const std::string& path,uint8_t *p,size_t size) {
    std::wstring w=wide(path);if(w.empty())return false;
    HANDLE h=CreateFileW(w.c_str(),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,0);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;LARGE_INTEGER total;total.QuadPart=0;
    bool ok=GetFileSizeEx(h,&total)!=FALSE && total.QuadPart==LONGLONG(size);
    if(ok)ok=ReadFile(h,p,DWORD(size),&n,0)!=FALSE && n==size;
    if(!CloseHandle(h))ok=false;
    return ok;
  }
  bool removeDurable(const std::string& path) {
    std::wstring w=wide(path);
    return !w.empty() && DeleteFileW(w.c_str())!=FALSE;
  }
  bool readImage(const std::string& path,uint8_t *p) {
    std::wstring w=wide(path);if(w.empty())return false;
    HANDLE h=CreateFileW(w.c_str(),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,0);
    if(h==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER n; n.QuadPart=0;DWORD done=0;
    bool ok=GetFileSizeEx(h,&n)!=FALSE && n.QuadPart==655360LL;
    if(ok)ok=ReadFile(h,p,655360U,&done,0)!=FALSE && done==655360U;
    if(!CloseHandle(h))ok=false;
    return ok;
  }
  bool replaceSector(const std::string& path,uint32_t pos,const uint8_t *p) {
    std::wstring w=wide(path);if(w.empty())return false;
    HANDLE h=CreateFileW(w.c_str(),GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,0);
    if(h==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER expected;expected.QuadPart=655360LL;
    LARGE_INTEGER offset;offset.QuadPart=pos;
    LARGE_INTEGER n;n.QuadPart=0;DWORD done=0;
    bool ok=GetFileSizeEx(h,&n)!=FALSE && n.QuadPart==expected.QuadPart;
    if(ok)ok=SetFilePointerEx(h,offset,0,FILE_BEGIN)!=FALSE;
    if(ok)ok=WriteFile(h,p,256U,&done,0)!=FALSE && done==256U;
    if(ok)ok=FlushFileBuffers(h)!=FALSE;
    if(!CloseHandle(h))ok=false;
    return ok;
  }
#else
  bool syncParent(const std::string& path) {
    size_t slash=path.find_last_of('/');
    const std::string parent=(slash==std::string::npos)?".":
        (slash==0?"/":path.substr(0,slash));
    int fd=open(parent.c_str(),O_RDONLY);
    if(fd<0)return false;
    const bool ok=(fsync(fd)==0);
    close(fd);
    return ok;
  }
  bool writeNew(const std::string& path,const uint8_t *p,size_t size) {
    int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return false;
    size_t done=0;
    while(done<size){ssize_t n=write(fd,p+done,size-done);if(n<=0)break;done+=size_t(n);}
    bool ok=(done==size && fsync(fd)==0);
    if(close(fd)!=0)ok=false;
    if(ok)ok=syncParent(path);
    return ok;
  }
  bool readExact(const std::string& path,uint8_t *p,size_t size) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0)return false;
    struct stat st;bool ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==off_t(size);
    size_t done=0;
    while(ok && done<size){ssize_t n=::read(fd,p+done,size-done);if(n<=0){ok=false;break;}done+=size_t(n);}
    if(close(fd)!=0)ok=false;
    return ok && done==size;
  }
  bool removeDurable(const std::string& path) {
    return unlink(path.c_str())==0 && syncParent(path);
  }
  bool readImage(const std::string& path,uint8_t *p) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0)return false;
    struct stat st;bool ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==655360;
    size_t done=0;
    while(ok && done<655360U) {
      ssize_t n=::read(fd,p+done,655360U-done);
      if(n<=0){ok=false;break;}done+=size_t(n);
    }
    if(close(fd)!=0)ok=false;
    return ok && done==655360U;
  }
  bool replaceSector(const std::string& path,uint32_t pos,const uint8_t *p) {
    int fd=open(path.c_str(),O_RDWR|O_NOFOLLOW);
    if(fd<0)return false;
    struct stat st;bool ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==655360;
    size_t done=0;
    while(ok && done<256U) {
      ssize_t n=pwrite(fd,p+done,256U-done,off_t(pos+done));
      if(n<=0){ok=false;break;}done+=size_t(n);
    }
    if(ok)ok=fsync(fd)==0;
    if(close(fd)!=0)ok=false;
    return ok && done==256U;
  }
#endif
}
std::string journalPath(const std::string& image) {
  return image+".ep128emu-sdd.pending";
}
bool pending(const std::string& image) {
#ifdef WIN32
  std::wstring w=wide(journalPath(image));
  if(w.empty())return true;
  DWORD attr=GetFileAttributesW(w.c_str());
  return attr!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND;
#else
  struct stat st;
  int rc=lstat(journalPath(image).c_str(),&st);
  return rc==0 || errno!=ENOENT;
#endif
}
bool read(const std::string& image,Record& record) {
  uint8_t raw[wireLength];
  return readExact(journalPath(image),raw,sizeof(raw)) && decode(raw,record);
}
bool stage(const std::string& image,const Record& record) {
  if(record.offset>=655360U || record.offset%256U)return false;
  if(pending(image)) {
    Record prior;
    return read(image,prior) && prior.offset==record.offset &&
           prior.otherSectorsHash==record.otherSectorsHash &&
           std::memcmp(prior.desired,record.desired,256)==0;
  }
  uint8_t raw[wireLength];encode(raw,record);
  return writeNew(journalPath(image),raw,sizeof(raw));
}
bool clear(const std::string& image) {
  return removeDurable(journalPath(image));
}
uint64_t hashOtherSectors(const uint8_t *all,size_t size,uint32_t offset) {
  uint64_t h=14695981039346656037ULL;
  for(size_t i=0;i<size;++i) if(i<offset || i>=size_t(offset)+256U)
    h=(h^uint64_t(all[i]))*1099511628211ULL;
  return h;
}
bool inspect(const std::string& image,Record& record,bool& matchesPrevious,
             bool& matchesDesired,bool& otherSectorsMatch) {
  matchesPrevious=false;matchesDesired=false;otherSectorsMatch=false;
  if(!pending(image) || !read(image,record))return false;
  std::vector<uint8_t> data(655360U);
  if(!readImage(image,data.data()))return false;
  matchesPrevious=std::memcmp(data.data()+record.offset,record.previous,256)==0;
  matchesDesired=std::memcmp(data.data()+record.offset,record.desired,256)==0;
  otherSectorsMatch=(hashOtherSectors(data.data(),data.size(),record.offset)==
                     record.otherSectorsHash);
  return true;
}
bool recover(const std::string& image,bool chooseDesired) {
  Record record={};bool prev=false,desired=false,unchanged=false;
  if(!inspect(image,record,prev,desired,unchanged) || !unchanged)return false;
  // A stale journal must never mutate an unrelated or externally modified
  // disk. A torn target sector alone is allowed for explicit repair.
  std::vector<uint8_t> imageBytes(655360U);
  if(!readImage(image,imageBytes.data()))return false;
  Record retry={};bool p2=false,d2=false,u2=false;
  if(!inspect(image,retry,p2,d2,u2) || !u2 ||
      retry.offset!=record.offset || retry.otherSectorsHash!=record.otherSectorsHash ||
      std::memcmp(retry.previous,record.previous,256)!=0 ||
      std::memcmp(retry.desired,record.desired,256)!=0) return false;
  // Exclusive, durable full-image backup. Refuse overwriting an existing one.
  if(!writeNew(image+".ep128emu-before-recovery.sdd",
               imageBytes.data(),imageBytes.size()))return false;
  const uint8_t *choice=chooseDesired?record.desired:record.previous;
  if(!replaceSector(image,record.offset,choice))return false;
  Record after={};bool afterPrev=false,afterDesired=false,afterOther=false;
  if(!inspect(image,after,afterPrev,afterDesired,afterOther) || !afterOther ||
      !(chooseDesired?afterDesired:afterPrev))return false;
  return clear(image);
}
}} // namespace Ep128Emu::SDDJournal
