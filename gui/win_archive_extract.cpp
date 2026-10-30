#ifdef _WIN32
#include "win_archive_extract.hpp"
#include "win_rar_fallback.hpp"
#include "archive_drop.hpp"
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
  static std::wstring wide(const std::string& s) {
    if(s.empty()) return std::wstring();
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,0,0);
    if(n<=0) return std::wstring();
    std::vector<wchar_t> b(static_cast<size_t>(n),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,&b[0],n))
      return std::wstring();
    return std::wstring(&b[0]);
  }
  static std::string utf8(const std::wstring& s) {
    if(s.empty()) return std::string();
    int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,0,0,0,0);
    if(n<=0) return std::string();
    std::vector<char> b(static_cast<size_t>(n),'\0');
    if(!WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,&b[0],n,0,0))
      return std::string();
    return std::string(&b[0]);
  }
  static bool validSegment(const std::wstring& s) {
    if(s.empty() || s==L"." || s==L".." || s.size()>245)
      return false;
    if(s.back()==L'.' || s.back()==L' ') return false;
    for(wchar_t c:s) if(c<32 || c==L':' || c==L'<' || c==L'>' ||
                      c==L'|' || c==L'?' || c==L'*' || c==L'"')
      return false;
    std::wstring t=s.substr(0,s.find(L'.'));
    for(size_t i=0;i<t.size();++i)
      if(t[i]>=L'a' && t[i]<=L'z') t[i]=wchar_t(t[i]-32);
    if(t==L"CON"||t==L"PRN"||t==L"AUX"||t==L"NUL"||
       t==L"COM1"||t==L"COM2"||t==L"COM3"||t==L"COM4"||
       t==L"COM5"||t==L"COM6"||t==L"COM7"||t==L"COM8"||t==L"COM9"||
       t==L"LPT1"||t==L"LPT2"||t==L"LPT3"||t==L"LPT4"||
       t==L"LPT5"||t==L"LPT6"||t==L"LPT7"||t==L"LPT8"||t==L"LPT9")
      return false;
    return true;
  }
  static bool safeRelative(const std::wstring& name,
                           std::vector<std::wstring>& parts) {
    parts.clear();
    if(name.empty() || name[0]==L'/' || name[0]==L'\\') return false;
    size_t first=0;
    while(first<name.size()) {
      size_t end=name.find_first_of(L"/\\",first);
      if(end==std::wstring::npos) end=name.size();
      std::wstring part=name.substr(first,end-first);
      if(!validSegment(part)) return false;
      parts.push_back(part);
      if(parts.size()>16) return false;
      first=end+1;
    }
    return !parts.empty();
  }
  static void removeDirectory(const std::wstring& directory) {
    if(directory.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h=FindFirstFileW((directory+L"\\*").c_str(),&fd);
    if(h!=INVALID_HANDLE_VALUE) {
      do {
        std::wstring name(fd.cFileName);
        if(name==L"."||name==L"..") continue;
        std::wstring child=directory+L"\\"+name;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
          DeleteFileW(child.c_str());
        else if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
          removeDirectory(child);
        else DeleteFileW(child.c_str());
      } while(FindNextFileW(h,&fd));
      FindClose(h);
    }
    RemoveDirectoryW(directory.c_str());
  }
  static bool makePath(const std::wstring& root,
                       const std::vector<std::wstring>& parts,
                       bool directory, std::wstring& out) {
    out=root;
    for(size_t i=0;i<parts.size();i++) {
      out+=L"\\";
      out+=parts[i];
      if(directory||i+1<parts.size()) {
        if(!CreateDirectoryW(out.c_str(),0) &&
           GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        DWORD a=GetFileAttributesW(out.c_str());
        if(a==INVALID_FILE_ATTRIBUTES ||
           !(a&FILE_ATTRIBUTE_DIRECTORY) ||
           (a&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
      }
    }
    return true;
  }
  static void collect(const std::wstring& path,
                      std::vector<std::string>& files, size_t depth) {
    if(depth>16) return;
    WIN32_FIND_DATAW fd;
    HANDLE h=FindFirstFileW((path+L"\\*").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do {
      std::wstring n(fd.cFileName);
      if(n==L"."||n==L".."||(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
        continue;
      std::wstring f=path+L"\\"+n;
      if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) collect(f,files,depth+1);
      else if(n.size()>=4) {
        std::wstring ext=n.substr(n.size()-4);
        for(size_t i=0;i<ext.size();++i)
          if(ext[i]>=L'A'&&ext[i]<=L'Z') ext[i]=wchar_t(ext[i]+32);
        if(ext==L".com"||ext==L".trn") files.push_back(utf8(f));
      }
    } while(FindNextFileW(h,&fd));
    FindClose(h);
  }
}

namespace Ep128EmuArchiveDrop {
  void winRemoveTree(const std::string& directory) {
    std::wstring p=wide(directory);
    if(!p.empty()&&p.find(L"ep128emu-archive-")!=std::wstring::npos)
      removeDirectory(p);
  }
  bool winCreateProgramAlias(const std::string& src,
                             std::string& alias,std::string& error) {
    std::wstring name=wide(src);
    alias.clear();
    if(name.empty()) {error="Invalid UTF-8 program path.";return false;}
    size_t dot=name.find_last_of(L'.');
    std::wstring ext=(dot!=std::wstring::npos?
                      name.substr(dot):std::wstring());
    for(size_t i=0;i<ext.size();++i)
      if(ext[i]>=L'a'&&ext[i]<=L'z') ext[i]=wchar_t(ext[i]-32);
    if(ext!=L".COM"&&ext!=L".TRN") {
      error="Only COM or TRN can be started.";return false;
    }
    size_t slash=name.find_last_of(L"/\\");
    if(slash==std::wstring::npos) {error="Missing program directory.";return false;}
    for(unsigned i=0;i<100;++i) {
      wchar_t b[35];
      if(i==0) swprintf(b,35,L"EP128RUN%ls",ext.c_str());
      else swprintf(b,35,L"EP128R%02u%ls",i,ext.c_str());
      std::wstring candidate=name.substr(0,slash+1)+b;
      if(CreateHardLinkW(candidate.c_str(),name.c_str(),0) ||
         CopyFileW(name.c_str(),candidate.c_str(),TRUE)) {
        alias=utf8(candidate);return !alias.empty();
      }
      if(GetLastError()!=ERROR_FILE_EXISTS&&
         GetLastError()!=ERROR_ALREADY_EXISTS) break;
    }
    error="Could not create Enterprise launch alias.";
    return false;
  }
}

namespace Ep128EmuArchiveDrop {
  bool winStageProgram(const std::string& original,std::string& tempDirectory,
                       std::string& stagedFile,std::string& error) {
    tempDirectory.clear();stagedFile.clear();error.clear();
    std::wstring input=wide(original);
    if(input.empty()){error="Invalid Enterprise program filename.";return false;}
    size_t dot=input.find_last_of(L'.');
    std::wstring ext=(dot==std::wstring::npos ? L"" : input.substr(dot));
    for(size_t i=0;i<ext.size();++i)
      if(ext[i]>=L'a'&&ext[i]<=L'z')ext[i]=wchar_t(ext[i]-32);
    if(ext!=L".COM" && ext!=L".TRN"){
      error="Only COM and TRN programs can be staged.";return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if(!GetFileAttributesExW(input.c_str(),GetFileExInfoStandard,&attr) ||
       (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
       attr.nFileSizeHigh!=0 || attr.nFileSizeLow>64U*1024U*1024U) {
      error="The dropped Enterprise program cannot be read or exceeds 64MB.";
      return false;
    }
    wchar_t tmp[MAX_PATH+2]={},stem[MAX_PATH+2]={};
    DWORD n=GetTempPathW(MAX_PATH,tmp);
    if(!n||n>=MAX_PATH||!GetTempFileNameW(tmp,L"ep1",0,stem)) {
      error="Could not create temporary program directory.";return false;
    }
    DeleteFileW(stem);
    std::wstring root(stem);
    size_t slash=root.find_last_of(L"/\\");
    root=root.substr(0,slash+1)+L"ep128emu-archive-"+
         root.substr(slash+1);
    if(!CreateDirectoryW(root.c_str(),NULL)) {
      error="Could not create temporary program directory.";return false;
    }
    std::wstring destination=root+L"\\EP128RUN"+ext;
    if(!CopyFileW(input.c_str(),destination.c_str(),TRUE)) {
      removeDirectory(root);
      error="Could not stage the Enterprise program.";return false;
    }
    tempDirectory=utf8(root);
    stagedFile=utf8(destination);
    if(tempDirectory.empty()||stagedFile.empty()) {
      removeDirectory(root);
      tempDirectory.clear();stagedFile.clear();
      error="Temporary program path encoding failed.";return false;
    }
    return true;
  }
}

namespace Ep128EmuArchiveDrop {
  bool winExtractArchive(const std::string& archivePath,
                         std::string& output,
                         std::vector<std::string>& programs,
                         std::string& error) {
    output.clear();programs.clear();error.clear();
    std::wstring input=wide(archivePath);
    if(input.empty()) {error="Invalid UTF-8 archive filename.";return false;}
    wchar_t tmp[MAX_PATH+2]={0};
    wchar_t randomName[MAX_PATH+2]={0};
    DWORD n=GetTempPathW(MAX_PATH,tmp);
    if(n==0||n>=MAX_PATH ||
       !GetTempFileNameW(tmp,L"ep1",0,randomName)) {
      error="Could not allocate temporary archive directory.";return false;
    }
    DeleteFileW(randomName);
    std::wstring root=randomName;
    size_t slash=root.find_last_of(L"/\\");
    root=root.substr(0,slash+1)+L"ep128emu-archive-"+
         root.substr(slash+1);
    if(!CreateDirectoryW(root.c_str(),0)) {
      error="Could not create temporary archive directory.";return false;
    }
    struct archive *reader=archive_read_new();
    if(!reader) {removeDirectory(root);error="Archive allocation failed.";return false;}
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    bool ok=true;
    if(archive_read_open_filename_w(reader,input.c_str(),10240)!=ARCHIVE_OK) {
      error=std::string("Could not read ZIP/RAR: ")+archive_error_string(reader);
      ok=false;
    }
    size_t total=0,entries=0;
    if(ok) {
      struct archive_entry *entry=0;
      int rc=0;
      while((rc=archive_read_next_header(reader,&entry))==ARCHIVE_OK) {
        if(++entries>4096) {error="Archive has too many entries.";ok=false;break;}
        const char *raw=archive_entry_pathname_utf8(entry);
        if(!raw) raw=archive_entry_pathname(entry);
        std::wstring rel=wide(raw?raw:"");
        std::vector<std::wstring> parts;
        if(!safeRelative(rel,parts)) {
          error="Archive has an unsafe pathname.";ok=false;break;
        }
        const int type=archive_entry_filetype(entry);
        if(archive_entry_hardlink(entry) || archive_entry_symlink(entry) ||
           (type!=AE_IFDIR && type!=AE_IFREG)) {
          error="Archive links/devices are not permitted.";ok=false;break;
        }
        const la_int64_t length=archive_entry_size(entry);
        if(type==AE_IFREG &&
           (length<0||length>64*1024*1024LL ||
            total+static_cast<size_t>(length)>256*1024*1024UL)) {
          error="Archive file/total size exceeds safe limits.";ok=false;break;
        }
        std::wstring dest;
        if(!makePath(root,parts,type==AE_IFDIR,dest)) {
          error="Could not create safe archive destination.";ok=false;break;
        }
        if(type==AE_IFDIR) {archive_read_data_skip(reader);continue;}
        HANDLE h=CreateFileW(dest.c_str(),GENERIC_WRITE,0,0,CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL,0);
        if(h==INVALID_HANDLE_VALUE) {
          error="Could not create extracted file.";ok=false;break;
        }
        char buf[32768];
        la_ssize_t bytes=0;
        size_t count=0;
        bool writeFailed=false;
        bool tooLarge=false;
        while((bytes=archive_read_data(reader,buf,sizeof(buf)))>0) {
          if(size_t(bytes)>64*1024*1024UL-count ||
             size_t(bytes)>size_t(length)-count) {
            tooLarge=true;ok=false;break;
          }
          DWORD written=0;
          if(!WriteFile(h,buf,DWORD(bytes),&written,0) ||
             written!=DWORD(bytes)) {
            writeFailed=true;ok=false;break;
          }
          count+=size_t(bytes);
        }
        if(tooLarge) {
          error="Archive decoded data exceeds its declared safe size.";
        }
        else if(writeFailed) {
          error="Unable to write extracted file.";
        }
        else if(bytes<0 || count!=size_t(length)) {
          std::string archiveError=archive_error_string(reader)
              ?archive_error_string(reader):"decoded length mismatch";
          const int format=archive_format(reader);
          if(format==ARCHIVE_FORMAT_RAR || format==ARCHIVE_FORMAT_RAR_V5) {
            LARGE_INTEGER offset={};
            if(SetFilePointerEx(h,offset,NULL,FILE_BEGIN) &&
               SetEndOfFile(h)) {
              std::string fallbackError;
              if(winExtractLegacyRARMember(input,rel,h,size_t(length),
                                           fallbackError)) {
                count=size_t(length);
                ok=true;
              }
              else {
                ok=false;
                error=std::string("RAR decoder: ")+archiveError+
                    "; compatibility decoder: "+fallbackError;
              }
            }
            else {
              ok=false;error="Unable to reset extracted file for RAR fallback.";
            }
          }
          else {
            ok=false;
            error=std::string("Archive decompression: ")+archiveError;
          }
        }
        CloseHandle(h);
        if(!ok)break;
        total+=count;
      }
      if(ok && rc!=ARCHIVE_EOF) {
        error="Archive validation/decompression failed.";ok=false;
      }
    }
    archive_read_close(reader);
    archive_read_free(reader);
    if(ok) {
      collect(root,programs,0);
      std::sort(programs.begin(),programs.end());
      output=utf8(root);
      if(output.empty()) {error="Temporary filename encoding failed.";ok=false;}
    }
    if(!ok) {removeDirectory(root);programs.clear();output.clear();}
    return ok;
  }
}
#endif
