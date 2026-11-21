// C09: conservative host-side classification of raw ZX disk images.
// No filesystem inference, writable mount, or cross-machine ROM support.
#ifndef EP128EMU_C09_DISK_MEDIA_HPP
#define EP128EMU_C09_DISK_MEDIA_HPP
#include <string>
#include <fstream>
#include <cctype>
#include <stdexcept>
namespace Ep128Emu {
struct C09DiskMedia {
  enum Type { Other=0, SpeccySDD=1, BetaTRD=2, Invalid=3 };
  Type type;
  int tracks,sides,sectors,bytesPerSector;
  C09DiskMedia(Type t=Other,int tr=0,int sd=0)
    : type(t),tracks(tr),sides(sd),sectors(16),bytesPerSector(256) {}
  bool identified() const {return type==SpeccySDD || type==BetaTRD;}
  bool sideMajor() const {return type==SpeccySDD;}
};
inline C09DiskMedia inspectC09DiskMedia(const std::string& name) {
  if (name.size()<4) return C09DiskMedia();
  std::string suffix=name.substr(name.size()-4);
  for (std::size_t i=0;i<suffix.size();++i)
    suffix[i]=char(std::tolower(static_cast<unsigned char>(suffix[i])));
  bool sdd=(suffix==".sdd"), trd=(suffix==".trd");
  if (!sdd && !trd) return C09DiskMedia();
  // Extension+exact length are necessary, but do NOT imply a valid guest FS.
  // Non-ZX .IMG and hardware device paths remain on the original EP path.
  std::ifstream stream(name.c_str(),std::ios::binary|std::ios::ate);
  if (!stream) return C09DiskMedia(C09DiskMedia::Invalid);
  std::streamoff n=stream.tellg();
  if (sdd && n==std::streamoff(80*2*16*256))
    return C09DiskMedia(C09DiskMedia::SpeccySDD,80,2);
  if (trd && n==std::streamoff(80*2*16*256))
    return C09DiskMedia(C09DiskMedia::BetaTRD,80,2);
  if (trd && n==std::streamoff(40*1*16*256))
    return C09DiskMedia(C09DiskMedia::BetaTRD,40,1);
  return C09DiskMedia(C09DiskMedia::Invalid);
}
} // namespace Ep128Emu
#endif
