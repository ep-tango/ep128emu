#ifndef EP128EMU_STANDARD_ROM_ARCHIVE_CATALOG_HPP
#define EP128EMU_STANDARD_ROM_ARCHIVE_CATALOG_HPP

// Original upstream HTTPS ep128emu_roms-2.0.11.bin (2.0.11.2), verified 2026-11-09.
// Compressed archive SHA-256: 775f65b46d8b857bce468e106c1216fc3acbdbd72d84878d8f5e6fb6bb460441
// Metadata only: 72 filenames, NO firmware bytes in this source tree.
// This restores EXOS 2.4, TVC and the complete original stock preset set.
#include <string>
#include <vector>
#include <set>
#include <cctype>
namespace Ep128EmuStandardROMs {
 inline const char * const * archiveNames(size_t& count) {
   static const char * const names[] = {
     "asmon15.rom",
     "basic20.rom",
     "basic21.rom",
     "brd.rom",
     "cpc464.rom",
     "cpc6128.rom",
     "cpc664.rom",
     "cpc_amsdos.rom",
     "cyrus.rom",
     "edcw.rom",
     "ep-plus.rom",
     "epd17l12.rom",
     "epd17z12.rom",
     "epd19hft.rom",
     "epd19uk.rom",
     "epfileio.rom",
     "esb.rom",
     "esp.rom",
     "exdos10.rom",
     "exdos13.rom",
     "exdos14isdos10uk-brd.rom",
     "exdos14isdos10uk-esp.rom",
     "exdos14isdos10uk-hfont.rom",
     "exdos14isdos10uk.rom",
     "exos20.rom",
     "exos21.rom",
     "exos22.rom",
     "exos23.rom",
     "exos232esp.rom",
     "exos232hun.rom",
     "exos232uk.rom",
     "exos24brd.rom",
     "exos24es.rom",
     "exos24hu.rom",
     "exos24uk.rom",
     "fenas12.rom",
     "forth.rom",
     "genmon.rom",
     "heass10hfont.rom",
     "heass10uk.rom",
     "heassekn.rom",
     "hun.rom",
     "ide12.rom",
     "iview.rom",
     "lisp.rom",
     "pascal12.rom",
     "paszians.rom",
     "sdext05.rom",
     "tpt.rom",
     "tvc12_ext.rom",
     "tvc12_sys.rom",
     "tvc21_ext.rom",
     "tvc21_sys.rom",
     "tvc22_ext.rom",
     "tvc22_sys.rom",
     "tvc_dos11c.rom",
     "tvc_dos11d.rom",
     "tvc_dos12c.rom",
     "tvc_dos12d.rom",
     "tvc_sdext.rom",
     "tvcfileio.rom",
     "tvcupm_c.rom",
     "tvcupm_d.rom",
     "zt19ekn.rom",
     "zt19hfnt.rom",
     "zt19hun.rom",
     "zt19uk.rom",
     "zx128.rom",
     "zx41.rom",
     "zx41uk.rom",
     "zx48.rom",
     "zx48gw03.rom",
   };
   count=sizeof(names)/sizeof(names[0]);
   return names;
 }
 // The manual Windows ROM menu must inspect the *running executable's*
 // installation, not the current working directory or stale configuration.
 // Only filename presence is tested here; the 72-file archive hash/contents
 // are verified by the separate, existing downloader before any writes.
 template <typename Exists>
 inline size_t countMissingInstalled(const std::string& programDir,
                                     char separator, Exists exists) {
   size_t count=0;
   const char * const *names=archiveNames(count);
   if (programDir.empty()) return count; // never declare unknown root complete
   std::string romDir=programDir;
   if (romDir[romDir.size()-1]!='/' && romDir[romDir.size()-1]!='\\')
     romDir+=separator;
   romDir+="roms";
   romDir+=separator;
   size_t missing=0;
   for (size_t i=0;i<count;++i)
     if (!exists(romDir+names[i])) ++missing;
   return missing;
 }
 inline bool providedByArchive(const std::string& path) {
   const std::string::size_type slash=path.find_last_of("/\\");
   std::string name=path.substr(slash==std::string::npos ? 0 : slash+1);
   for(size_t i=0;i<name.size();++i)
     name[i]=char(std::tolower(static_cast<unsigned char>(name[i])));
   size_t count=0;
   const char * const *names=archiveNames(count);
   for(size_t i=0;i<count;++i)
     if(name==names[i]) return true;
   return false;
 }
 // Fail closed on obsolete, malformed or partial remotely fetched packages.
 // The data must contain the exact original 2.0.11.2 ROM filename set.
 inline bool isCompleteROMPackage(const std::vector<unsigned char>& data) {
   if (data.size() < 4) return false;
   size_t n=(size_t(data[0]) << 24) | (size_t(data[1]) << 16)
           | (size_t(data[2]) << 8) | size_t(data[3]);
   size_t count=0;
   archiveNames(count);
   if (n != count || n > (data.size()-4)/32) return false;
   size_t off=4+n*32;
   std::set<std::string> found;
   for (size_t i=0; i<n; ++i) {
     const size_t k=4+i*32;
     size_t sz=(size_t(data[k]) << 24) | (size_t(data[k+1]) << 16)
              | (size_t(data[k+2]) << 8) | size_t(data[k+3]);
     if (sz<8192 || sz>65536 || (sz%8192) || sz>data.size()-off)
       return false;
     std::string filename;
     for (size_t j=0; j<28 && data[k+4+j]; ++j) {
       unsigned char c=data[k+4+j];
       if (c>='A' && c<='Z') c|=0x20;
       if (!((c>='a' && c<='z') || (c>='0' && c<='9') ||
             c=='+' || c=='-' || c=='_' || (c=='.' && j!=0))) return false;
       filename+=char(c);
     }
     if (filename.empty() || !providedByArchive(filename) ||
         !found.insert(filename).second) return false;
     off+=sz;
   }
   return off==data.size() && found.size()==count;
 }
}
#endif
