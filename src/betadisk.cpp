// EF08D read-only WD17xx-based Beta Disk bus research, not full WD1793 yet.
#include "betadisk.hpp"
#include <sys/stat.h>
#ifndef WIN32
# include <unistd.h>
#endif
#ifdef WIN32
# include <windows.h>
# include <vector>
#endif
namespace ZX128 {
  BetaDiskBus::BetaDiskBus(const std::vector<uint8_t>& romImage)
    : rom(romImage), drive(), fdc(), active(false), systemControl(0x04),
      indexPhase(0), indexPeriod(177344), indexPulseWidth(3547),
      typeIStatusSelected(true), writableCopyActive(false),
      writeTrackNoDataCycles(0)
  {
    if (rom.size() != 16384U)
      throw Ep128Emu::Exception("Beta Disk: ROM must be 16384 bytes");
    fdc.setIsWD1773(true);
    fdc.setFloppyDrive(&drive);
    reset(true);
  }
  void BetaDiskBus::reset(bool cold)
  {
    if (!syncWritableCopy())
      throw Ep128Emu::Exception("Beta Disk: writable TRD copy sync failed during reset");
    active = false;
    systemControl = 0x04;
    indexPhase = 0U;
    typeIStatusSelected = true;
    writeTrackNoDataCycles = 0;
    fdc.reset(cold);
    drive.motorOff();
    drive.setSide(0);
  }
  void BetaDiskBus::setULACycleFrequency(uint32_t hz)
  {
    // WD1793 index-hole signal: approximately 5 revolutions per second,
    // with an active pulse around 4 ms, driven by emulated ULA time.
    indexPeriod = (hz >= 5U ? hz / 5U : 1U);
    indexPulseWidth = (hz >= 250U ? hz / 250U : 1U);
    if (indexPulseWidth > indexPeriod) indexPulseWidth = indexPeriod;
    indexPhase = 0U;
  }

  void BetaDiskBus::clockULACycle()
  {
    if (++indexPhase >= indexPeriod) indexPhase = 0U;
    if (writeTrackNoDataCycles != 0U) {
      // A physical WD1793 WRITE TRACK without a byte stream finishes
      // with lost-data status, rather than holding BUSY and DRQ forever.
      // Allow two full revolutions for the guest to begin formatting.
      if (writeTrackNoDataCycles++ >= (2U * indexPeriod)) {
        writeTrackNoDataCycles = 0U;
        if (fdc.getDataRequestFlag() &&
            (fdc.readStatusRegisterDebug() & 0x01U) != 0U) {
          fdc.writeCommandRegister(0xD8U);
          typeIStatusSelected = true;
        }
      }
    }
  }

