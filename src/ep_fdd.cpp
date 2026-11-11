
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2019 Istvan Varga <istvanv@users.sourceforge.net>
// https://github.com/istvan-v/ep128emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include "ep128emu.hpp"
#include "ep_fdd.hpp"
#include "sdd_journal.hpp"
#include "system.hpp"

#include <vector>
#include <sys/stat.h>
#ifndef WIN32
#  include <unistd.h> // fsync for original-SDD durability
#endif

#ifdef WIN32
#  include <windows.h>
#  include <winioctl.h>
#elif defined(HAVE_LINUX_FD_H)
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <linux/fd.h>
#endif

#ifdef WIN32

namespace std {

  FILE *ep_fopen(const char *filename, const char *mode);
  int ep_fclose(FILE *stream);
  int ep_fseek(FILE *stream, long int offset, int origin);
  long int ep_ftell(FILE *stream);
  size_t ep_fread(void *ptr, size_t size, size_t count, FILE *stream);
  size_t ep_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
  int ep_setvbuf(FILE *stream, char *buffer, int mode, size_t size);

}       // namespace std

#define fopen ep_fopen
#define fclose ep_fclose
#define fseek ep_fseek
#define ftell ep_ftell
#define fread ep_fread
#define fwrite ep_fwrite
#define setvbuf ep_setvbuf

#endif  // WIN32

namespace Ep128Emu {

  int checkFloppyDisk(const char *fileName,
                      int& nTracks, int& nSides, int& nSectorsPerTrack)
  {
#ifdef WIN32
    if (std::strlen(fileName) < 5)
      return 0;                 // return value == 0: regular file
    if (!(fileName[0] == '\\' && fileName[1] == '\\' &&
          fileName[2] == '.' && fileName[3] == '\\')) {
      return 0;
    }
    DISK_GEOMETRY diskGeometry;
    HANDLE  h = INVALID_HANDLE_VALUE;
    DWORD   tmp;
    bool    retryFlag = false;
    while (true) {
      h = CreateFileA(fileName, (DWORD) 0,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      (LPSECURITY_ATTRIBUTES) 0, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, (HANDLE) 0);
      if (h == INVALID_HANDLE_VALUE)
        return -1;              // return value == -1: error opening device
      if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY,
                          (LPVOID) 0, (DWORD) 0,
                          &diskGeometry, (DWORD) sizeof(diskGeometry),
                          &tmp, (LPOVERLAPPED) 0) == FALSE) {
        if (!retryFlag) {
          retryFlag = true;
          std::FILE *f = std::fopen(fileName, "rb");
          if (f)
            std::fclose(f);
          CloseHandle(h);
          continue;
        }
        CloseHandle(h);
        return -1;
      }
      break;
    }
    bool    errorFlag = false;
    if (!(diskGeometry.MediaType > Unknown &&
          diskGeometry.MediaType < RemovableMedia)) {
      errorFlag = true;
    }
    if (diskGeometry.Cylinders.QuadPart < (LONGLONG) 1 ||
        diskGeometry.Cylinders.QuadPart > (LONGLONG) 254 ||
        diskGeometry.TracksPerCylinder < (DWORD) 1 ||
        diskGeometry.TracksPerCylinder > (DWORD) 2 ||
        diskGeometry.SectorsPerTrack < (DWORD) 1 ||
        diskGeometry.SectorsPerTrack > (DWORD) 240 ||
        diskGeometry.BytesPerSector != (DWORD) 512) {
      errorFlag = true;
    }
    if (nTracks >= 1 && nTracks <= 254) {
      if (nTracks != int(diskGeometry.Cylinders.QuadPart))
        errorFlag = true;
    }
    if (nSides >= 1 && nSides <= 2) {
      if (nSides != int(diskGeometry.TracksPerCylinder))
        errorFlag = true;
    }
    if (nSectorsPerTrack >= 1 && nSectorsPerTrack <= 240) {
      if (nSectorsPerTrack != int(diskGeometry.SectorsPerTrack))
        errorFlag = true;
    }
    if (errorFlag) {
      CloseHandle(h);
      return -2;                // return value == -2: invalid disk geometry
    }
    nTracks = int(diskGeometry.Cylinders.QuadPart);
    nSides = int(diskGeometry.TracksPerCylinder);
    nSectorsPerTrack = int(diskGeometry.SectorsPerTrack);
    if (DeviceIoControl(h, IOCTL_DISK_IS_WRITABLE,
                        (LPVOID) 0, (DWORD) 0, (LPVOID) 0, (DWORD) 0,
                        &tmp, (LPOVERLAPPED) 0) == FALSE) {
      CloseHandle(h);
      return 1;                 // return value == 1: write protected disk
    }
    CloseHandle(h);
    return 2;                   // return value == 2: writable floppy disk
#elif defined(HAVE_LINUX_FD_H)
    int     fd = open(fileName, O_RDONLY);
    if (fd < 0)
      return -1;                // error opening image file
    struct floppy_drive_struct  driveState;
    struct floppy_struct  floppyParams;
    if (ioctl(fd, FDPOLLDRVSTAT, &driveState) != 0) {
      close(fd);
      return 0;                 // not a floppy device
    }
    if (ioctl(fd, FDGETPRM, &floppyParams) != 0) {
      close(fd);
      return 0;                 // not a floppy device
    }
    close(fd);
    if (floppyParams.track < 1U || floppyParams.track > 254U ||
        floppyParams.head < 1U || floppyParams.head > 2U ||
        floppyParams.sect < 1U || floppyParams.sect > 240U ||
        floppyParams.size != (floppyParams.track * floppyParams.head
                              * floppyParams.sect)) {
      return -2;
    }
    if ((nTracks >= 1 && nTracks <= 254 &&
         nTracks != int(floppyParams.track)) ||
        (nSides >= 1 && nSides <= 2 && nSides != int(floppyParams.head)) ||
        (nSectorsPerTrack >= 1 && nSectorsPerTrack <= 240 &&
         nSectorsPerTrack != int(floppyParams.sect))) {
      return -2;
    }
    nTracks = int(floppyParams.track);
    nSides = int(floppyParams.head);
    nSectorsPerTrack = int(floppyParams.sect);
    if (driveState.flags & FD_DISK_WRITABLE)
      return 2;
    return 1;
