
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

#ifndef EP128EMU_EP_FDD_HPP
#define EP128EMU_EP_FDD_HPP

#include "ep128emu.hpp"
#include <vector>

namespace Ep128Emu {

  /*!
   * Check if the disk image specified in 'fileName' (should not be NULL) is
   * a real floppy disk, and set (if zero or negative) or verify (if positive)
   * the geometry parameters.
   * Returns one of the following values:
   *   -2: the specified geometry does not match the actual disk parameters
   *   -1: error opening file or device
   *    0: the file is not a disk device
   *    1: no error, disk is write protected
   *    2: no error, disk is not write protected
   */
  extern int checkFloppyDisk(const char *fileName,
                             int& nTracks, int& nSides, int& nSectorsPerTrack);

  class FloppyDrive {
   private:
    static const uint32_t ledStateCount1 = 81U;         // 162 ms
    static const uint32_t ledStateCount2 = 528U;        // 1056 ms
    std::string imageFileName;
    std::FILE   *imageFile;
    uint8_t     nTracks;
    uint8_t     nSides;
    uint8_t     nSectorsPerTrack;
    // EF06A: physical bytes per logical sector (128, 256, 512, 1024).
    // Remains 512 unless a caller explicitly opts in to a different size.
    uint16_t    bytesPerSector;
    uint8_t     currentTrack;
    uint8_t     currentSide;
    bool        writeProtectFlag;
    bool        userWriteProtected;
    // Explicit SpeccyDOS-only raw side-major media; always host read-only.
    bool        sideMajorSDDReadOnly;
    bool        nativeSDDWritable;
    bool        nativeSDDWriteError;
    // Isolated Beta Disk copy only: never active on original/legacy images.
    bool        writableTRDCopy;
    bool        writableTRDWriteError;
    bool syncNativeSDDFile();
    bool        diskChangeFlag;
    uint8_t     bufferedTrack;          // track stored in buffer, 0xFF: none
    uint8_t     bufferedSide;           // side stored in buffer, 0xFF: none
    bool        motorOnInput;
    bool        isMotorOn;
    uint32_t    ledStateCounter;
    std::vector< uint32_t > buf_;
    uint8_t     *trackBuffer;
    // for each sector of the buffered track:
    // bit 0: sector data is valid, bit 7: sector is "dirty" (not flushed)
    uint8_t     *flagsBuffer;
    uint8_t     *tmpBuffer;
    long        bufPos;                 // position in track buffer, -1: none
    bool        trackDirtyFlag;
    // ----------------
    void closeDiskImage();
    uint8_t getLEDState_();
   public:
    FloppyDrive();
    virtual ~FloppyDrive();
    virtual void setDiskImageFile(const std::string& fileName_,
                                  int nTracks_ = -1,
                                  int nSides_ = 2,
                                  int nSectorsPerTrack_ = 9,
                                  int bytesPerSector_ = 512,
                                  bool sideMajorSDDReadOnly_ = false,
                                  bool nativeSDDWritable_ = false,
                                  bool writableTRDCopy_ = false);
    inline void setDiskChangeFlag(bool isChanged)
    {
      diskChangeFlag = isChanged;
    }
    inline bool getDiskChangeFlag() const
    {
      return diskChangeFlag;
    }
    inline void setSide(int n)
    {
      currentSide = uint8_t(n & 1);
    }
    inline bool haveDisk() const
    {
      return (imageFile != (std::FILE *) 0);
    }
    inline bool getIsWriteProtected() const
    {
      return writeProtectFlag || userWriteProtected || nativeSDDWriteError || writableTRDWriteError;
    }
    inline void setUserWriteProtected(bool v) { userWriteProtected=v; }
    inline bool getUserWriteProtected() const { return userWriteProtected; }
    inline bool isNativeSDDWritable() const { return nativeSDDWritable; }
    inline bool isWritableTRDCopy() const { return writableTRDCopy; }
    inline bool requiresDurableWrite() const
    { return nativeSDDWritable || writableTRDCopy; }
    bool syncWritableTRDCopy();
    bool syncNativeSDD();
    inline void motorOn()
    {
      motorOnInput = true;
      isMotorOn = true;
      if (ledStateCounter < ((ledStateCount1 * 3U) >> 2) ||
          ledStateCounter > ledStateCount1) {
        ledStateCounter = (ledStateCount1 * 3U) >> 2;
      }
    }
    inline void motorOff()
    {
      motorOnInput = false;
    }
    inline bool getIsMotorOn() const
    {
      return isMotorOn;
    }
    inline bool getIsReady() const
    {
      return (imageFile != (std::FILE *) 0 && bool(ledStateCounter & 1U));
    }
    // returns 0: black (off), 1: red, 2: green, 3: yellow-green
    // should be called at a rate of 500 Hz
    inline uint8_t getLEDState()
    {
      if (EP128EMU_UNLIKELY(ledStateCounter))
        return getLEDState_();
      return 0x00;
    }
    virtual void reset();
    inline uint8_t getSectorsPerTrack() const
    {
      return nSectorsPerTrack;
    }
    inline uint16_t getBytesPerSector() const
    {
      return bytesPerSector;
    }
    inline uint8_t getSectorLengthCode() const
    {
      // WD17xx sector length code: 0=128, 1=256, 2=512, 3=1024.
      return uint8_t(bytesPerSector == 128 ? 0 :
                     bytesPerSector == 256 ? 1 :
                     bytesPerSector == 512 ? 2 : 3);
    }
    inline uint8_t getCurrentTrack() const
    {
      return currentTrack;
    }
    inline uint8_t getCurrentSide() const
    {
      return currentSide;
    }
    inline bool checkCurrentTrack(uint8_t trackNum) const
    {
      return (isMotorOn && currentTrack < nTracks && currentSide < nSides &&
              currentTrack == trackNum);
    }
    // n > 0: step in, n < 0: step out
    void doStep(int n);
    inline bool getIsTrack0() const
    {
      return (currentTrack == 0);
    }
    // returns true on success
    bool startDataTransfer(uint8_t trackNum, uint8_t sectorNum);
    inline void endDataTransfer()
    {
      bufPos = -1L;
    }
    inline bool isDataTransfer() const
    {
      return (bufPos >= 0L);
    }
    inline long getSectorBufferPosition() const
    {
      return (bufPos >= 0L ? (bufPos % long(bytesPerSector)) : bufPos);
    }
    // returns true at end of sector
    inline bool readByte(uint8_t& b)
    {
      b = trackBuffer[bufPos];
      bufPos++;
      return ((bufPos % long(bytesPerSector)) == 0L);
    }
    inline bool writeByte(uint8_t b)
    {
      trackDirtyFlag = true;
      trackBuffer[bufPos] = b;
      flagsBuffer[bufPos / long(bytesPerSector)] = 0x81;
      bufPos++;
      return ((bufPos % long(bytesPerSector)) == 0L);
    }
    // pad the rest of the current sector with zero bytes
    // called when a write command is aborted
    void padSector();
   private:
    void copySector(int n);
    void clearDirtyFlag();
    void clearFlagsBuffer();
    void clearBuffer(uint8_t *buf);
    bool updateBufferedTrack_();
    bool readTrack();
   public:
    bool flushTrack();
    inline bool updateBufferedTrack()
    {
      if (currentTrack != bufferedTrack || currentSide != bufferedSide)
        return updateBufferedTrack_();
      return true;
    }
    inline bool getSectorData()
    {
      if (!flagsBuffer[bufPos / long(bytesPerSector)])
        return readTrack();
      return true;
    }
  };

}       // namespace Ep128Emu

#endif  // EP128EMU_EP_FDD_HPP

