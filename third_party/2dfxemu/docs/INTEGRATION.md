# Host integration

Create one `twodfx_emu` with `TWODFX_EMU_ENTERPRISE` or `TWODFX_EMU_TVC`. Destroy it with `twodfx_emu_destroy`. Keep the device alive across ordinary guest CPU resets; call `twodfx_emu_power_cycle` only for a physical card reset or power cycle.

## Ports and interrupts

Decode the low byte of Z80 I/O addresses. Route F8h writes to `twodfx_emu_write_f8`, F9h writes to `twodfx_emu_write_f9`, and F8h reads to `twodfx_emu_read_f8`. F9h is write-only. Combine reads with other emulated devices using the host machine's bus rules; the connector must not suppress another device's response.

Pass absolute physical scanline numbers to `twodfx_emu_hsync` on every scanline, including when no raster interrupt is enabled and during resource loading. HSYNC drains the 16-job resource FIFO in order, without publishing a new scene; jobs are not limited to one per frame. Call `twodfx_emu_vsync` once per physical 50 Hz frame, including when rendering is set to 25 fps. Combine `twodfx_emu_irq_asserted` with other interrupt sources. IRQ remains asserted until the guest clears its pending raster latches.

Cold boot starts asleep and OFF. The guest wakes the card with F8h=50h, F9h=01h (or another nonzero engine mode). F8h bit6 becomes zero when ready. Software should keep this handshake when running on 2dfx hardware; the behavioural model completes it immediately.

## Video

Read indices and coverage with `twodfx_emu_overlay_raw_line`, using physical raw coordinates, or `twodfx_emu_overlay_line`, using stored-framebuffer coordinates. Both functions clear their output buffers before filling them. An index is meaningful wherever coverage is nonzero.

| Profile | Stored framebuffer | Raw origin | Practical visible pointer bounds, inclusive |
|---|---|---|---|
| Enterprise | 744 × 288 | (88, 16) | X=88..811, Y=18..303 |
| TVC | 704 × 256 | (0, 41) | X=79..686, Y=41..296 |

NORMAL overlays pixels whose index differs from the transparent key. FORCED overlays every stored pixel, including the key. OFF supplies no coverage. Retained layers draw in this order: background, sprites 0..63, foreground, built-in logo, pointer, optional overrun marker. Each drawing layer starts with a full clip and zero XY offset; overlays and sprites ignore 2DPT clipping.

Keep numeric indices unchanged. Enterprise colours depend on the host NICK palette and BIAS; TVC uses its external-colour interpretation. NICK-format upload conversion is already performed by this module: do not apply another bit shuffle at the overlay boundary. Raw packed resources store the left pixel in the low nibble.

## State and execution

Use the snapshot functions for a complete peripheral chunk; do not serialize the opaque struct. They include resources, unfinished commands, accepted and pending scenes, cursor caches, the pending resource FIFO and overflow latch, demo state and the current frame. The host must save its own video phase separately.

Multiple devices may run independent demos. Calls into the library, including calls for different devices, must be serialized on one thread or protected by a host lock. Demo callbacks temporarily bind their saved state to the portable API. The module itself creates no threads.

`twodfx_emu_set_render_enabled` is a host diagnostic option, independent of guest ENGINE_OFF. Leave it enabled in an ordinary integration. `twodfx_emu_set_logo_art` overrides only the normal-size logo; the small logo uses the supplied 103 × 40 asset.

The generic `TwoDfxAdapter` demonstrates port, sync, IRQ, overlay and snapshot wiring. It owns its device and cannot be copied. The older adapter header and class name remain compatibility aliases.