#else
    (void) fileName;
    (void) nTracks;
    (void) nSides;
    (void) nSectorsPerTrack;
    return 0;
#endif
  }

  // --------------------------------------------------------------------------

  FloppyDrive::FloppyDrive()
    : imageFileName(""),
      imageFile((std::FILE *) 0),
      nTracks(0),
      nSides(0),
      nSectorsPerTrack(0),
      bytesPerSector(512),
      currentTrack(0),
      currentSide(0),
      writeProtectFlag(false),
      userWriteProtected(false),
      sideMajorSDDReadOnly(false),
      nativeSDDWritable(false),
      nativeSDDWriteError(false),
      writableTRDCopy(false),
      writableTRDWriteError(false),
      diskChangeFlag(false),
      bufferedTrack(0xFF),
      bufferedSide(0xFF),
      motorOnInput(false),
      isMotorOn(false),
      ledStateCounter(0U),
      trackBuffer((uint8_t *) 0),
      flagsBuffer((uint8_t *) 0),
      tmpBuffer((uint8_t *) 0),
      bufPos(-1L),
      trackDirtyFlag(false),
      mediaWriteEpoch(0U),
      mediaMountEpoch(0U),
      rewindWriteGuard(false)
  {
    buf_.resize(0);
    this->reset();
  }

  FloppyDrive::~FloppyDrive()
  {
    try { closeDiskImage(); }
    catch (...) {
      // Never throw from a destructor; the GUI checks flush before eject.
      if (imageFile) std::fclose(imageFile);
      imageFile = (std::FILE *) 0;
    }
  }

  static void rewindIdentityMix(uint64_t& h, uint64_t v)
  {
    // Stable, non-cryptographic metadata signature; actual file identity is
    // checked by the OS using both the mounted handle and its current path.
    for (int i = 0; i < 8; ++i) {
      h ^= uint8_t(v >> (8 * i));
      h *= UINT64_C(1099511628211);
    }
  }

  uint64_t FloppyDrive::getRewindImageIdentity() const
  {
    if (!imageFile)
      return 0U;
    uint64_t h = UINT64_C(1469598103934665603);
#ifdef WIN32
    // ep_fopen() returns a raw Win32 HANDLE cast to FILE*, NOT a CRT FILE*.
    // Passing it to _fileno()/ _get_osfhandle() causes a CRT access violation
    // when the rewind identity is queried (including during VM startup).
    // Keep native file-ID comparison against the path: it detects external
    // replacement of a mounted IMG without touching guest disk contents.
    HANDLE nativeHandle = HANDLE(imageFile);
    if (!nativeHandle || nativeHandle == INVALID_HANDLE_VALUE)
      return 0U;
    HANDLE pathHandle = CreateFileA(imageFileName.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pathHandle == INVALID_HANDLE_VALUE)
      return 0U;
    BY_HANDLE_FILE_INFORMATION live, byPath;
    bool ok = GetFileInformationByHandle(nativeHandle, &live) &&
              GetFileInformationByHandle(pathHandle, &byPath);
    CloseHandle(pathHandle);
    if (!ok || live.dwVolumeSerialNumber != byPath.dwVolumeSerialNumber ||
        live.nFileIndexHigh != byPath.nFileIndexHigh ||
        live.nFileIndexLow != byPath.nFileIndexLow)
      return 0U;
    rewindIdentityMix(h, live.dwVolumeSerialNumber);
    rewindIdentityMix(h, (uint64_t(live.nFileIndexHigh) << 32) |
                           uint64_t(live.nFileIndexLow));
    rewindIdentityMix(h, (uint64_t(live.nFileSizeHigh) << 32) |
                           uint64_t(live.nFileSizeLow));
    rewindIdentityMix(h, (uint64_t(live.ftLastWriteTime.dwHighDateTime) << 32) |
                           uint64_t(live.ftLastWriteTime.dwLowDateTime));
#else
    struct stat opened, byPath;
    if (fstat(fileno(imageFile), &opened) != 0 ||
        stat(imageFileName.c_str(), &byPath) != 0 ||
        !S_ISREG(opened.st_mode) || !S_ISREG(byPath.st_mode) ||
        opened.st_dev != byPath.st_dev || opened.st_ino != byPath.st_ino)
      return 0U;
    rewindIdentityMix(h, uint64_t(opened.st_dev));
    rewindIdentityMix(h, uint64_t(opened.st_ino));
    rewindIdentityMix(h, uint64_t(opened.st_size));
    rewindIdentityMix(h, uint64_t(opened.st_mtime));
    rewindIdentityMix(h, uint64_t(opened.st_ctime));
#  if defined(__APPLE__)
    rewindIdentityMix(h, uint64_t(opened.st_mtimespec.tv_nsec));
    rewindIdentityMix(h, uint64_t(opened.st_ctimespec.tv_nsec));
#  elif defined(__linux__)
    rewindIdentityMix(h, uint64_t(opened.st_mtim.tv_nsec));
    rewindIdentityMix(h, uint64_t(opened.st_ctim.tv_nsec));
#  endif
#endif
    return h ? h : 1U;
  }

  bool FloppyDrive::isRewindRegularImageEligible() const
  {
    if (!imageFile)
      return true;
    // Scope explicitly to normal flat .IMG files. SDD/TRD/physical floppy
    // and unusual host device paths retain the historical rewind barrier.
    size_t n = imageFileName.size();
    if (n < 4 || imageFileName[n - 4] != '.' ||
        (imageFileName[n - 3] != 'i' && imageFileName[n - 3] != 'I') ||
        (imageFileName[n - 2] != 'm' && imageFileName[n - 2] != 'M') ||
        (imageFileName[n - 1] != 'g' && imageFileName[n - 1] != 'G'))
      return false;
    return getRewindImageIdentity() != 0U;
  }

  bool FloppyDrive::getRewindIdleState(RewindIdleState& state) const
  {
    // In-flight transfers and pending writes cannot be reconstructed by
    // invalidating the track cache. Rewind at an idle boundary only.
    if (bufPos >= 0L || trackDirtyFlag ||
        !isRewindRegularImageEligible())
      return false;
    state.imageFileName = imageFileName;
    state.mediaWriteEpoch = mediaWriteEpoch;
    state.mediaMountEpoch = mediaMountEpoch;
    state.mounted = haveDisk();
    state.writeProtected = getIsWriteProtected();
    state.currentTrack = currentTrack;
    state.currentSide = currentSide;
    state.nTracks = nTracks;
    state.nSides = nSides;
    state.nSectorsPerTrack = nSectorsPerTrack;
    state.bytesPerSector = bytesPerSector;
    state.diskChangeFlag = diskChangeFlag;
    state.motorOnInput = motorOnInput;
    state.isMotorOn = isMotorOn;
    state.ledStateCounter = ledStateCounter;
    return true;
  }

  bool FloppyDrive::canRestoreRewindIdleState(
      const RewindIdleState& s) const
  {
    return (bufPos < 0L && !trackDirtyFlag &&
            isRewindRegularImageEligible() &&
            haveDisk() == s.mounted &&
            mediaWriteEpoch == s.mediaWriteEpoch &&
            mediaMountEpoch == s.mediaMountEpoch &&
            imageFileName == s.imageFileName &&
            nTracks == s.nTracks && nSides == s.nSides &&
            nSectorsPerTrack == s.nSectorsPerTrack &&
            bytesPerSector == s.bytesPerSector &&
            getIsWriteProtected() == s.writeProtected);
  }

  bool FloppyDrive::restoreRewindIdleState(const RewindIdleState& s)
  {
    if (!canRestoreRewindIdleState(s))
      return false;
    currentTrack = s.currentTrack;
    currentSide = s.currentSide;
    diskChangeFlag = s.diskChangeFlag;
    motorOnInput = s.motorOnInput;
    isMotorOn = s.isMotorOn;
    ledStateCounter = s.ledStateCounter;
    // The disk is immutable within the write epoch: force the next read to
    // fetch from the host image. Never flush or replay a write on restore.
    bufferedTrack = 0xFF;
    bufferedSide = 0xFF;
    bufPos = -1L;
    if (imageFile)
      clearFlagsBuffer();
    return true;
  }

  void FloppyDrive::doStep(int n)
  {
    int     tmp = int(currentTrack) + n;
    int     maxTrack =
        (nTracks > 80 ? (nTracks < 254 ? (nTracks + 1) : 254) : 81);
    currentTrack = uint8_t(tmp > 0 ? (tmp < maxTrack ? tmp : maxTrack) : 0);
  }

  bool FloppyDrive::startDataTransfer(uint8_t trackNum, uint8_t sectorNum)
  {
    if (!(isMotorOn && currentTrack < nTracks && currentSide < nSides &&
          currentTrack == trackNum &&
          sectorNum >= 1 && sectorNum <= nSectorsPerTrack)) {
      return false;
    }
    bufPos = long(sectorNum - 1) * long(bytesPerSector);
    return true;
  }

  void FloppyDrive::closeDiskImage()
  {
    if (!imageFile)
      return;
    if (nativeSDDWritable && !syncNativeSDD())
      throw Exception("FDD: writable SDD flush failed; disk remains mounted");
    if (writableTRDCopy && !syncWritableTRDCopy())
      throw Exception("FDD: writable TRD copy flush failed; disk remains mounted");
    if (!nativeSDDWritable && !writableTRDCopy)
      (void) flushTrack();           // legacy floppy semantics
    std::fclose(imageFile);
    imageFile = (std::FILE *) 0;
    nTracks = 0;
    nSides = 0;
    nSectorsPerTrack = 0;
    bytesPerSector = 512;
    writeProtectFlag = false;
    userWriteProtected = false;
    sideMajorSDDReadOnly = false;
    nativeSDDWritable = false;
    nativeSDDWriteError = false;
    writableTRDCopy = false;
    writableTRDWriteError = false;
    bufferedTrack = 0xFF;
    bufferedSide = 0xFF;
    trackBuffer = (uint8_t *) 0;
    flagsBuffer = (uint8_t *) 0;
    tmpBuffer = (uint8_t *) 0;
    imageFileName.clear();
    buf_.resize(0);
  }

  void FloppyDrive::setDiskImageFile(const std::string& fileName_,
                                     int nTracks_, int nSides_,
                                     int nSectorsPerTrack_, int bytesPerSector_,
                                     bool sideMajorSDDReadOnly_,
                                     bool nativeSDDWritable_,
                                     bool writableTRDCopy_)
  {
    // Native SDD mode cannot be selected by ordinary EP/TVC/TRD drives.
    // Restrict this initial SpeccyDOS trial to the accepted 640 KiB layout.
    if (writableTRDCopy_ &&
        (sideMajorSDDReadOnly_ || nativeSDDWritable_ ||
         !((nTracks_ == 80 && nSides_ == 2) ||
           (nTracks_ == 40 && nSides_ == 1)) ||
         nSectorsPerTrack_ != 16 || bytesPerSector_ != 256))
      throw Exception("FDD: writable TRD copy requires exact 80x2 or 40x1 geometry");
    if (sideMajorSDDReadOnly_ && nativeSDDWritable_)
      throw Exception("FDD: SDD cannot be both read-only and writable");
    // A pending crash-recovery record is never replayed automatically.
    // Refuse both native RW and native RO mounts until manually inspected.
    if (!fileName_.empty() && (sideMajorSDDReadOnly_ || nativeSDDWritable_) &&
        SDDJournal::pending(fileName_))
      throw Exception("FDD: pending SDD recovery journal; resolve it before mounting");
    if ((sideMajorSDDReadOnly_ || nativeSDDWritable_) &&
        (nTracks_ != 80 || nSides_ != 2 ||
         nSectorsPerTrack_ != 16 || bytesPerSector_ != 256))
      throw Exception("FDD: native SpeccyDOS SDD requires 80x2x16x256");
    if (bytesPerSector_ != 128 && bytesPerSector_ != 256 &&
        bytesPerSector_ != 512 && bytesPerSector_ != 1024)
      throw Exception("FDD: sector size must be 128, 256, 512 or 1024");
    if ((fileName_ == "" && imageFileName == "") ||
        (fileName_ == imageFileName &&
         nTracks_ == int(nTracks) &&
         nSides_ == int(nSides) &&
         nSectorsPerTrack_ == int(nSectorsPerTrack) &&
         bytesPerSector_ == int(bytesPerSector) &&
         sideMajorSDDReadOnly_ == sideMajorSDDReadOnly &&
         nativeSDDWritable_ == nativeSDDWritable &&
         writableTRDCopy_ == writableTRDCopy)) {
      return;
    }
    // Bump even when the replacement mount subsequently fails.
    // A historical checkpoint must never survive eject/remount by name.
    ++mediaMountEpoch;
    diskChangeFlag = true;
    closeDiskImage();
    this->reset();
    if (fileName_ == "")
      return;
    unsigned char tmpBuf[512];
    bool    nTracksValid = (nTracks_ >= 1 && nTracks_ <= 254);
    bool    nSidesValid = (nSides_ >= 1 && nSides_ <= 2);
    bool    nSectorsPerTrackValid =
                (nSectorsPerTrack_ >= 1 && nSectorsPerTrack_ <= 240);
    bool    disableFATCheck =
        (nTracksValid && nSidesValid && nSectorsPerTrackValid);
    {
      int     diskType = checkFloppyDisk(fileName_.c_str(),
                                         nTracks_, nSides_, nSectorsPerTrack_);
      if (diskType > 0) {
        // Physical disk geometry is separately queried by the OS and
        // currently supports only its native 512-byte sector devices.
        if (bytesPerSector_ != 512)
          throw Exception("FDD: non-512 sector size on physical device");
        writeProtectFlag = (diskType == 1);
        nTracksValid = true;
        nSidesValid = true;
        nSectorsPerTrackValid = true;
      }
      else if (diskType == -2) {
        throw Exception("FDD: invalid or inconsistent "
                        "disk image size parameters");
      }
      else if (diskType < 0) {
        throw Exception("FDD: error opening disk image file");
      }
    }
    try {
      if (!writeProtectFlag && !sideMajorSDDReadOnly_)
        imageFile = std::fopen(fileName_.c_str(), "r+b");
      if (!imageFile && (nativeSDDWritable_ || writableTRDCopy_))
        throw Exception("FDD: original SDD cannot be opened for read/write");
      if (!imageFile) {
        imageFile = std::fopen(fileName_.c_str(), "rb");
        if (imageFile)
          writeProtectFlag = true;
        else
          throw Exception("FDD: error opening disk image file");
      }
      if (sideMajorSDDReadOnly_)
        writeProtectFlag = true;  // never expose a writable original SDD
      std::setvbuf(imageFile, (char *) 0, _IONBF, 0);
      long    fileSize = -1L;
      if (std::fseek(imageFile, 0L, SEEK_END) >= 0)
        fileSize = std::ftell(imageFile);
      if (fileSize >= long(bytesPerSector_) &&
          fileSize <= (254L * 2L * 240L * 1024L) &&
          (fileSize % long(bytesPerSector_)) == 0L) {
        long    nSectors_ = fileSize / long(bytesPerSector_);
        if (!nTracksValid && nSidesValid && nSectorsPerTrackValid) {
          nTracks_ = int(nSectors_ / (long(nSides_) * long(nSectorsPerTrack_)));
          nTracksValid = true;
        }
        else if (nTracksValid && !nSidesValid && nSectorsPerTrackValid) {
          nSides_ = int(nSectors_ / (long(nTracks_) * long(nSectorsPerTrack_)));
          nSidesValid = true;
        }
        else if (nTracksValid && nSidesValid && !nSectorsPerTrackValid) {
          nSectorsPerTrack_ = int(nSectors_ / (long(nTracks_) * long(nSides_)));
          nSectorsPerTrackValid = true;
        }
      }
      // Try to infer geometry from FAT BPB, but never reinterpret an
      // unambiguous FAT image using a conflicting manually selected size.
      // Only explicit non-512 requests add this check: unchanged default
      // 512-byte image compatibility remains exactly as before.
      if ((!disableFATCheck || bytesPerSector_ != 512) &&
          std::fseek(imageFile, 0L, SEEK_SET) >= 0) {
        if (std::fread(&(tmpBuf[0]), 1, 512, imageFile) == 512) {
          int     fatSectorSize = int(tmpBuf[0x0B]) | (int(tmpBuf[0x0C]) << 8);
          long    fatSectors = long(tmpBuf[0x13]) | (long(tmpBuf[0x14]) << 8);
          if (!fatSectors) {
            fatSectors =
                long(tmpBuf[0x20]) | (long(tmpBuf[0x21]) << 8)
                | (long(tmpBuf[0x22]) << 16) | (long(tmpBuf[0x23]) << 24);
          }
          int     fatSides = int(tmpBuf[0x1A]) | (int(tmpBuf[0x1B]) << 8);
          int     fatSectorsPerTrack =
              int(tmpBuf[0x18]) | (int(tmpBuf[0x19]) << 8);
          bool plausibleFAT = ((tmpBuf[0x15] == 0xF0 ||
                                tmpBuf[0x15] >= 0xF8) &&
                               (tmpBuf[0x0D] & 0x7F) != 0 &&
                               (tmpBuf[0x0D] & (tmpBuf[0x0D] - 1)) == 0 &&
                               (fatSectorSize == 128 || fatSectorSize == 256 ||
                                fatSectorSize == 512 || fatSectorSize == 1024) &&
                               fatSectors > 0 && fatSides >= 1 &&
                               fatSides <= 2 && fatSectorsPerTrack >= 1 &&
                               fatSectorsPerTrack <= 240 &&
                               fatSectors * long(fatSectorSize) == fileSize);
          if (bytesPerSector_ != 512 && plausibleFAT &&
              fatSectorSize != bytesPerSector_) {
            throw Exception("FDD: selected sector size conflicts "
                            "with FAT boot sector");
          }
          if (!disableFATCheck &&
              (tmpBuf[0x15] == 0xF0 || tmpBuf[0x15] >= 0xF8) &&
              (tmpBuf[0x0D] & 0x7F) != 0 &&
              (tmpBuf[0x0D] & (tmpBuf[0x0D] - 1)) == 0 &&
              fatSectorSize == bytesPerSector_ && fatSectors >= 1L &&
              fatSides >= 1 && fatSides <= 2 &&
              fatSectorsPerTrack >= 1 && fatSectorsPerTrack <= 240) {
            if ((fatSectors % long(fatSides * fatSectorsPerTrack)) == 0L) {
              long    fatTracks =
                  fatSectors / long(fatSides * fatSectorsPerTrack);
              if (fatTracks >= 1L && fatTracks <= 254L) {
                // found a valid FAT header, set or check geometry parameters
                if (!nTracksValid)
                  nTracks_ = int(fatTracks);
                if (!nSidesValid)
                  nSides_ = fatSides;
                if (!nSectorsPerTrackValid)
                  nSectorsPerTrack_ = fatSectorsPerTrack;
                if (nTracks_ != int(fatTracks) || nSides_ != fatSides ||
                    nSectorsPerTrack_ != fatSectorsPerTrack) {
                  throw Exception("FDD: invalid or inconsistent "
                                  "disk image size parameters");
                }
              }
            }
          }
        }
      }
      if (!(nTracks_ >= 1 && nTracks_ <= 254 &&
            nSides_ >= 1 && nSides_ <= 2 &&
            nSectorsPerTrack_ >= 1 && nSectorsPerTrack_ <= 240)) {
        if (!(nTracksValid | nSidesValid | nSectorsPerTrackValid
              | (fileSize != (80L * 2 * 9 * long(bytesPerSector_))))) {
          nTracks_ = 80;
          nSides_ = 2;
          nSectorsPerTrack_ = 9;
        }
        else {
          throw Exception("FDD: cannot determine disk geometry; "
                          "set Tracks, Sides and Sectors per track explicitly "
                          "(or use an unambiguous FAT image)");
        }
      }
      fileSize = long(nTracks_ * nSides_) * long(nSectorsPerTrack_)
                 * long(bytesPerSector_);
      // Check exact image length even for a one-sector 128/256-byte
      // fixture. Do not seek 512 bytes before EOF when the file is shorter.
      bool err = true;
      const size_t tailBytes = size_t(fileSize < 512L ? fileSize : 512L);
      if (std::fseek(imageFile, fileSize - long(tailBytes), SEEK_SET) >= 0) {
        if (std::fread(&(tmpBuf[0]), 1, tailBytes, imageFile) == tailBytes &&
            std::fread(&(tmpBuf[0]), 1, 1, imageFile) == 0)
          err = false;
      }
      if (err) {
        throw Exception("FDD: invalid or inconsistent "
                        "disk image size parameters");
      }
      std::fseek(imageFile, 0L, SEEK_SET);
      imageFileName = fileName_;
      // Track cache: DATA + 4 bytes flags/sector + TMP DATA.
      // In uint32_t words: (2 * sectorBytes + 4) / 4.
      buf_.resize(size_t(nSectorsPerTrack_) *
                  (size_t(bytesPerSector_) / 2U + 1U));
    }
    catch (...) {
      closeDiskImage();
      throw;
    }
    nTracks = uint8_t(nTracks_);
    nSides = uint8_t(nSides_);
    nSectorsPerTrack = uint8_t(nSectorsPerTrack_);
    bytesPerSector = uint16_t(bytesPerSector_);
    sideMajorSDDReadOnly = sideMajorSDDReadOnly_;
    nativeSDDWritable = nativeSDDWritable_;
    nativeSDDWriteError = false;
    writableTRDCopy = writableTRDCopy_;
    writableTRDWriteError = false;
    trackBuffer = reinterpret_cast< uint8_t * >(&(buf_.front()));
    flagsBuffer = &(trackBuffer[size_t(nSectorsPerTrack) *
                                  size_t(bytesPerSector)]);
    tmpBuffer = &(flagsBuffer[size_t(nSectorsPerTrack) * 4U]);
    clearFlagsBuffer();
    this->reset();
  }

  void FloppyDrive::padSector()
  {
    if (bufPos < 0L || (bufPos % long(bytesPerSector)) == 0L)
      return;
    if (rewindWriteGuard)
      throw Exception("FDD: host write forbidden during frame render");
    ++mediaWriteEpoch;
    do {
      trackBuffer[bufPos] = 0x00;
      bufPos++;
    } while ((bufPos % long(bytesPerSector)) != 0L);
    bufPos = -1L;
  }

  void FloppyDrive::copySector(int n)
  {
    if (n < 1 || n > int(nSectorsPerTrack) || flagsBuffer[n - 1] != 0x00)
      return;
    size_t    offs = size_t(n - 1) * size_t(bytesPerSector);
    uint32_t  *srcPtr = reinterpret_cast< uint32_t * >(&(tmpBuffer[offs]));
    uint32_t  *dstPtr = reinterpret_cast< uint32_t * >(&(trackBuffer[offs]));
    for (size_t i = 0; i < (size_t(bytesPerSector) / 4U); i += 4) {
      dstPtr[i] = srcPtr[i];
      dstPtr[i + 1] = srcPtr[i + 1];
      dstPtr[i + 2] = srcPtr[i + 2];
      dstPtr[i + 3] = srcPtr[i + 3];
    }
    flagsBuffer[n - 1] = 0x01;
  }

  void FloppyDrive::clearDirtyFlag()
  {
    if (!trackDirtyFlag)
      return;
    for (size_t i = 0; i < size_t(nSectorsPerTrack); i++)
      flagsBuffer[i] &= uint8_t(0x01);
    trackDirtyFlag = false;
  }

  void FloppyDrive::clearFlagsBuffer()
  {
    for (size_t i = 0; i < size_t(nSectorsPerTrack); i++)
      flagsBuffer[i] = 0x00;
    trackDirtyFlag = false;
  }

  void FloppyDrive::clearBuffer(uint8_t *buf)
  {
    uint32_t  *p = reinterpret_cast< uint32_t * >(buf);
    size_t    n = size_t(nSectorsPerTrack) *
                  (size_t(bytesPerSector) / 4U);
    uint32_t  c = 0U;
    for (size_t i = 0; i < n; i += 4) {
      p[i] = c;
      p[i + 1] = c;
      p[i + 2] = c;
      p[i + 3] = c;
    }
  }

  bool FloppyDrive::updateBufferedTrack_()
  {
    bool    retval = flushTrack();
    if (requiresDurableWrite() && !retval)
      return false;                 // retain dirty data on I/O failure
    bufferedTrack = currentTrack;
    bufferedSide = currentSide;
    clearFlagsBuffer();
    return retval;
  }

  bool FloppyDrive::readTrack()
  {
    if (!imageFile)
      return false;
    uint8_t firstSector = 0;
    uint8_t lastSector = 0;
    for (uint8_t i = 1; i <= nSectorsPerTrack; i++) {
      if (!flagsBuffer[i - 1]) {
        if (!firstSector)
          firstSector = i;
        lastSector = i;
      }
    }
    bool    errorFlag = false;
    if (currentTrack >= nTracks || currentSide >= nSides)
      errorFlag = true;
    if (!firstSector)
      return (!errorFlag);
    clearBuffer(tmpBuffer);
    if (!errorFlag) {
      size_t  offs = size_t(firstSector - 1) * size_t(bytesPerSector);
      size_t  nBytes = size_t(lastSector + 1 - firstSector) *
                       size_t(bytesPerSector);
      const long trackIndex = (sideMajorSDDReadOnly || nativeSDDWritable) ?
          (long(currentSide) * long(nTracks) + long(currentTrack)) :
          (long(currentTrack) * long(nSides) + long(currentSide));
      long    filePos = trackIndex * long(nSectorsPerTrack);
      filePos = (filePos * long(bytesPerSector)) + long(offs);
      if (std::fseek(imageFile, filePos, SEEK_SET) < 0) {
        errorFlag = true;
      }
      else {
        size_t  bytesRead =
            std::fread(&(tmpBuffer[offs]), sizeof(uint8_t), nBytes, imageFile);
        errorFlag = (bytesRead != nBytes);
      }
    }
    for (uint8_t i = firstSector; i <= lastSector; i++)
      copySector(i);
    return (!errorFlag);
  }

  bool FloppyDrive::syncNativeSDDFile()
  {
    if (!imageFile) return false;
#ifdef EP128EMU_SDD_RW_FAULT_TEST
    // Only compiled into isolated host-media regression binaries, never the
    // distributed emulator. Reproduces durable sync errors deterministically.
    const char *fault = std::getenv("EP128EMU_TEST_SDD_SYNC_FAULT");
    if (fault && *fault == '1') return false;
#endif
#ifdef WIN32
    // Win32 disk FILE* is a HANDLE in ep_fdd.cpp's historical adapter.
    return FlushFileBuffers(HANDLE(imageFile)) != FALSE;
#else
    return (std::fflush(imageFile) == 0 && ::fsync(fileno(imageFile)) == 0);
#endif
  }

  bool FloppyDrive::syncNativeSDD()
  {
    if (!nativeSDDWritable) return true;
    if (!flushTrack() || !syncNativeSDDFile() ||
        SDDJournal::pending(imageFileName)) {
      nativeSDDWriteError = true;
      return false;
    }
    nativeSDDWriteError = false;
    return true;
  }

  bool FloppyDrive::syncWritableTRDCopy()
  {
    if (!writableTRDCopy) return true;
    if (!flushTrack() || !syncNativeSDDFile()) {
      writableTRDWriteError = true;
      return false;
    }
    writableTRDWriteError = false;
    return true;
  }

  bool FloppyDrive::flushTrack()
  {
    if (rewindWriteGuard && trackDirtyFlag)
      throw Exception("FDD: host flush forbidden during frame render");
    if (!trackDirtyFlag)
      return true;
    if (nativeSDDWritable || writableTRDCopy) {
      if (!imageFile || writeProtectFlag || bufferedTrack >= nTracks ||
          bufferedSide >= nSides) {
        if (nativeSDDWritable) nativeSDDWriteError = true;
        if (writableTRDCopy) writableTRDWriteError = true;
        return false;
      }
      for (size_t i = 0; i < size_t(nSectorsPerTrack); i++) {
        if (!(flagsBuffer[i] & 0x80)) continue;
        const size_t offs = i * size_t(bytesPerSector);
        const long pos =
            ((nativeSDDWritable ?
                (long(bufferedSide)*long(nTracks)+long(bufferedTrack)) :
                (long(bufferedTrack)*long(nSides)+long(bufferedSide))) *
               long(nSectorsPerTrack)+long(i)) * long(bytesPerSector);
        if (writableTRDCopy) {
#ifdef EP128EMU_TRD_COPY_FAULT_TEST
          // Synthetic-only deterministic failure before touching copy bytes.
          // The pending in-memory track remains dirty until an explicit retry.
          const char* fault = std::getenv("EP128EMU_TEST_TRD_WRITE_FAULT");
          if (fault && *fault == '1') {
            writableTRDWriteError=true;
            return false;
          }
#endif
          // Raw TRD uses normal track-major layout, on a PRIVATE output file.
          // No write-ahead record on this still-experimental copy mode;
          // a failed host write retains the dirty track and rejects eject.
          if (std::fseek(imageFile,pos,SEEK_SET)!=0 ||
              std::fwrite(&(trackBuffer[offs]),1,bytesPerSector,imageFile)
                  != bytesPerSector || !syncNativeSDDFile()) {
            writableTRDWriteError=true;
            return false;
          }
          flagsBuffer[i] &= uint8_t(0x01);
          continue;
        }
        // Write-ahead record MUST reach durable storage before original
        // sector bytes are touched. Record both old and intended payload.
        SDDJournal::Record record = {};
        record.offset=uint32_t(pos);
        std::memcpy(record.desired,&(trackBuffer[offs]),256);
        if (SDDJournal::pending(imageFileName)) {
          SDDJournal::Record prior = {};
          if (!SDDJournal::read(imageFileName,prior) ||
              prior.offset!=record.offset ||
              std::memcmp(prior.desired,record.desired,256)!=0) {
            nativeSDDWriteError=true;
            return false;
          }
          // A retry must not overwrite a disk edited elsewhere after the
          // pending record was created. Target sector may legitimately be
          // OLD, NEW or partially written; other sectors must be unchanged.
          std::vector<uint8_t> current(655360U);
          if (std::fseek(imageFile,0L,SEEK_SET)!=0 ||
              std::fread(current.data(),1,current.size(),imageFile)!=current.size() ||
              SDDJournal::hashOtherSectors(current.data(),current.size(),
                                           prior.offset)!=prior.otherSectorsHash) {
            nativeSDDWriteError=true;
            return false;
          }
          record=prior;              // safe retry after transient sync error
        }
        else {
          std::vector<uint8_t> image(655360U);
          if (std::fseek(imageFile,0L,SEEK_SET)!=0 ||
              std::fread(image.data(),1,image.size(),imageFile)!=image.size()) {
            nativeSDDWriteError=true;
            return false;
          }
          std::memcpy(record.previous,image.data()+record.offset,256);
          record.otherSectorsHash=SDDJournal::hashOtherSectors(
              image.data(),image.size(),record.offset);
          if (!SDDJournal::stage(imageFileName,record)) {
            nativeSDDWriteError=true;
            return false;
          }
        }
        if (std::fseek(imageFile, pos, SEEK_SET) != 0 ||
            std::fwrite(&(trackBuffer[offs]), 1, bytesPerSector, imageFile)
                 != bytesPerSector || !syncNativeSDDFile() ||
            !SDDJournal::clear(imageFileName)) {
          nativeSDDWriteError = true;
          return false;             // durable WAL retained if sync fails
        }
        flagsBuffer[i] &= uint8_t(0x01);
      }
      trackDirtyFlag = false;
      nativeSDDWriteError = false;
      return true;
    }
    if (bufferedTrack >= nTracks || bufferedSide >= nSides ||
        writeProtectFlag || !imageFile) {
      clearDirtyFlag();
      return false;
    }
    uint8_t lastSector = 0;
    bool    errorFlag = false;
    while (true) {
      uint8_t firstSector = 0;
      for (uint8_t i = lastSector + 1; i <= nSectorsPerTrack; i++) {
        uint8_t tmp = flagsBuffer[i - 1];
        if (tmp & 0x80) {
          if (!firstSector)
            firstSector = i;
          lastSector = i;
        }
        else if (!tmp) {
          if (firstSector)
            break;
        }
      }
      if (!firstSector) {
        clearDirtyFlag();
        return (!errorFlag);
      }
      size_t  offs = size_t(firstSector - 1) * size_t(bytesPerSector);
      size_t  nBytes = size_t(lastSector + 1 - firstSector) *
                       size_t(bytesPerSector);
      long    filePos =
          (long(bufferedTrack) * long(nSides) + long(bufferedSide))
          * (long(nSectorsPerTrack) * long(bytesPerSector))
          + long(offs);
      if (std::fseek(imageFile, filePos, SEEK_SET) < 0) {
        errorFlag = true;
      }
      else {
        size_t  bytesWritten = std::fwrite(&(trackBuffer[offs]),
                                           sizeof(uint8_t), nBytes, imageFile);
        if (bytesWritten != nBytes)
          errorFlag = true;
      }
    }
    return false;       // not reached
  }

  uint8_t FloppyDrive::getLEDState_()
  {
    if (EP128EMU_UNLIKELY(ledStateCounter == 0U))
      return 0x00;
    if (ledStateCounter > ledStateCount1) {
      if (!trackDirtyFlag) {
        ledStateCounter = 0U;
        return 0x00;
      }
      if (--ledStateCounter == ledStateCount1) {
        ledStateCounter++;
        if (!flushTrack() && requiresDurableWrite())
          return 0x01;                // pending data remains dirty
        ledStateCounter = 0U;
        return 0x00;
      }
      return 0x01;
    }
    if (motorOnInput) {
      if (ledStateCounter < ledStateCount1)
        ledStateCounter++;
    }
    else if (--ledStateCounter == 0U) {
      isMotorOn = false;
      if (trackDirtyFlag) {
        ledStateCounter = ledStateCount2;
        return 0x01;
      }
      return 0x00;
    }
    return 0x03;
  }

  void FloppyDrive::reset()
  {
    if (nativeSDDWritable && !syncNativeSDD())
      throw Exception("FDD: writable SDD flush failed during reset");
    if (writableTRDCopy && !syncWritableTRDCopy())
      throw Exception("FDD: writable TRD copy flush failed during reset");
    if (!nativeSDDWritable && !writableTRDCopy)
      (void) flushTrack();             // preserve legacy behavior
    currentTrack = 0;
    currentSide = 0;
    bufferedTrack = 0xFF;
    bufferedSide = 0xFF;
    if (imageFile) {
      isMotorOn = true;
      ledStateCounter = ledStateCount1;
    }
    else {
      isMotorOn = false;
      ledStateCounter = 0U;
    }
    bufPos = -1L;
    clearFlagsBuffer();
  }

}       // namespace Ep128Emu