  void BetaDiskBus::preOpcodeFetch(uint16_t pc, bool basicROMVisible)
  {
    if (pc >= 0x4000U) active = false;
    else if ((pc & 0xFF00U) == 0x3D00U && basicROMVisible) active = true;
  }
  bool BetaDiskBus::handlesPort(uint16_t port) const
  {
    if (!active) return false;
    switch (port & 0xFFU) {
    case 0x1F: case 0x3F: case 0x5F: case 0x7F: case 0xFF: return true;
    }
    return false;
  }
  uint8_t BetaDiskBus::readPort(uint16_t port, bool debug)
  {
    switch (port & 0xFFU) {
    case 0x1F: {
      uint8_t v = (debug ? fdc.readStatusRegisterDebug() : fdc.readStatusRegister());
      if (!drive.haveDisk() || !drive.getIsMotorOn()) v |= 0x80U;
      else v &= 0x7FU;
      // Index-hole pulse is a physical input only for Type I/IV status.
      // On Type II/III the same bit is DRQ and must not be touched.
      if (typeIStatusSelected) {
        v &= 0xFDU;
        if (getIndexPulseForTesting()) v |= 0x02U;
      }
      return v;
    }
    case 0x3F: return fdc.readTrackRegister();
    case 0x5F: return fdc.readSectorRegister();
    case 0x7F: return (debug ? fdc.readDataRegisterDebug() : fdc.readDataRegister());
    case 0xFF: return uint8_t((fdc.getDataRequestFlag() ? 0x40U : 0U) |
                              (fdc.getInterruptRequestFlag() ? 0x80U : 0U));
    }
    return 0xFF;
  }
  void BetaDiskBus::writePort(uint16_t port, uint8_t value)
  {
    switch (port & 0xFFU) {
    case 0x1F:
      // A busy WD1793 ignores unrelated commands; they must not disable
      // the active no-data WRITE TRACK guard. FORCE INTERRUPT cancels it.
      if ((fdc.readStatusRegisterDebug() & 0x01U) == 0U ||
          (value & 0xF0U) == 0xD0U)
        writeTrackNoDataCycles = 0U;
      // Only explicit, verified *private* writable-copy mounts may write.
      // The original/read-only path retains the historical force-interrupt.
      if (!writableCopyActive &&
          ((value & 0xE0U) == 0xA0U || (value & 0xF0U) == 0xF0U)) {
        // The staged disk is immutable: do not execute a write/format.
        // However the original TR-DOS does issue WRITE TRACK while
        // recovering from an absent disk. A silent drop leaves it polling
        // for an INTRQ that never arrives (EF08T, after Abort -> Enter).
        // Complete the rejected operation in the Beta-only WD1793 facade.
        fdc.writeCommandRegister(0xD8U);
        typeIStatusSelected = true;
        break;
      }
      fdc.writeCommandRegister(value);
      if (writableCopyActive && (value & 0xF0U) == 0xF0U &&
          fdc.getDataRequestFlag() &&
          (fdc.readStatusRegisterDebug() & 0x01U) != 0U)
        writeTrackNoDataCycles = 1U;
      // WD1793 without any medium must complete a failed Type II/III
      // request. The shared WD177x core leaves BUSY set forever on
      // absent media; the original TR-DOS then spins waiting for INTRQ.
      // Restrict this completion to Beta Disk. Keep actual No-Ready bit7
      // in readPort(1F), so firmware can report "No disk" without reset.
      if (!drive.haveDisk() && (value & 0x80U) != 0U &&
          (value & 0xF0U) != 0xD0U &&
          (fdc.readStatusRegisterDebug() & 0x01U) != 0U) {
        fdc.writeCommandRegister(0xD8U); // WD1793 force immediate INTRQ
      }
      typeIStatusSelected = ((value & 0x80U) == 0U ||
                             (value & 0xF0U) == 0xD0U);
      break;
    case 0x3F: fdc.writeTrackRegister(value); break;
    case 0x5F: fdc.writeSectorRegister(value); break;
    case 0x7F:
      // A genuine FORMAT stream has begun; do not treat it as a probe.
      writeTrackNoDataCycles = 0U;
      fdc.writeDataRegister(value);
      break;
    case 0xFF:
      systemControl = value;
      if (!(value & 0x04U)) {
        writeTrackNoDataCycles = 0U;
        fdc.reset(true);
        drive.motorOff();
      }
      else {
        fdc.setFloppyDrive((value & 3U) ? (Ep128Emu::FloppyDrive *) 0 : &drive);
        // The original Technology Research v5.03 selects the physical
        // lower head with FF bit4=1 (verified against a real LIST).
        // The host FloppyDrive side index is 0 for that lower head.
        drive.setSide(((value >> 4) & 1U) ^ 1U);
        drive.motorOn();
      }
      break;
    }
  }
  bool BetaDiskBus::syncWritableCopy()
  {
    return !writableCopyActive || drive.syncWritableTRDCopy();
  }

