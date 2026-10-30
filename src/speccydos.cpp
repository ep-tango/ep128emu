// EF07A isolated SpeccyDOS peripheral model.
// Behaviour researched against MAME speccydos.cpp (BSD-3-Clause),
// the original SpeccyDOS manual, and X128. No upstream code copied.
#include "speccydos.hpp"
#include <algorithm>
#include <stdexcept>

namespace ZX128 {
  SpeccyDOSBus::SpeccyDOSBus()
    : firmware(), ram(), firmwarePresent(false), wd1772Firmware(false),
      romSelected(false),
      singleDensity(false), controlRegister(0),
      typeIBusyCyclesRemaining(0U), ulaClockHz(886724U),
      controller(), drives()
  {
    firmware.fill(0xFFU);
    ram.fill(0U);
    reset(true);
  }

  uint8_t SpeccyDOSBus::decodeROMByte(uint8_t raw)
  {
    // Board physically swaps ROM chip data bits D1 and D5.
    return uint8_t((raw & ~0x22U) |
                   ((raw & 0x02U) << 4) |
                   ((raw & 0x20U) >> 4));
  }

  bool SpeccyDOSBus::loadFirmware(const std::vector<uint8_t>& raw,
                                    bool alreadyDecoded)
  {
    if (raw.size() != firmware.size())
      return false;
    // ROM dumps appear in either physical EPROM pin order (MAME) or
    // as already decoded Z80 bytes (TOSEC 4.1). Normalize to physical
    // pin order internally; readMemory() always applies the real D1/D5
    // hardware swap exactly once.
    if (!alreadyDecoded)
      std::copy(raw.begin(), raw.end(), firmware.begin());
    else
      for (size_t i = 0; i < raw.size(); i++)
        firmware[i] = decodeROMByte(raw[i]);
    // Exact 8192-byte physical-pin EPROM fingerprint for the original
    // SpeccyDOS v4.2 WD1772 BIOS (MAME sdos422.bin, SHA1
    // e72d2ea19cbe8f3459e4fc9d3c57975dc9917bd6). Use FNV-1a64
    // here to avoid adding a platform crypto library dependency.
    // This is firmware identification, not a security/authenticity check.
    uint64_t id = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < firmware.size(); i++) {
      id ^= uint64_t(firmware[i]);
      id *= UINT64_C(1099511628211);
    }
    wd1772Firmware = (id == UINT64_C(0xbfdbd2b384688a7e));
    firmwarePresent = true;
    reset(true);
    return true;
  }

  void SpeccyDOSBus::reset(bool cold)
  {
    romSelected = false;
    singleDensity = false;
    controlRegister = 0U;
    typeIBusyCyclesRemaining = 0U;
    controller.reset(cold);
    controller.setFloppyDrive(&drives[0]);
    // RAM persists over warm reset, like the physical expansion SRAM.
  }

  void SpeccyDOSBus::setULACycleFrequency(uint32_t hz)
  {
    ulaClockHz = hz > 0U ? hz : 886724U;
  }

  void SpeccyDOSBus::preOpcodeFetch(uint16_t pc)
  {
    if (!firmwarePresent || (controlRegister & 0x40U))
      return;
    if (pc == 0x0066U || pc == 0x1292U || pc == 0x1B41U)
      romSelected = true;
  }

  bool SpeccyDOSBus::readMemory(uint16_t addr, uint8_t& value)
  {
    if (!firmwarePresent || !romSelected || addr >= 0x4000U)
      return false;
    if (addr < 0x2000U) {
      value = decodeROMByte(firmware[addr]);
    }
    else if (addr < 0x2400U) {
      value = ram[addr - 0x2000U];
    }
    else if (addr >= 0x3290U && addr <= 0x3293U) {
      switch (addr & 3U) {
       case 0:
         value = controller.readStatusRegister();
         if (typeIBusyCyclesRemaining != 0U)
           value |= 0x01U;
         break;
       case 1: value = controller.readTrackRegister(); break;
       case 2: value = controller.readSectorRegister(); break;
       default: value = controller.readDataRegister(); break;
      }
    }
    else {
      value = 0xFFU; // unassigned expansion-window address
    }
    return true;
  }

  bool SpeccyDOSBus::writeMemory(uint16_t addr, uint8_t value)
  {
    if (!firmwarePresent)
      return false;
    if (addr == 0x3B41U) {
      // This register is decoded even while expansion ROM is hidden.
      controlRegister = value;
      controller.setFloppyDrive(&drives[value & 3U]);
      drives[value & 3U].setSide((value >> 2) & 1U);
      // SpeccyDOS exposes no separate software motor register. The
      // WD17xx-ready drive must spin after selection, otherwise a real
      // firmware LIST* fails although host-side reads (manually spun)
      // work. This only runs on the optional expansion.
      drives[value & 3U].motorOn();
      // Density bit is recorded here; WD177x legacy core does not yet
      // distinguish FM from MFM track encoding (EF07 acceptance blocker).
      singleDensity = ((value & 0x80U) != 0U);
      romSelected = ((value & 0x40U) == 0U &&
                     (value & 0x08U) != 0U);
      return true;
    }
    if (!romSelected || addr >= 0x4000U)
      return false;
    if (addr >= 0x2000U && addr < 0x2400U) {
      ram[addr - 0x2000U] = value;
    }
    else if (addr >= 0x3290U && addr <= 0x3293U) {
      switch (addr & 3U) {
       case 0:
         controller.writeCommandRegister(value);
         if ((value & 0x80U) == 0U) {
           // WD1770 rates: 6/12/20/30 ms; WD1772 rates: 6/12/2/3 ms
           // for Type-I r1:r0 = 00/01/10/11 (validated against
           // Hatari/EmuTOS and WD1772 documentation, EF07G).
           // The shared controller performs the head step immediately;
           // the optional SpeccyDOS overlay only extends BUSY.
           const uint8_t wd1770Ms[4] = {6U, 12U, 20U, 30U};
           const uint8_t wd1772Ms[4] = {6U, 12U, 2U, 3U};
           const uint8_t *delayMs = (wd1772Firmware ? wd1772Ms : wd1770Ms);
           typeIBusyCyclesRemaining =
               uint32_t((uint64_t(ulaClockHz) *
                         uint64_t(delayMs[value & 3U]) + 999U) / 1000U);
         }
         else {
           typeIBusyCyclesRemaining = 0U;
         }
         break;
       case 1: controller.writeTrackRegister(value); break;
       case 2: controller.writeSectorRegister(value); break;
       default: controller.writeDataRegister(value); break;
      }
    }
    return true; // expansion owns the lower 16K (including ROM writes)
  }

  bool SpeccyDOSBus::identifySDDByLength(std::size_t n,
                                         SpeccyDOSGeometry& g)
  {
    // SpeccyDOS SDD geometries in MAME's sdd_dsk.cpp:
    // 80T 2S 16x256 (MFM) or 35T 1S 16x256 (MFM),
    // and 80T 2S 10x256 (FM). Disk size alone does not encode
    // 3.5in versus 5.25in media or represent exotic geometries.
    if (n == 80U * 2U * 16U * 256U) {
      g.tracks = 80; g.sides = 2; g.sectorsPerTrack = 16;
      g.bytesPerSector = 256; g.singleDensity = false;
      return true;
    }
    if (n == 35U * 1U * 16U * 256U) {
      g.tracks = 35; g.sides = 1; g.sectorsPerTrack = 16;
      g.bytesPerSector = 256; g.singleDensity = false;
      return true;
    }
    if (n == 80U * 2U * 10U * 256U) {
      g.tracks = 80; g.sides = 2; g.sectorsPerTrack = 10;
      g.bytesPerSector = 256; g.singleDensity = true;
      return true;
    }
    return false;
  }

  std::vector<uint8_t>
  SpeccyDOSBus::convertSDDToTrackMajor(
      const std::vector<uint8_t>& source, const SpeccyDOSGeometry& g)
  {
    if (g.tracks < 1 || g.sides < 1 || g.sides > 2 ||
        g.sectorsPerTrack < 1 || g.bytesPerSector != 256)
      throw std::invalid_argument("invalid SpeccyDOS geometry");
    const std::size_t perTrack =
        std::size_t(g.sectorsPerTrack) * std::size_t(g.bytesPerSector);
    if (source.size() != std::size_t(g.tracks) *
                         std::size_t(g.sides) * perTrack)
      throw std::invalid_argument("SpeccyDOS image size mismatch");
    std::vector<uint8_t> result(source.size());
    for (int t=0; t < g.tracks; ++t) {
      for (int side=0; side < g.sides; ++side) {
        const std::size_t src =
            (std::size_t(side) * std::size_t(g.tracks) +
             std::size_t(t)) * perTrack;
        const std::size_t dst =
            (std::size_t(t) * std::size_t(g.sides) +
             std::size_t(side)) * perTrack;
        std::copy(source.begin() + src, source.begin() + src + perTrack,
                  result.begin() + dst);
      }
    }
    return result;
  }
}