#ifdef WIN32

#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fread
#undef fwrite
#undef setvbuf

namespace std {

  FILE *ep_fopen(const char *filename, const char *mode)
  {
    wchar_t tmpBuf[512];
    wchar_t *fileName_ = &(tmpBuf[0]);
    Ep128Emu::convertUTF8(fileName_, filename, 512);
    HANDLE  hF = 0;
    // Physical Windows floppies historically require direct uncached I/O.
    // Normal disk-image FILES must not use NO_BUFFERING: ReadFile then
    // rejects 128/256-byte sectors and unaligned track-cache addresses.
    const bool physicalDevice =
        (filename[0] == '\\' && filename[1] == '\\' &&
         filename[2] == '.' && filename[3] == '\\');
    const DWORD fileFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH |
        (physicalDevice ? FILE_FLAG_NO_BUFFERING : 0);

    if (!strcmp(mode, "rb")) {
      hF = CreateFileW(LPCWSTR(fileName_), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                       fileFlags, 0);
    }
    else if (!strcmp(mode, "r+b")) {
      hF = CreateFileW(LPCWSTR(fileName_), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                       fileFlags, 0);

      if (filename[0] == '\\' && filename[1] == '\\' &&
          filename[2] == '.' && filename[3] == '\\') {
        DWORD bytes;
        BOOL  ret = DeviceIoControl(hF, FSCTL_LOCK_VOLUME,
                                    0, 0, 0, 0, &bytes, 0);

        if (!ret) {
          CloseHandle(hF);
          hF = 0;
        }
      }
    }

    // Win32 CreateFileW failure is INVALID_HANDLE_VALUE (-1), not NULL.
    // A regular read-only image must fall back from r+b to rb so the
    // virtual FDD can report write protection and preserve original media.
    // Never expose an invalid Win32 HANDLE as a non-null FILE*.
    return (hF == INVALID_HANDLE_VALUE || !hF ? (FILE *) 0 : (FILE *) hF);
  }

