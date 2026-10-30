#include "archive_drop.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#  include "win_archive_extract.hpp"
#endif

#ifdef __APPLE__
#  include <dirent.h>
#  include <mach-o/dyld.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace {

  static std::string lowerExtension(const std::string& fileName)
  {
    size_t slash = fileName.find_last_of("/\\");
    size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash))
      return std::string();
    std::string s(fileName.substr(dot));
    for (size_t i = 0; i < s.size(); i++)
      s[i] = char(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
  }

  static std::string normalizeExtractorError(const std::string& text)
  {
    std::string result;
    result.reserve(text.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); i++) {
      unsigned char c = static_cast<unsigned char>(text[i]);
      if (std::isspace(c)) {
        pendingSpace = !result.empty();
        continue;
      }
      if (pendingSpace) {
        result += ' ';
        pendingSpace = false;
      }
      result += char(c);
      if (result.size() >= 600U) {
        result.resize(597U);
        result += "...";
        break;
      }
    }
    return result;
  }

#ifdef __APPLE__
  static std::string bundledUnarPath()
  {
    const char *overridePath = std::getenv("EP128EMU_UNAR");
    if (overridePath && overridePath[0] && access(overridePath, X_OK) == 0)
      return std::string(overridePath);

    uint32_t n = 0;
    (void) _NSGetExecutablePath((char *) 0, &n);
    if (n == 0U)
      return std::string();
    std::vector<char> buf(size_t(n) + 1U, '\0');
    if (_NSGetExecutablePath(&buf[0], &n) != 0)
      return std::string();

    std::string executable(&buf[0]);
    size_t slash = executable.find_last_of('/');
    if (slash == std::string::npos)
      return std::string();
    std::string helper(executable.substr(0, slash + 1U));
    helper += "ep128emu-unar";
    if (access(helper.c_str(), X_OK) != 0)
      return std::string();
    return helper;
  }

  static bool removeTree(const std::string& path)
  {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
      return (errno == ENOENT);
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
      return (unlink(path.c_str()) == 0);

    DIR *d = opendir(path.c_str());
    if (!d)
      return false;
    bool ok = true;
    while (true) {
      struct dirent *ent = readdir(d);
      if (!ent)
        break;
      if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, ".."))
        continue;
      std::string child(path);
      child += '/';
      child += ent->d_name;
      if (!removeTree(child))
        ok = false;
    }
    closedir(d);
    if (rmdir(path.c_str()) != 0)
      ok = false;
    return ok;
  }

  static void collectPrograms(const std::string& path,
                              std::vector<std::string>& programs)
  {
    DIR *d = opendir(path.c_str());
    if (!d)
      return;
    while (true) {
      struct dirent *ent = readdir(d);
      if (!ent)
        break;
      if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, ".."))
        continue;
      std::string child(path);
      child += '/';
      child += ent->d_name;

      struct stat st;
      if (lstat(child.c_str(), &st) != 0)
        continue;
      if (S_ISLNK(st.st_mode)) {
        // Do not allow an archive-created symlink to escape the extraction
        // directory when FILE: later opens companion files.
        (void) unlink(child.c_str());
        continue;
      }
      if (S_ISDIR(st.st_mode)) {
        collectPrograms(child, programs);
        continue;
      }
      if (!S_ISREG(st.st_mode))
        continue;
      std::string ext = lowerExtension(child);
      if (ext == ".com" || ext == ".trn")
        programs.push_back(child);
    }
    closedir(d);
  }
#endif

}

namespace Ep128EmuArchiveDrop {

