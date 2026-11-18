# Supported behaviour

This package implements the final 2dfx 1.1.4 command and rendering behaviour for Enterprise and TVC. The NICK conversion, reset RET slots, triangle strokes, pattern flags, small logo, pointer, mesh counts and angle packing are included. Projection defaults are FX=579 on Enterprise and FX=508 on TVC, FY=300, near=96 and far=2490.

Rendering uses integer rules checked against the final 2dfx reference. The included test suite contains 1,920 reference-frame vectors spanning both profiles, all drawing families, offsets, clipping and mesh modes. It also checks colour conversion, strict transactions, frame cadence, cursor caching, state restoration and the current demos. This is source-based behavioural validation; it does not certify every host emulator's integration or every possible command stream.

## Timing boundary

This is a behavioural device, not a cycle-timing model of 2dfx hardware. Port writes execute synchronously. READY completes immediately. Resource jobs enter a 16-job FIFO. Each HSYNC callback drains the FIFO in order, independently of rendering, engine mode and frame cadence. VSYNC also drains it as a fallback and skips scene publication at that opportunity. Job duration is not simulated; an ordinary host integration must continue calling HSYNC throughout resource loading. SYS_RESET completes synchronously while preserving engine mode. Drawing duration, electrical bus effects and finite input-byte queue overload are not simulated. Resource FIFO overflow is simulated: the new request is rejected and sticky active-low STATUS5 is asserted. The optional overrun marker displays this condition. Consequently, benchmark/overrun diagnostics cannot measure 2dfx hardware speed in this model.

Demo animation advances at accepted 25/50 fps scene opportunities. Random demonstrations use deterministic per-device entropy, so their particular random trajectories need not match a physical power-on run. Their assets, scene logic and command behaviour are preserved. Logical demo time is based on emulated frames, not the computer's wall clock.

Host developers should verify palette interpretation, overlay coordinates, IRQ combination, reset semantics and snapshot chunk wiring in their own integrations. No external emulator integration is included or claimed as tested by this package.