  int ep_fclose(FILE *stream)
  {
    if (CloseHandle(HANDLE(stream)))
      return 0;
    else
      return EOF;
  }

  int ep_fseek(FILE *stream, long int offset, int origin)
  {
    DWORD method = 0;

    switch (origin) {
    case SEEK_SET:
      method = FILE_BEGIN;
      break;
    case SEEK_CUR:
      method = FILE_CURRENT;
      break;
    case SEEK_END:
      method = FILE_END;
      break;
    }

    DWORD ret = SetFilePointer(HANDLE(stream), offset, 0, method);

    return (ret != INVALID_SET_FILE_POINTER ? 0 : 1);
  }

  long int ep_ftell(FILE *stream)
  {
    DWORD ret = SetFilePointer(HANDLE(stream), 0, 0, FILE_CURRENT);
    return ret;
  }

  size_t ep_fread(void *ptr, size_t size, size_t count, FILE *stream)
  {
    DWORD num = 0;
    ReadFile(HANDLE(stream), ptr, size * count, &num, 0);
    return num;
  }

  size_t ep_fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
  {
    DWORD num = 0;
    WriteFile(HANDLE(stream), ptr, size * count, &num, 0);
    FlushFileBuffers(HANDLE(stream));
    return num;
  }

  int ep_setvbuf(FILE *stream, char *buffer, int mode, size_t size)
  {
    return 0;
  }

}       // namespace std

#endif  // WIN32

