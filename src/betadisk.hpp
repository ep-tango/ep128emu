// EF08D experimental ZX Beta Disk read-only bus. No GUI auto-enablement.
#ifndef EP128EMU_BETADISK_HPP
#define EP128EMU_BETADISK_HPP
#include "wd177x.hpp"
#include <vector>
#include <string>
#include <stdint.h>
namespace ZX128 {
class BetaDiskBus {
  std::vector<uint8_t> rom;
  Ep128Emu::FloppyDrive drive;
  Ep128Emu::WD177x fdc;
  bool active;
  uint8_t systemControl;
  uint32_t indexPhase;
  uint32_t indexPeriod;
  uint32_t indexPulseWidth;
  bool typeIStatusSelected;
  bool writableCopyActive;
  // Beta-only WD1793 probe guard: non-serviced WRITE TRACK must time out.
  // Never abort a command after the guest has begun streaming format data.
  uint32_t writeTrackNoDataCycles;
public:
  explicit BetaDiskBus(const std::vector<uint8_t>& romImage);
  void reset(bool cold);
  void preOpcodeFetch(uint16_t pc, bool basicROMVisible);
  void setULACycleFrequency(uint32_t hz);
  void clockULACycle();
  bool getIndexPulseForTesting() const
    { return (drive.haveDisk() && drive.getIsMotorOn() &&
              indexPhase < indexPulseWidth); }
  bool isROMSelected() const { return active; }
  uint8_t readROM(uint16_t addr) const { return rom[addr & 0x3FFFU]; }
  bool handlesPort(uint16_t port) const;
  uint8_t readPort(uint16_t port, bool debug = false);
  void writePort(uint16_t port, uint8_t value);
  void mountReadOnly(const std::string& fileName, int tracks, int sides);
  void mountProtectedExisting(const std::string&,int,int);
  void setWriteProtected(bool v) { drive.setUserWriteProtected(v); }
  bool isWriteProtected() const { return drive.getIsWriteProtected(); }
  // Directly mount an existing user-selected regular .TRD in read/write mode.
  void mountWritableTRD(const std::string& fileName, int tracks, int sides);
  // Compatibility for existing isolated regression harnesses.
  void mountWritableCopy(const std::string& fileName, int tracks, int sides)
    { mountWritableTRD(fileName, tracks, sides); }
  bool syncWritableCopy();
  bool isWritableCopyMounted() const {return writableCopyActive && drive.haveDisk();}
  bool isDiskMounted() const {return drive.haveDisk();}
  void ejectReadOnly();
  bool isReadOnlyMediaMounted() const
  { return drive.haveDisk() && drive.getIsWriteProtected(); }
  uint8_t getSystemControl() const { return systemControl; }
  Ep128Emu::WD177x& getController() { return fdc; }
};
}
#endif
