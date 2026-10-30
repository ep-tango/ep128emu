#ifndef EP128EMU_ARCHIVE_DROP_HPP
#define EP128EMU_ARCHIVE_DROP_HPP

#include <string>
#include <vector>

namespace Ep128EmuArchiveDrop {

  struct ProgramInspection {
    bool        readable;
    size_t      fileSize;
    bool        type5Header;
    unsigned int declaredPayloadSize;
    size_t      actualPayloadSize;
    bool        payloadSizeMatches;

    ProgramInspection()
      : readable(false),
        fileSize(0U),
        type5Header(false),
        declaredPayloadSize(0U),
        actualPayloadSize(0U),
        payloadSizeMatches(false)
    {
    }
  };

  bool isSupportedArchive(const std::string& fileName);

  bool inspectProgram(const std::string& programFile,
                      ProgramInspection& info,
                      std::string& errorMessage);

  bool extractArchive(const std::string& archiveFile,
                      std::string& temporaryDirectory,
                      std::vector<std::string>& programFiles,
                      std::string& errorMessage);

  void removeTemporaryDirectory(const std::string& directory);

  bool createProgramAlias(const std::string& programFile,
                          std::string& aliasFile,
                          std::string& errorMessage);

  bool canLoadProgramByOriginalName(const std::string& programFile);

  std::string directoryName(const std::string& fileName);
  std::string baseName(const std::string& fileName);
  std::string relativeProgramPath(const std::string& archiveDirectory,
                                  const std::string& programFile);

}

#endif
