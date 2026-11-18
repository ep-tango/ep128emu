# Tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTWODFX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The suite uses explicit checks that remain enabled in Release builds.

- `resource_queue`: all 16 FIFO slots, rejected 17th request, 1,024 wrap cycles, mixed conversion modes, dependent jobs, decoder-error continuation, sticky STATUS5 and its marker, HSYNC/VSYNC servicing, queued/partial-command snapshots, reset, engine OFF, 25fps, demo activation and terminal entry.
- `protocol`: all 256 input values in RAW/NICK/TVC conversion and uploads, ZX1 literals, short/long offsets and overlapping matches, wrapped resources, strict sessions, reset RET slots, cadence, 3D packets/defaults, cursor cache and movement, partial-transaction snapshots, corrupt-snapshot rejection, raster IRQ and terminal SHOW_FW.
- `raster_vectors`: 1,920 expected whole-frame hashes generated from the final 2dfx 1.1.4 reference. Covers both profiles, drawing primitives, text, patterns, tilemaps, odd bitmap stride, crop/stretch, offsets, clipping and all mesh modes. Expected output is fixed data, independent of the running renderer.
- `original_eggs_and_firmware_page`: all supported demo selectors, scene setup and SHOW_FW.
- `demo_state`: independent devices, save/restore continuation for every applicable egg, all six D4 phases, D5 progression and retired selectors.
- `layers`: sprite flips/doubling, clipping, odd-width stride and resource wrap; collision cells and delayed reporting; all 14 built-in pointers, both logo sizes and overlay coverage.
- `cpp_adapter_compiles`: C++11 host connector compilation and ownership.

For GCC or Clang, an additional local build can use `-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and the same flags for C++ and linking. The package does not require the reference source or Python to run these tests.

Integration tests in a host emulator should additionally exercise the real guest I/O path, machine palette, raw video coordinates and combined interrupt line. Hardware render-time measurements are outside this suite.
