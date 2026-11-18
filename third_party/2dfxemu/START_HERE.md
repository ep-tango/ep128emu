# Portable 2dfx emulator 1.1.4

A complete behavioural 2dfx device for developers integrating 2dfx into **Enterprise or TVC emulators**.

1. Build and test the package using the commands in `README.md`.
2. Link the `twodfx_emu` target and include `src/include/twodfx_emu.h`.
3. Connect the guest's F8h/F9h accesses, HSYNC, VSYNC, interrupt line and indexed video overlay as described in `docs/INTEGRATION.md`.
4. Use the generic C++11 connector in `src/adapter/twodfx_adapter.hpp` if convenient. The C interface works independently of it.
5. Use [COMMAND_REFERENCE.md](docs/COMMAND_REFERENCE.md) for the complete guest command set and [EGGS.md](docs/EGGS.md) for all built-in demos and their controls.
6. When updating an existing integration, read `docs/UPGRADING.md`; saved peripheral state uses format 15.

All production sources and assets are under `src/`. There are no external runtime dependencies beyond the C standard library. The included eggs follow the final 2dfx 1.1.4 demonstrations, including the six-scene D4 graphics demo and D5 public-mesh tunnel.

Read `LICENSE` and `ASSET_LICENSES.md` when redistributing the package. See `docs/COMPATIBILITY.md` for the behavioural timing boundary.
