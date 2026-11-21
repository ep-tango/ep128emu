#ifndef EP128EMU_DIAGNOSTIC_LOGGING_HPP
#define EP128EMU_DIAGNOSTIC_LOGGING_HPP
// Shared opt-in host diagnostic log. No network, guest data or frame dumps.
// A disabled or old profile performs no disk writes. Maximum 256 KiB per file.
#include "system.hpp"
#include <FL/fl_utf8.h>      // Unicode-aware file creation on Windows.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <cctype>

namespace Ep128EmuDiagnostic {
struct State {
  std::mutex mutex;
  std::FILE* file;
  bool enabled;
  std::string path;
  std::string glProbe;
  State() : file(NULL), enabled(false) {}
  ~State() { if (file) std::fclose(file); }
};
inline State& state() { static State s; return s; }
inline std::string logPath() {
  std::string base = Ep128Emu::getEp128EmuHomeDirectory();
  if (!base.empty() && base[base.size()-1] != '/' && base[base.size()-1] != '\\')
    base += '/';
  return base + "ep128emu-diagnostic.log";
}
inline std::string safeValue(const char* s) {
  if (!s) return "(none)";
  // No implicit disclosure of ROM/disk paths, email addresses, or pasted text.
  for (const char* p=s; *p; ++p)
    if (*p=='/' || *p=='\\' || *p=='@') return "[path_or_private_text_omitted]";
  std::string out;
  for (const unsigned char* p=(const unsigned char*)s; *p && out.size()<440; ++p) {
    if (std::isalnum(*p) || *p==' ' || *p=='.' || *p=='_' ||
        *p=='-' || *p==':' || *p=='(' || *p==')' || *p=='+' ||
        *p=='=' || *p==',' || *p=='%' || *p=='[' || *p==']')
      out += char(*p);
    else if (*p=='\n' || *p=='\r') out+=' ';
    else out+='_';
  }
  return out;
}
inline bool openForAppend(State& s) {
  if (s.file) return true;
  if (s.path.empty()) return false;
  s.file = fl_fopen(s.path.c_str(), "ab+");
  if (!s.file) return false;
  std::fseek(s.file, 0L, SEEK_END);
  long size = std::ftell(s.file);
  if (size < 0 || size >= 262144L) {
    std::fclose(s.file); s.file=NULL;
    const std::string old=s.path+".old";
    fl_unlink(old.c_str()); // Unicode-aware, one bounded diagnostic backup
    fl_rename(s.path.c_str(),old.c_str());
    s.file=fl_fopen(s.path.c_str(), "ab+");
  }
  return s.file != NULL;
}
inline void writeLocked(State& s,const char* kind,const char* message) {
  if (!s.enabled || !openForAppend(s)) return;
  std::time_t now=std::time(NULL);
  std::tm broken={};
#ifdef WIN32
  gmtime_s(&broken,&now);
#else
  gmtime_r(&now,&broken);
#endif
  char date[32]={0};
  std::strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%SZ",&broken);
  const std::string category=safeValue(kind);
  const std::string value=safeValue(message);
  std::fseek(s.file,0L,SEEK_END);
  const long current=std::ftell(s.file);
  const size_t needed=std::strlen(date)+category.size()+value.size()+5;
  if (current < 0 || size_t(current)+needed > 262144U) {
    std::fclose(s.file);s.file=NULL;
    const std::string old=s.path+".old";
    fl_unlink(old.c_str());
    if (fl_rename(s.path.c_str(),old.c_str()) != 0) {
      // Never create an unbounded log or silently overwrite unrelated files.
      s.enabled=false;return;
    }
    if (!openForAppend(s)) { s.enabled=false;return; }
  }
  std::fprintf(s.file,"%s [%s] %s\n",date,category.c_str(),value.c_str());
  std::fflush(s.file);
}
inline void rememberOpenGL(const std::string& summary) {
  State& s=state();std::lock_guard<std::mutex> guard(s.mutex);
  s.glProbe=safeValue(summary.c_str());
  if (s.enabled)writeLocked(s,"OPENGL",s.glProbe.c_str());
}
inline bool configure(bool on) {
  State& s=state();std::lock_guard<std::mutex> guard(s.mutex);
  if (!on) {
    if (s.enabled)writeLocked(s,"SESSION","Diagnostic logging disabled");
    if (s.file){std::fclose(s.file);s.file=NULL;}
    s.enabled=false;return true;
  }
  if (s.enabled) return true;
  try { s.path=logPath(); }
  catch (...) { return false; }
  if (!openForAppend(s)) return false;
  s.enabled=true;
  writeLocked(s,"SESSION","Diagnostic logging enabled. Only local files.");
  if (!s.glProbe.empty())writeLocked(s,"OPENGL",s.glProbe.c_str());
  else writeLocked(s,"OPENGL","Probe not run. Direct software mode or unavailable.");
  return true;
}
inline void event(const char* kind,const char* message) {
  State& s=state();std::lock_guard<std::mutex> guard(s.mutex);
  writeLocked(s,kind,message);
}
} // namespace Ep128EmuDiagnostic
#endif
