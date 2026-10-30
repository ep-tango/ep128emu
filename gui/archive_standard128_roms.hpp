// EF08N: deterministic, non-mutating resolver for temporary Drag & Run
// Standard 128K firmware. Never combine ROMs from different directories.
#ifndef EP128EMU_GUI_ARCHIVE_STANDARD128_ROMS_HPP
#define EP128EMU_GUI_ARCHIVE_STANDARD128_ROMS_HPP
#include <string>
#include <vector>

namespace Ep128EmuArchiveStandard128 {
struct ROMSet {
  std::string directory;
  std::string exos;
  std::string basic;
  std::string fileIO;
};
inline std::string join(const std::string& dir, const char *name) {
  std::string p(dir);
  if (!p.empty() && p[p.size()-1]!='/' && p[p.size()-1]!='\\') p+='/';
  p+=name;
  return p;
}
template <typename Readable>
bool resolve(const std::vector<std::string>& dirs, Readable readable, ROMSet& out) {
  out=ROMSet();
  for (size_t i=0;i<dirs.size();++i) {
    if (dirs[i].empty()) continue;
    ROMSet next;
    next.directory=dirs[i];
    next.exos=join(dirs[i],"exos21.rom");
    next.basic=join(dirs[i],"basic21.rom");
    next.fileIO=join(dirs[i],"epfileio.rom");
    if (readable(next.exos) && readable(next.basic) && readable(next.fileIO)) {
      out=next;
      return true;
    }
  }
  return false;
}
} // namespace Ep128EmuArchiveStandard128
#endif
