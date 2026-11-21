// EF08D experimental ZX Beta Disk read-only bus. No GUI auto-enablement.
#ifndef EP128EMU_BETADISK_HPP
#define EP128EMU_BETADISK_HPP
#include "wd177x.hpp"
#include <vector>
#include <array>
#include <stdexcept>
#include <string>
#include <stdint.h>
namespace ZX128 {
class BetaDiskBus {
  std::vector<uint8_t> rom;
  std::array<Ep128Emu::FloppyDrive,4> drives;
  Ep128Emu::WD177x fdc;
  bool active;
  uint8_t systemControl;
  uint32_t indexPhase;
  uint32_t indexPeriod;
  uint32_t indexPulseWidth;
  bool typeIStatusSelected;
  std::array<bool,4> writableCopyActive;
  // UI activity pulses are independent of WD1793 motor/ready semantics.
  // 81 x 2 ms aligns with Enterprise's shared FloppyDrive LED interval.
  std::array<uint8_t,4> ledActivityTicks;
  Ep128Emu::FloppyDrive& driveAt(unsigned int n) {
    if (n >= 4U) throw std::out_of_range("Beta Disk: drive index out of range");
    return drives[n];
  }
  const Ep128Emu::FloppyDrive& driveAt(unsigned int n) const {
    if (n >= 4U) throw std::out_of_range("Beta Disk: drive index out of range");
    return drives[n];
  }
  // Beta-only WD1793 probe guard: non-serviced WRITE TRACK must time out.
  // Never abort a command after the guest has begun streaming format data.
  uint32_t writeTrackNoDataCycles;
public:
  explicit BetaDiskBus(const std::vector<uint8_t>& romImage);
  void reset(bool cold);
  void preOpcodeFetch(uint16_t pc, bool basicROMVisible);
  void setULACycleFrequency(uint32_t hz);
  void clockULACycle();
  unsigned int getSelectedDriveNumber() const { return unsigned(systemControl & 3U); }
  bool consumeDriveActivityLED(unsigned int n) {
    uint8_t& ticks=ledActivityTicks[n & 3U];
    if(ticks==0U) return false;
    --ticks;
    return true;
  }
  Ep128Emu::FloppyDrive& getDrive(unsigned int n) { return driveAt(n); }
  const Ep128Emu::FloppyDrive& getDrive(unsigned int n) const { return driveAt(n); }
  bool getIndexPulseForTesting() const
    { const Ep128Emu::FloppyDrive& d = drives[getSelectedDriveNumber()];
      return d.haveDisk() && d.getIsMotorOn() &&
             indexPhase < indexPulseWidth; }
  bool isROMSelected() const { return active; }
  uint8_t readROM(uint16_t addr) const { return rom[addr & 0x3FFFU]; }
  bool handlesPort(uint16_t port) const;
  uint8_t readPort(uint16_t port, bool debug = false);
  void writePort(uint16_t port, uint8_t value);
  // New explicit slot-indexed media API; the no-index overloads keep all
  // existing one-drive ROM, GUI and headless A-drive tests byte-for-byte.
  void mountReadOnly(unsigned int slot,const std::string& fileName,int tracks,int sides);
  void mountProtectedExisting(unsigned int slot,const std::string&,int,int);
  void mountWritableTRD(unsigned int slot,const std::string& fileName,int tracks,int sides);
  void ejectReadOnly(unsigned int slot);
  bool syncWritableCopy(unsigned int slot);
  bool syncWritableCopy();
  void mountReadOnly(const std::string& name,int tracks,int sides)
    { mountReadOnly(0U,name,tracks,sides); }
  void mountProtectedExisting(const std::string& name,int tracks,int sides)
    { mountProtectedExisting(0U,name,tracks,sides); }
  void mountWritableTRD(const std::string& name,int tracks,int sides)
    { mountWritableTRD(0U,name,tracks,sides); }
  void ejectReadOnly() { ejectReadOnly(0U); }
  void setWriteProtected(unsigned int n,bool v) { driveAt(n).setUserWriteProtected(v); }
  void setWriteProtected(bool v) { setWriteProtected(0U,v); }
  bool isWriteProtected(unsigned int n) const { return driveAt(n).getIsWriteProtected(); }
  bool isWriteProtected() const { return isWriteProtected(0U); }
  void mountWritableCopy(const std::string& name,int tracks,int sides)
    { mountWritableTRD(0U,name,tracks,sides); }
  bool isWritableCopyMounted(unsigned int n) const
    { return driveAt(n).haveDisk() && writableCopyActive[n]; }
  bool isWritableCopyMounted() const { return isWritableCopyMounted(0U); }
  bool isDiskMounted(unsigned int n) const { return driveAt(n).haveDisk(); }
  bool isDiskMounted() const { return isDiskMounted(0U); }
  bool isReadOnlyMediaMounted(unsigned int n) const
    { return driveAt(n).haveDisk() && driveAt(n).getIsWriteProtected(); }
  bool isReadOnlyMediaMounted() const { return isReadOnlyMediaMounted(0U); }
  uint8_t getSystemControl() const { return systemControl; }
  Ep128Emu::WD177x& getController() { return fdc; }
};
}
#endif
