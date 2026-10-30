#ifndef EP128EMU_SDD_SHUTDOWN_GUARD_HPP
#define EP128EMU_SDD_SHUTDOWN_GUARD_HPP
#include "ep_fdd.hpp"

// Shared host-media stop gate, used by actual GUI shutdown/switch routes AND
// the synthetic fault regression. Never stop on unsynced original SDD data.
// Read-only and unrelated legacy floppy types require no new sync behavior.
inline bool ep128emuSDDCanGracefullyStop(Ep128Emu::FloppyDrive& disk)
{
  return !disk.isNativeSDDWritable() || disk.syncNativeSDD();
}

#endif
