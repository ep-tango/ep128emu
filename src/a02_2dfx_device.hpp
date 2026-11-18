// SPDX-License-Identifier: GPL-2.0-or-later
// A02 opt-in portable 2DFX host owner: bus, clock and bounded video output.
// Retains independently licensed core copyright in third_party/2dfxemu/LICENSE.
#ifndef EP128EMU_A02_2DFX_DEVICE_HPP
#define EP128EMU_A02_2DFX_DEVICE_HPP

#ifdef ENABLE_A02_2DFX
#include "twodfx_emu.h"
#include "a02_video_line.hpp"
#include "display.hpp"   // canonical VM display VSYNC line thresholds
#include <string.h>
#include <new>
#include <atomic>
#include <stdint.h>

namespace Ep128Emu {
  class A02TwoDFX {
   private:
    twodfx_emu *device_;
    uint16_t line_;
    uint8_t lastCommand_;
    std::atomic<bool> touched_;
    std::atomic<bool> engineActive_;
    std::atomic<uint8_t> statusByte_;
    bool enabled_;
    bool frameBoundarySeen_;
    uint8_t composedLine_[A02VideoLine::kMaxEncodedBytes];

    A02TwoDFX(const A02TwoDFX&);
    A02TwoDFX& operator=(const A02TwoDFX&);
   public:
    explicit A02TwoDFX(twodfx_machine machine)
      : device_(twodfx_emu_create(machine)),
        line_(0), lastCommand_(0xFF), touched_(false),
        engineActive_(false), statusByte_(0xFF),
        enabled_(true), frameBoundarySeen_(false)
    {
      if (!device_)
        throw std::bad_alloc();
    }

    ~A02TwoDFX()
    {
      twodfx_emu_destroy(device_);
    }

    // Runtime expansion switch. Resetting on every transition drops all
    // framebuffer/resource, pending IRQ, and parser state atomically.
    bool enabled() const { return enabled_; }
    void setEnabled(bool state)
    {
      if (enabled_ == state)
        return;
      enabled_ = state;
      reset();
    }

    void reset()
    {
      twodfx_emu_power_cycle(device_);
      line_ = 0;
      lastCommand_ = 0xFF;
      touched_ = false;
      engineActive_.store(false, std::memory_order_relaxed);
      statusByte_.store(0xFF, std::memory_order_relaxed);
      frameBoundarySeen_ = false;
    }

    void write(uint16_t port, uint8_t value)
    {
      if (!enabled_) return;
      if ((port & 0xFFu) == 0xF8u) {
        twodfx_emu_write_f8(device_, value);
        lastCommand_ = value;
      }
      else if ((port & 0xFFu) == 0xF9u) {
        twodfx_emu_write_f9(device_, value);
      }
      else {
        return;
      }
      touched_ = true;
      // The physical board's green LED reflects the running 2DFX engine,
      // not the host's expansion-card attachment or historical bus traffic.
      // Read the C core on its emulation owner thread and publish atomically.
      engineActive_.store(twodfx_emu_engine_mode(device_) !=
                          TWODFX_EMU_ENGINE_OFF, std::memory_order_relaxed);
      statusByte_.store(twodfx_emu_read_f8(device_),
                        std::memory_order_relaxed);
    }

    uint8_t read(uint16_t port) const
    {
      if (!enabled_) return 0xFF;
      return ((port & 0xFFu) == 0xF8u ?
              twodfx_emu_read_f8(device_) : uint8_t(0xFF));
    }

    uint8_t debugRead(uint16_t port) const
    {
      if (!enabled_) return 0xFF;
      return ((port & 0xFFu) == 0xF8u ? lastCommand_ : uint8_t(0xFF));
    }

    void hsync()
    {
      if (!enabled_) return;
      twodfx_emu_hsync(device_, line_);
      statusByte_.store(twodfx_emu_read_f8(device_),
                        std::memory_order_relaxed);
      if (line_ < 0xFFFFu)
        line_++;
    }