  bool inspectProgram(const std::string& programFile,
                      ProgramInspection& info,
                      std::string& errorMessage)
  {
    info = ProgramInspection();
    errorMessage.clear();

    std::FILE *f = std::fopen(programFile.c_str(), "rb");
    if (!f) {
      errorMessage = "Could not read the selected Enterprise program.";
      return false;
    }

    unsigned char header[16];
    size_t n = std::fread(header, 1, sizeof(header), f);
    if (std::fseek(f, 0, SEEK_END) != 0) {
      std::fclose(f);
      errorMessage = "Could not determine the selected program size.";
      return false;
    }
    long sz = std::ftell(f);
    std::fclose(f);
    if (sz < 0) {
      errorMessage = "Could not determine the selected program size.";
      return false;
    }

    info.readable = true;
    info.fileSize = size_t(sz);
    info.actualPayloadSize = (info.fileSize >= 16U ?
                              info.fileSize - 16U : 0U);
    if (n >= 4U) {
      info.type5Header = (header[0] == 0x00U && header[1] == 0x05U);
      info.declaredPayloadSize =
          (static_cast<unsigned int>(header[2]) | (static_cast<unsigned int>(header[3]) << 8));
      info.payloadSizeMatches =
          (info.type5Header && info.fileSize >= 16U &&
           info.actualPayloadSize == size_t(info.declaredPayloadSize));
    }
    return true;
  }

  bool isSupportedArchive(const std::string& fileName)
  {
    std::string ext = lowerExtension(fileName);
    return (ext == ".zip" || ext == ".rar");
  }

  std::string directoryName(const std::string& fileName)
  {
    size_t p = fileName.find_last_of("/\\");
    if (p == std::string::npos)
      return std::string(".");
    if (p == 0)
      return fileName.substr(0, 1);
    return fileName.substr(0, p);
  }

  std::string baseName(const std::string& fileName)
  {
    size_t p = fileName.find_last_of("/\\");
    return (p == std::string::npos ? fileName : fileName.substr(p + 1));
  }

  std::string relativeProgramPath(const std::string& archiveDirectory,
                                  const std::string& programFile)
  {
    std::string root(archiveDirectory);
    while (root.size() > 1U &&
           (root[root.size() - 1] == '/' || root[root.size() - 1] == '\\'))
      root.resize(root.size() - 1U);
    if (!root.empty() && programFile.size() > root.size() &&
        programFile.compare(0, root.size(), root) == 0 &&
        (programFile[root.size()] == '/' || programFile[root.size()] == '\\')) {
      return programFile.substr(root.size() + 1U);
    }
    return baseName(programFile);
  }

  void removeTemporaryDirectory(const std::string& directory)
  {
#ifdef _WIN32
    winRemoveTree(directory);
#elif defined(__APPLE__)
    if (!directory.empty())
      (void) removeTree(directory);
#else
    (void) directory;
#endif
  }

  bool createProgramAlias(const std::string& programFile,
                          std::string& aliasFile,
                          std::string& errorMessage)
  {
    aliasFile.clear();
    errorMessage.clear();
#ifdef _WIN32
    return winCreateProgramAlias(programFile,aliasFile,errorMessage);
#elif defined(__APPLE__)
    std::string ext = lowerExtension(programFile);
    if (ext != ".com" && ext != ".trn") {
      errorMessage = "The selected file is not a COM or TRN program.";
      return false;
    }
    std::string dir = directoryName(programFile);
    const char *suffix = (ext == ".trn" ? ".TRN" : ".COM");

    for (int i = 0; i < 100; i++) {
      char nameBuf[32];
      if (i == 0)
        std::snprintf(nameBuf, sizeof(nameBuf), "EP128RUN%s", suffix);
      else
        std::snprintf(nameBuf, sizeof(nameBuf), "EP128R%02d%s", i, suffix);
      std::string candidate(dir);
      if (!candidate.empty() && candidate[candidate.size() - 1] != '/')
        candidate += '/';
      candidate += nameBuf;

      if (link(programFile.c_str(), candidate.c_str()) == 0) {
        aliasFile = candidate;
        return true;
      }
      if (errno != EEXIST) {
        errorMessage = "Could not create a temporary Enterprise program alias.";
        return false;
      }
    }
    errorMessage = "Could not allocate a temporary Enterprise program name.";
    return false;
#endif
  }

  bool canLoadProgramByOriginalName(const std::string& programFile)
  {
    std::string ext = lowerExtension(programFile);
    if (ext != ".com" && ext != ".trn")
      return false;

    std::string name = baseName(programFile);
    if (name.empty())
      return false;

    for (size_t i = 0; i < name.size(); i++) {
      unsigned char c = static_cast<unsigned char>(name[i]);
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-')
        continue;
      return false;
    }
    return true;
  }

