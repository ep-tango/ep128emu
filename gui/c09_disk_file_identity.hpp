// C09: compare actual host file identity to prohibit simultaneous disk aliases.
#ifndef EP128EMU_C09_DISK_FILE_IDENTITY_HPP
#define EP128EMU_C09_DISK_FILE_IDENTITY_HPP
#include <string>
#include <vector>
#ifndef WIN32
# include <sys/stat.h>
#endif
static bool c09ZXSameHostImage(const std::string& a,const std::string& b)
{
  if(a.empty() || b.empty())return false;
  if(a==b)return true;
#ifdef WIN32
  auto wide=[](const std::string& s)->std::vector<wchar_t> {
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                                    s.c_str(),-1,0,0);
    if(n<=1)return {};
    std::vector<wchar_t> w(size_t(n),0);
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
            s.c_str(),-1,w.data(),n)!=n)w.clear();
    return w;
  };
  const std::vector<wchar_t> wa=wide(a),wb=wide(b);
  if(wa.empty() || wb.empty())return false;
  HANDLE ha=CreateFileW(wa.data(),FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,0,0);
  if(ha==INVALID_HANDLE_VALUE)return false;
  HANDLE hb=CreateFileW(wb.data(),FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,0,0);
  if(hb==INVALID_HANDLE_VALUE){CloseHandle(ha);return false;}
  BY_HANDLE_FILE_INFORMATION ia,ib;
  const bool same=GetFileInformationByHandle(ha,&ia) &&
    GetFileInformationByHandle(hb,&ib) &&
    ia.dwVolumeSerialNumber==ib.dwVolumeSerialNumber &&
    ia.nFileIndexHigh==ib.nFileIndexHigh &&
    ia.nFileIndexLow==ib.nFileIndexLow;
  CloseHandle(hb);CloseHandle(ha);return same;
#else
  struct stat sa,sb;
  return ::stat(a.c_str(),&sa)==0 && ::stat(b.c_str(),&sb)==0 &&
         sa.st_dev==sb.st_dev && sa.st_ino==sb.st_ino;
#endif
}
#endif