    // Direct core clock for standalone firmware/host unit tests.
    void vsync()
    {
      if (!enabled_) return;
      twodfx_emu_vsync(device_);
      // The portable core can change the engine state on the VSYNC edge
      // (e.g. final firmware demo); reflect that in the host status LED.
      engineActive_.store(twodfx_emu_engine_mode(device_) !=
                          TWODFX_EMU_ENGINE_OFF, std::memory_order_relaxed);
      statusByte_.store(twodfx_emu_read_f8(device_),
                        std::memory_order_relaxed);
      line_ = 0;
      frameBoundarySeen_ = true;
    }

    // Real NICK asserts VSYNC repeatedly on successive scanlines, whereas
    // the 2DFX core accepts one 50 Hz FRAME edge. Reuse the existing
    // FLTK/capture minimum-line gate; never count one NICK pulse as a frame.
    // TVC CRTC normally supplies one VSYNC per 312 scanlines.
    bool hardwareVsync()
    {
      if (!enabled_) return false;
      const unsigned minFrameLines =
          EP128EMU_VSYNC_MIN_LINES - EP128EMU_VSYNC_OFFSET;
      if (frameBoundarySeen_ && line_ < minFrameLines)
        return false;
      vsync();
      return true;
    }

    uint64_t videoFrames() const
    {
      return twodfx_emu_video_frame_count(device_);
    }

    // The same bounded wire packet is sent to FLTK/GL and video capture.
    // When no selected pixels exist, return the original bytes unchanged.
    const uint8_t *composeLine(const uint8_t *src, size_t *nBytes,
                               const uint8_t *nickPalette)
    {
      if (!enabled_ || !touched_ || !src || !nBytes || *nBytes == 0)
        return src;
      const bool tvc = (twodfx_emu_machine(device_) == TWODFX_EMU_TVC);
      // +3 NICK raster rows vs the original EP integration: the Digger
      // static 2DFX sprites now match the 2.0.12.8 reference exactly.
      // This is screenshot-reference parity, not yet physical-card PASS.
      // TVC has independent CRTC sync and retains its 19-row mapping.
      const unsigned lineOffset = tvc ? 19U : 21U;
      if (line_ < lineOffset ||
          line_ - lineOffset >= twodfx_emu_framebuffer_height(device_))
        return src;
      uint8_t colours[A02VideoLine::kPixels] = {0};
      uint8_t coverage[A02VideoLine::kPixels] = {0};
      const size_t left = (tvc ? 2U : 28U);
      if (!twodfx_emu_overlay_line(device_, 0U, line_ - lineOffset,
                                   colours + left, coverage + left,
                                   A02VideoLine::kPixels - left))
        return src;
      size_t nextBytes = 0;
      if (!A02VideoLine::compose(src, *nBytes, colours, coverage,
                                  nickPalette, tvc,
                                  composedLine_, sizeof(composedLine_),
                                  nextBytes))
        return src;  // Bounded fail-closed: no potentially corrupt output.
      *nBytes = nextBytes;
      return composedLine_;
    }

    bool irq() const { return enabled_ && twodfx_emu_irq_asserted(device_) != 0; }
    bool touched() const { return enabled_ && touched_.load(std::memory_order_relaxed); }
    // Host UI polls this without taking the emulation thread's heavy lock.
    // It is a guest F8h/F9h access indicator, NOT a simulated PCB GPIO.
    // Thread-safe live engine state: OFF after power-cycle or 50h/00h,
    // lit after 50h/01h or 50h/FFh. No speculative PCB GPIO animation.
    bool engineActive() const
    {
      return enabled_ && engineActive_.load(std::memory_order_relaxed);
    }
    uint8_t statusSnapshot() const
    {
      // Read-only GUI/debug sample: no guest port accesses or owner lock.
      return statusByte_.load(std::memory_order_relaxed);
    }
    bool guestBusTouched() const
    {
      return touched_.load(std::memory_order_relaxed);
    }
    uint16_t rawLine() const { return line_; }
    const twodfx_emu *native() const { return device_; }
  };
}
#endif  // ENABLE_A02_2DFX
#endif  // EP128EMU_A02_2DFX_DEVICE_HPP
