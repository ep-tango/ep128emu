# Portable 2dfx emulator — 1.1.4

A portable C99 behavioural model of 2dfx hardware for Enterprise and TVC emulator integrations. It includes the device's command parser, 8 MiB resource RAM, sprites, both retained 2D drawing layers, indexed-mesh rendering, cursor and logo overlays, raster interrupts, collision reporting, built-in demonstrations and the SHOW_FW information page.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake 3.16 or later and a C99 compiler are required. Tests also build a C++11 connector example. When used with `add_subdirectory`, tests default to off; enable them with `-DTWODFX_BUILD_TESTS=ON`.

```cmake
add_subdirectory(path/to/2dfx_portable_emulator)
target_link_libraries(your_emulator PRIVATE twodfx_emu)
```

The host-facing C ABI remains version 1. Guest command semantics are **2dfx 1.1.4**. Use `TWODFX_EMU_VERSION` for the package version, not the ABI constant.

- `START_HERE.md`: integration entry point.
- `docs/INTEGRATION.md`: ports, video, IRQ and lifecycle.
- `docs/COMMAND_REFERENCE.md`: byte layouts and current semantics.
- `docs/UPGRADING.md`: replacement and patch integration.
- `docs/EGGS.md`: the 22 current demonstrations.
- `docs/SNAPSHOT_FORMAT.md`: saved state.
- `docs/TESTING.md`: reproducible tests.

The module emits palette indices and coverage. The host applies the selected machine's palette and video behaviour. See `LICENSE`, `ASSET_LICENSES.md` and `CREDITS.md` for the code and asset terms.

UNZX1 uses a 16-job FIFO with sticky STATUS5 on overflow. The existing HSYNC callback services queued resources independently of rendering; no guest-side handshake is added. See `docs/INTEGRATION.md` for callback scheduling and `docs/COMMAND_REFERENCE.md` for resource lifetime rules.
