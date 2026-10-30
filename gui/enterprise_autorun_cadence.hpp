#ifndef EP128EMU_ENTERPRISE_AUTORUN_CADENCE_HPP
#define EP128EMU_ENTERPRISE_AUTORUN_CADENCE_HPP

#include <stdint.h>

namespace Ep128EmuArchiveAutoRun {
  // The macOS T081 headless reference used 50 ms down + 50 ms up.
  // Four completed 50-Hz video frames provide 80 ms per state,
  // independent of FLTK's host-side timer resolution and CPU scheduling.
  // FrameSequenceNumber is monotonic across the emulation session.
  inline bool keyPhaseElapsed(uint64_t current, uint64_t start) {
    return (current - start) >= 4ULL;
  }
}
#endif
