#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include "win_rar_fallback.hpp"
#include <windows.h>
#include <string>
#include <vector>

namespace {
std::wstring quoteWindowsArgument(const std::wstring& s) {
  std::wstring q=L"\"";
  unsigned slashCount=0;
  for(size_t i=0;i<s.size();++i) {
    if(s[i]==L'\\') {++slashCount;continue;}
    if(s[i]==L'"') {
      q.append(2*slashCount+1,L'\\');
      q+=L'"';slashCount=0;continue;
    }
    q.append(slashCount,L'\\');
    slashCount=0;
    q+=s[i];
  }
  q.append(2*slashCount,L'\\');
  return q+L'"';
}
}

namespace Ep128EmuArchiveDrop {
bool winExtractLegacyRARMember(const std::wstring& archivePath,
    const std::wstring& memberName, HANDLE destination,
    size_t expectedSize, std::string& errorMessage)
{
  errorMessage.clear();
  if(!destination || destination==INVALID_HANDLE_VALUE ||
     expectedSize>64U*1024U*1024U || memberName.empty()) {
    errorMessage="Invalid RAR fallback parameters.";return false;
  }
  wchar_t modulePath[32768]={};
  DWORD pathSize=GetModuleFileNameW(NULL,modulePath,32768);
  if(pathSize==0 || pathSize>=32768) {
    errorMessage="Unable to find the legacy RAR decoder.";return false;
  }
  std::wstring decoder(modulePath,pathSize);
  size_t slash=decoder.find_last_of(L"/\\");
  if(slash==std::wstring::npos) {
    errorMessage="Unable to find the legacy RAR decoder.";return false;
  }
  decoder.resize(slash+1);
  decoder+=L"ep128emu-7z.exe";
  DWORD attrs=GetFileAttributesW(decoder.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_DIRECTORY)) {
    errorMessage="Legacy RAR decoder is missing (ep128emu-7z.exe).";
    return false;
  }
  SECURITY_ATTRIBUTES sa={};
  sa.nLength=sizeof(sa);
  sa.bInheritHandle=TRUE;
  HANDLE readPipe=NULL,writePipe=NULL;
  if(!CreatePipe(&readPipe,&writePipe,&sa,0)) {
    errorMessage="Could not establish the legacy RAR decoder pipe.";
    return false;
  }
  SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
  HANDLE nullInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              &sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  HANDLE nullError=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              &sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if(nullInput==INVALID_HANDLE_VALUE || nullError==INVALID_HANDLE_VALUE) {
    CloseHandle(readPipe);CloseHandle(writePipe);
    if(nullInput!=INVALID_HANDLE_VALUE)CloseHandle(nullInput);
    if(nullError!=INVALID_HANDLE_VALUE)CloseHandle(nullError);
    errorMessage="Could not initialize legacy RAR decoder streams.";
    return false;
  }
  std::wstring cmd=quoteWindowsArgument(decoder)+L" x -so -spd "
      +quoteWindowsArgument(archivePath)+L" "
      +quoteWindowsArgument(L"-i!"+memberName);
  std::vector<wchar_t> arg(cmd.begin(),cmd.end());
  arg.push_back(L'\0');
  STARTUPINFOW startup={};
  startup.cb=sizeof(startup);
  startup.dwFlags=STARTF_USESTDHANDLES;
  startup.hStdInput=nullInput;
  startup.hStdOutput=writePipe;
  startup.hStdError=nullError;
  PROCESS_INFORMATION process={};
  const BOOL launched=CreateProcessW(decoder.c_str(),arg.data(),NULL,NULL,TRUE,
                                  CREATE_NO_WINDOW,NULL,NULL,&startup,&process);
  CloseHandle(writePipe);
  CloseHandle(nullInput);
  CloseHandle(nullError);
  if(!launched) {
    CloseHandle(readPipe);
    errorMessage="Could not launch the legacy RAR decoder.";
    return false;
  }
  bool valid=true;
  size_t total=0;
  char buf[32768];
  for(;;) {
    DWORD got=0;
    BOOL read=ReadFile(readPipe,buf,sizeof(buf),&got,NULL);
    if(!read || got==0)break;
    if(total>expectedSize || size_t(got)>expectedSize-total) {
      valid=false;
      errorMessage="Legacy RAR output exceeds the declared file size.";
      break;
    }
    DWORD written=0;
    if(!WriteFile(destination,buf,got,&written,NULL) || written!=got) {
      valid=false;
      errorMessage="Unable to save legacy RAR output.";
      break;
    }
    total+=size_t(got);
  }
  if(!valid)TerminateProcess(process.hProcess,1U);
  CloseHandle(readPipe);
  DWORD waited=WaitForSingleObject(process.hProcess,120000);
  if(waited==WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess,1U);
    valid=false;
    errorMessage="Legacy RAR decoder timed out.";
    WaitForSingleObject(process.hProcess,5000);
  }
  DWORD exitCode=1;
  if(!GetExitCodeProcess(process.hProcess,&exitCode) || exitCode!=0)
    valid=false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if(valid && total!=expectedSize) {
    valid=false;
    errorMessage="Legacy RAR decoded size does not match its header.";
  }
  if(!valid && errorMessage.empty())
    errorMessage="Legacy RAR decoder returned an error.";
  return valid;
}
}
#endif
