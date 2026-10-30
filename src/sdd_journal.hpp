// Host-only native SpeccyDOS original-SDD one-sector recovery record.
// WIP: do not expose original-media writable mode without recovery QA.
#ifndef EP128EMU_SDD_JOURNAL_HPP
#define EP128EMU_SDD_JOURNAL_HPP
#include <cstdint>
#include <cstddef>
#include <string>
namespace Ep128Emu { namespace SDDJournal {
  struct Record {
    uint32_t offset;
    uint64_t otherSectorsHash;
    uint8_t previous[256];
    uint8_t desired[256];
  };
  std::string journalPath(const std::string& image);
  bool pending(const std::string& image);  // errors fail closed
  bool read(const std::string& image, Record& record);
  bool stage(const std::string& image, const Record& record);
  bool clear(const std::string& image);
  // Explicit offline recovery only; backs up entire original before changing.
  // Not called by emulator automatically.
  bool inspect(const std::string& image, Record& record, bool& matchesPrevious,
               bool& matchesDesired, bool& otherSectorsMatch);
  bool recover(const std::string& image, bool chooseDesired);
  uint64_t hashOtherSectors(const uint8_t *all, size_t size, uint32_t offset);
}} // namespace
#endif
