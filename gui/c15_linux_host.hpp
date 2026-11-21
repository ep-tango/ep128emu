#ifndef EP128EMU_C15_LINUX_HOST_HPP
#define EP128EMU_C15_LINUX_HOST_HPP
#ifdef __linux__
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace Ep128EmuLinuxHost {
inline std::string executablePath() {
  std::vector<char> path(65536, '\0');
  const ssize_t n=::readlink("/proc/self/exe",path.data(),path.size()-1);
  if(n<=0 || size_t(n)>=path.size()-1)
    throw std::runtime_error("Spectrum: cannot locate Linux executable");
  return std::string(path.data(),size_t(n));
}
inline std::string bundledROMPathFromExecutable(
    const std::string& exe,const char* folder,const char* filename) {
  const size_t slash=exe.find_last_of('/');
  if(slash==std::string::npos)
    throw std::runtime_error("Spectrum: Linux executable folder unavailable");
  std::string directory=exe.substr(0,slash);
  // Generic release TAR: <root>/bin/ep128emu and <root>/SpeccyDOS or BetaDisk.
  // A standalone trial may also place these folders next to its executable.
  if(directory.size()>=4 &&
     directory.compare(directory.size()-4,4,"/bin")==0)
    directory.resize(directory.size()-4);
  if(directory.empty())directory="/";
  if(directory.back()!='/')directory+='/';
  return directory+folder+"/"+filename;
}
}
#endif
#endif
