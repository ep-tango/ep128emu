// Portable Speccy-DOS memory-mapped peripheral model (EF07A).
// Experimental and NOT wired into ZX128VM until genuine ROM/media validated.
// Copyright 2026 contributors. GPL-2.0-or-later as part of ep128emu.
#ifndef EP128EMU_SPECCYDOS_HPP
#define EP128EMU_SPECCYDOS_HPP

#include "ep_fdd.hpp"
#include "wd177x.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ZX128 {
  struct SpeccyDOSGeometry {
    int tracks;
    int sides;
    int sectorsPerTrack;
    int bytesPerSector;
    bool singleDensity;
  };

  class SpeccyDOSBus {
   private:
    std::array<uint8_t, 8192> firmware;
    std::array<uint8_t, 1024> ram;
    bool firmwarePresent;
    // Known original v4.2 WD1772 EPROM has a distinct step-rate table.
    // All other firmware retains established WD1770 timing by default.
    bool wd1772Firmware;
    bool romSelected;
    bool singleDensity;
    uint8_t controlRegister;
    // WD1770 Type-I RESTORE/SEEK/STEP remains BUSY for a real step
    // interval. The shared WD177x core completes immediately; this
    // SpeccyDOS-only guest-clock overlay preserves firmware polling.
    uint32_t typeIBusyCyclesRemaining;
    uint32_t ulaClockHz;
    Ep128Emu::WD177x controller;
    Ep128Emu::FloppyDrive drives[4];
   public:
    SpeccyDOSBus();
    // Raw 8-KiB ROM has hardware data lines D1 and D5 swapped.
    // The firmware must be supplied by the user: never embed a ROM.
    bool loadFirmware(const std::vector<uint8_t>& raw,
                      bool alreadyDecoded = false);
    bool hasFirmware() const { return firmwarePresent; }
    bool usesWD1772StepRates() const { return wd1772Firmware; }
    bool isROMSelected() const { return romSelected; }
    bool isSingleDensity() const { return singleDensity; }
    uint8_t getControlRegister() const { return controlRegister; }
    uint8_t getDriveNumber() const { return controlRegister & 3U; }
    static uint8_t decodeROMByte(uint8_t raw);
    void reset(bool cold);
    void preOpcodeFetch(uint16_t pc);
    void setULACycleFrequency(uint32_t hz);
    void clockULACycle() {
      if (typeIBusyCyclesRemaining > 0U)
        --typeIBusyCyclesRemaining;
    }
    uint32_t getPendingTypeIBusyCycles() const
      { return typeIBusyCyclesRemaining; }

    // Return true if the SpeccyDOS expansion owns the access. When false,
    // ordinary Spectrum RAM/ROM memory handling should process the access.
    bool readMemory(uint16_t addr, uint8_t& value);
    bool writeMemory(uint16_t addr, uint8_t value);

    Ep128Emu::WD177x& getController() { return controller; }
    Ep128Emu::FloppyDrive& getDrive(unsigned int n)
      { return drives[n & 3U]; }

    // A .sdd is sector-raw bytes stored SIDE-MAJOR (all tracks of side0,
    // followed by all tracks of side1), unlike ep128emu track-major images.
    // Metadata only: FM/MFM and track interleave must be modelled separately.
    static bool identifySDDByLength(std::size_t nBytes,
                                    SpeccyDOSGeometry& g);
    static std::vector<uint8_t>
      convertSDDToTrackMajor(const std::vector<uint8_t>& source,
                            const SpeccyDOSGeometry& g);
  };
}

#endif
