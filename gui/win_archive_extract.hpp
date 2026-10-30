#ifndef EP128EMU_WIN_ARCHIVE_EXTRACT_HPP
#define EP128EMU_WIN_ARCHIVE_EXTRACT_HPP
#ifdef _WIN32
#include <string>
#include <vector>
namespace Ep128EmuArchiveDrop {
 bool winExtractArchive(const std::string&,std::string&,
                        std::vector<std::string>&,std::string&);
 void winRemoveTree(const std::string&);
 bool winCreateProgramAlias(const std::string&,std::string&,std::string&);
 bool winStageProgram(const std::string&,std::string&,
                      std::string&,std::string&);
}
#endif
#endif