  bool extractArchive(const std::string& archiveFile,
                      std::string& temporaryDirectory,
                      std::vector<std::string>& programFiles,
                      std::string& errorMessage)
  {
    temporaryDirectory.clear();
    programFiles.clear();
    errorMessage.clear();

#ifdef _WIN32
    return winExtractArchive(archiveFile,temporaryDirectory,programFiles,errorMessage);
#elif defined(__APPLE__)
    const char *tmpRoot = std::getenv("TMPDIR");
    std::string templ((tmpRoot && tmpRoot[0]) ? tmpRoot : "/tmp");
    if (!templ.empty() && templ[templ.size() - 1] != '/')
      templ += '/';
    templ += "ep128emu-archive-XXXXXX";

    std::vector<char> tmpBuf(templ.begin(), templ.end());
    tmpBuf.push_back('\0');
    char *p = mkdtemp(&tmpBuf[0]);
    if (!p) {
      errorMessage = "Could not create a temporary directory for the archive.";
      return false;
    }
    temporaryDirectory = p;

    const bool useUnar = (lowerExtension(archiveFile) == ".rar");
    std::string unarPath;
    if (useUnar) {
      unarPath = bundledUnarPath();
      if (unarPath.empty()) {
        errorMessage = "The bundled RAR extractor is missing or not executable.";
        removeTemporaryDirectory(temporaryDirectory);
        temporaryDirectory.clear();
        return false;
      }
    }

    int errorPipe[2];
    if (pipe(errorPipe) != 0) {
      errorMessage = "Could not create the archive extractor error pipe.";
      removeTemporaryDirectory(temporaryDirectory);
      temporaryDirectory.clear();
      return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
      close(errorPipe[0]);
      close(errorPipe[1]);
      errorMessage = "Could not start the macOS archive extractor.";
      removeTemporaryDirectory(temporaryDirectory);
      temporaryDirectory.clear();
      return false;
    }
    if (pid == 0) {
      close(errorPipe[0]);
      if (dup2(errorPipe[1], STDERR_FILENO) < 0)
        _exit(125);
      close(errorPipe[1]);
      if (chdir(temporaryDirectory.c_str()) != 0)
        _exit(126);
      if (useUnar) {
        execl(unarPath.c_str(), "ep128emu-unar",
              "-q", "-f", "-D", "-nr", "-nq", "-k", "skip",
              "-o", temporaryDirectory.c_str(), archiveFile.c_str(),
              (char *) 0);
      }
      else {
        execl("/usr/bin/bsdtar", "bsdtar", "-xf", archiveFile.c_str(),
              (char *) 0);
      }
      _exit(127);
    }

    close(errorPipe[1]);
    std::string extractorError;
    char errorBuf[512];
    while (true) {
      ssize_t n = read(errorPipe[0], errorBuf, sizeof(errorBuf));
      if (n > 0) {
        if (extractorError.size() < 4096U) {
          size_t remaining = 4096U - extractorError.size();
          size_t addBytes = (size_t(n) < remaining ? size_t(n) : remaining);
          extractorError.append(errorBuf, addBytes);
        }
        continue;
      }
      if (n < 0 && errno == EINTR)
        continue;
      break;
    }
    close(errorPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
      if (errno != EINTR) {
        errorMessage = "Could not wait for the macOS archive extractor.";
        removeTemporaryDirectory(temporaryDirectory);
        temporaryDirectory.clear();
        return false;
      }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      errorMessage = "The ZIP/RAR archive could not be extracted.";
      std::string detail = normalizeExtractorError(extractorError);
      if (!detail.empty()) {
        errorMessage += "\nExtractor: ";
        errorMessage += detail;
      }
      removeTemporaryDirectory(temporaryDirectory);
      temporaryDirectory.clear();
      return false;
    }

    collectPrograms(temporaryDirectory, programFiles);
    std::sort(programFiles.begin(), programFiles.end());
    return true;
#endif
  }

}
