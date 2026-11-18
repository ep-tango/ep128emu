# Updating an integration from 1.1.0 to 1.1.4

The complete package can replace the old module directory. Preserve and reapply any host-local integration changes. An accompanying unified patch targets the exact `2dfx_portable_emulator/` directory from `2dfx_portable_emulator_complete_source_MIT.zip`.

From inside that old module directory:

```sh
git apply --check /path/to/2dfxemu_v1_1_0_to_v1_1_4.patch
git apply /path/to/2dfxemu_v1_1_0_to_v1_1_4.patch
```

The patch also works with `patch -p1` on an unmodified baseline. If the host modified the old module, review conflicts instead of forcing application. Delete any old build directory and rebuild. `SOURCE_MANIFEST.sha256` describes the resulting release tree.

## Host-side changes

The `twodfx_emu_*` C interface and ABI version 1 are retained. CMake now compiles 22 demo units from `src/demos`; manual source lists must use the new paths, include the new D4 graphics file, and remove the former D4 scope and DF files. New helpers are `.inc` files included by the core, not separate compilation units. `portable_support.h` replaces the old support headers.

Prefer `twodfx_adapter.hpp` and `TwoDfxAdapter` in new integrations. The old adapter remains an alias. Save-state format is now 15 and includes independently maintained demo state. Existing format-13 peripheral chunks are rejected.

Remove host workarounds for NICK conversion or unwritten drawing slots. NICK/TVC converted uploads now produce the correct internal indices; every reset 2DPT slot contains RET. An unwritten gap therefore terminates drawing. Insert explicit NOP commands when a guest program deliberately needs gaps.

UNZX1 requests now enter a 16-job FIFO; overflow rejects only the new request and sets sticky STATUS5. `53h` acknowledges the latch. Keep calling HSYNC on every emulated scanline, including during uploads, with ENGINE_OFF and with raster interrupts disabled. Each callback services the entire FIFO; VSYNC is a fallback. No added guest delay or handshake is required for independent source regions. The C interface is unchanged. Saved state includes the queue and overrun latch.

## Guest program changes

Byte counts below exclude the F8h opcode and include the slot for drawing commands. All coordinates marked XYZ are signed 16-bit little-endian.

| Command | 1.1.4 F9h layout |
|---|---|
| TRIANGLE/FILL_TRIANGLE, 75h/76h | slot, H01, X0, Y0, X1, Y1, H2, X2, Y2 |
| DEFINE_MESH, 86h | id, address low, mid, high7, vertex count−1, triangle-count highbits, triangle-count low8 |
| SET_CAMERA_POSE, 88h | Xlo, Xhi, Ylo, Yhi, Zlo, Zhi, angle highbits, roll low8, yaw low8, pitch low8 |
| DRAW_MESH, 7Ah..7Ch | slot, Xlo, Xhi, Ylo, Yhi, Zlo, Zhi, angle highbits, roll low8, yaw low8, pitch low8, mesh id |

DEFINE_MESH's two triangle bytes encode **count−1**, high part first; only bit0 of the high byte is used. Angle highbits: roll in bits5..4, yaw in bits3..2, pitch in bits1..0; bits7..6 ignored. Do not retain the old contiguous 30-bit angle packing.

TRIANGLE uses H01 bits7..6 for native stroke width−1 (1..4 pixels), with paper fill when paper differs from the transparent key. FILL_TRIANGLE retains half-open pixel-centre coverage.

PATTERN_FILL and PATTERN_BUCKET_FILL use their first coordinate/highbits byte: bit6 selects screen-relative phase; bit7 skips source pixels equal to the transparent key. Clear bit6 means rectangle-local or seed-local phase. Bits live in the highbits byte, **not the pattern-bank byte**. Set bit6 for an older screen-relative bucket effect. Odd X/width in PATTERN_FILL are preserved.

SHOW_BUILTIN_LOGO 90h uses highbits bit7 for the 103 × 40 logo; clear selects 206 × 79. Pointer commands 92h..95h are additive. For exact layouts and unchanged commands, see `COMMAND_REFERENCE.md`.

D4 now selects the six-scene graphics demo, D5 the updated public-mesh tunnel. DF and D6 are unassigned. There is no pointer egg. Existing applications that explicitly set projection values keep those settings; applications using reset defaults receive FX=579/508 for Enterprise/TVC.
