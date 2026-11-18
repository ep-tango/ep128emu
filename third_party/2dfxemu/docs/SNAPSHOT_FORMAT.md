# Saved state — format 15

Use `twodfx_emu_snapshot_size`, `twodfx_emu_save_snapshot` and `twodfx_emu_load_snapshot`. The host C ABI remains version 1; the serialized format version is separate.

The 20-byte header contains `2DFXSNAP` (8 bytes), format 15 as little-endian u32, body length as little-endian u32, and IEEE CRC-32 of the body as little-endian u32. Scalars in the body are explicitly serialized; native struct padding, pointer addresses and enum sizes are not stored.

The body contains the selected profile, parser and upload progress, resources and descriptors, shadow/active sprites and drawing lists, 3D camera/projection/mesh state, timing phase, IRQ/collision latches, resource FIFO records and indices, overflow counter/latch and marker phase, logo, pointer shadow/active state and custom caches, current framebuffer and all demo animation state. Demo codecs use little-endian scalars and translate the D3 model pointer into a model index. Unused demo-state reserve bytes are zero-initialized.

Loading checks the version, exact size, checksum, profile and device invariants before replacing the live device. Failure returns zero and leaves the existing device unchanged. Other format versions are rejected; they must not be reinterpreted as format 15. Reboot the emulated card when migrating an older save.

Save and load with host execution paused. Treat snapshots as local emulator state; CRC detects accidental corruption, not authenticity. The format is not a public untrusted-data interchange protocol. The current layout is defined by `snap_body` and the demo codecs, rather than offsets for host applications to hard-code.

Resource servicing is synchronous within a serialized host callback. Snapshots can contain accepted pending jobs and partial command arguments; they cannot interrupt a running decoder. Queue depth, job formats, addresses, status consistency and terminal-state invariants are validated before loading.