  void BetaDiskBus::ejectReadOnly()
  {
    if (!syncWritableCopy())
      throw Ep128Emu::Exception("Beta Disk: writable TRD copy flush failed; eject cancelled");
    writeTrackNoDataCycles = 0U;
    if (!drive.haveDisk())
      return;
    // Removing a real medium drops drive readiness and must terminate
    // controller waits. Without an INTRQ, original TR-DOS remains in its
    // port FF poll loop forever after an in-session Eject (EF08S).
    // The WD1793 Force Interrupt with I3=1 completes the pending request;
    // the next DOS command can report its own media error (Retry/Abort).
    drive.setDiskImageFile("");
    writableCopyActive = false;
    fdc.writeCommandRegister(0xD8U);
    typeIStatusSelected = true;
  }
  void BetaDiskBus::mountReadOnly(const std::string& fileName,
                                   int tracks, int sides)
  {
    if (drive.haveDisk())
      throw Ep128Emu::Exception("Beta Disk: eject current media first");
    if (!((tracks == 40 || tracks == 80) && (sides == 1 || sides == 2)))
      throw Ep128Emu::Exception("Beta Disk: unsupported TRD geometry");
#ifdef WIN32
    // Accept only a native readonly regular disk image, not a directory,
    // junction/reparse point or source writable by Windows. The Linux POSIX
    // stat mode bits cannot reliably represent Win32 FILE_ATTRIBUTE_READONLY.
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               fileName.c_str(), -1, 0, 0);
    if (n <= 1)
      throw Ep128Emu::Exception("Beta Disk: invalid TRD path");
    std::vector<wchar_t> name(size_t(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                           fileName.c_str(), -1, name.data(), n) != n)
      throw Ep128Emu::Exception("Beta Disk: invalid TRD filename");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(name.data(), GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                  FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0 ||
        info.nFileSizeHigh != 0 ||
        info.nFileSizeLow != DWORD(tracks * sides * 16 * 256))
      throw Ep128Emu::Exception("Beta Disk: requires read-only regular TRD image");
#else
    struct stat st;
    if (stat(fileName.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
        (st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0 ||
        st.st_size != off_t(tracks * sides * 16 * 256))
      throw Ep128Emu::Exception("Beta Disk: requires read-only regular TRD image");
#endif
    drive.setDiskImageFile(fileName, tracks, sides, 16, 256);
    if (!drive.getIsWriteProtected()) {
      drive.setDiskImageFile("");
      throw Ep128Emu::Exception("Beta Disk: writable media refused");
    }
    writableCopyActive = false;
  }

  void BetaDiskBus::mountProtectedExisting(const std::string& fileName,
                                            int tracks, int sides)
  {
    if (drive.haveDisk())
      throw Ep128Emu::Exception("Beta Disk: eject current disk first");
    if (!((tracks==80 && sides==2) || (tracks==40 && sides==1)))
      throw Ep128Emu::Exception("Beta Disk: unsupported TRD geometry");
    drive.setDiskImageFile(fileName,tracks,sides,16,256);
    drive.setUserWriteProtected(true);
    writableCopyActive=false;
  }

  void BetaDiskBus::mountWritableTRD(const std::string& fileName,
                                      int tracks, int sides)
  {
    if (drive.haveDisk())
      throw Ep128Emu::Exception("Beta Disk: eject current media first");
    if (!((tracks==80 && sides==2) || (tracks==40 && sides==1)))
      throw Ep128Emu::Exception("Beta Disk: writable TRD copy geometry invalid");
#ifdef WIN32
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                                    fileName.c_str(),-1,0,0);
    if(n<=1)throw Ep128Emu::Exception("Beta Disk: invalid writable TRD path");
    std::vector<wchar_t> w(size_t(n),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                          fileName.c_str(),-1,w.data(),n)!=n)
      throw Ep128Emu::Exception("Beta Disk: invalid writable TRD path");
    WIN32_FILE_ATTRIBUTE_DATA st;
    if(!GetFileAttributesExW(w.data(),GetFileExInfoStandard,&st) ||
       (st.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|
                               FILE_ATTRIBUTE_READONLY))!=0 ||
       st.nFileSizeHigh!=0 ||
       st.nFileSizeLow!=DWORD(tracks*sides*16*256))
      throw Ep128Emu::Exception("Beta Disk: writable TRD file invalid");
#else
    struct stat st;
    if(lstat(fileName.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
       (st.st_mode & S_IWUSR)==0 ||
       st.st_size!=off_t(tracks*sides*16*256))
      throw Ep128Emu::Exception("Beta Disk: writable TRD file invalid");
#endif
    // No conversion or special SDD mapping: raw TRD is track-major.
    drive.setDiskImageFile(fileName,tracks,sides,16,256,false,false,true);
    if(drive.getIsWriteProtected() || !drive.isWritableTRDCopy()) {
      drive.setDiskImageFile("");
      throw Ep128Emu::Exception("Beta Disk: writable TRD file refused");
    }
    writableCopyActive=true;
  }
}
