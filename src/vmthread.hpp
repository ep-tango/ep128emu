
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2016 Istvan Varga <istvanv@users.sourceforge.net>
// https://github.com/istvan-v/ep128emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#ifndef EP128EMU_VMTHREAD_HPP
#define EP128EMU_VMTHREAD_HPP

#include "ep128emu.hpp"
#include "system.hpp"
#include "vm.hpp"
#include "fileio.hpp"
#include <vector>
#include <deque>
#include <atomic>
#include <string>
#include <cstdio>

namespace Ep128Emu {

  class VMThread : private Thread {
   public:
    VirtualMachine& vm;
    struct VMThreadStatus : public VirtualMachine::VMStatus {
      // 'threadStatus' is zero if the emulation thread is running,
      // and non-zero if it has terminated (negative if the termination
      // occured due to an error).
      int       threadStatus;
      float     speedPercentage;
      bool      isPaused;
      size_t    rewindFramesAvailable;
      size_t    rewindTimelinePosition;
      size_t    rewindTimelineLength;
      size_t    rewindBufferUsageBytes;
      size_t    rewindAllocatedUsageBytes;
      size_t    rewindBufferLimitBytes;
      unsigned int rewindUnavailableReason;
      // --------
      VMThreadStatus(VMThread& vmThread_);
    };
   private:
    class Message;
    Mutex           mutex_;
    unsigned long   lockCnt;
    ThreadLock      threadLock1;
    ThreadLock      threadLock2;
    ThreadLock      threadUnlockAck;
    Timer           speedTimer;
    Message         *messageQueue;
    Message         *lastMessage;
    Message         *freeMessageStack;
    int             messageCnt;
    bool            exitFlag;
    bool            joinFlag;
    bool            errorFlag;
    bool            pauseFlag;
#ifdef WIN32
    bool            audioPauseApplied;
#endif
    unsigned int    frameAdvanceRequests;
    float           timesliceLength;
    float           avgTimesliceLength;
    int             hostThreadPriority;
    double          prvTime;
    double          nxtTime;
    VirtualMachine::VMStatus  vmStatus;
    void            *userData;
    void            (*errorCallback)(void *userData_, const char *msg);
    void            (*processCallback)(void *userData_);
    bool            keyboardState[128];
    std::string     queuedFileIODirectory;
    bool            queuedFileIOEnabled;
    bool            queuedFileIODefaultDevice;
    // An explicit GUI config/reset invalidates still queued archive FileIO
    // overrides. Only the latest generation may touch host-side file state.
    uint64_t        queuedFileIOGeneration;

    struct RewindDelta {
      std::vector<unsigned char> data;
      VideoDisplay::RewindState displayState;
      // Compressed reversible XOR vs the adjacent current video frame.
      // Current display state always remains uncompressed.
      std::vector<unsigned char> frameXor;
      std::vector<unsigned char> producerXor;
      bool videoEncoded;
      VirtualMachine::RewindRuntimeState runtimeState;
      RewindDelta() : videoEncoded(false) {}
    };
    std::deque<RewindDelta>      rewindHistory;
    std::vector<RewindDelta>     rewindForwardHistory;
    std::vector<unsigned char>   rewindCurrentState;
    // Scratch storage is VM-thread-owned, never shared with saved history.
    File                          rewindCaptureFile;
    VideoDisplay::RewindState    rewindCurrentDisplayState;
    VirtualMachine::RewindRuntimeState rewindCurrentRuntimeState;
    size_t                       rewindHistoryBytes;
    size_t                       rewindDeltaAllocatedBytes;
    std::atomic<size_t>          rewindHistoryLimitBytes;
    std::atomic<size_t>          rewindFramesAvailable;
    std::atomic<uint64_t>        rewindTimelineStatus;
    std::atomic<size_t>          rewindBufferUsageBytes;
    std::atomic<size_t>          rewindAllocatedUsageBytes;
    uint64_t                     rewindLastFrameSequence;
    uint64_t                     rewindLastHostIOEpoch;
    std::atomic<bool>            rewindDemoGuardActive;
    std::atomic<unsigned int>    rewindBlockedReason;
    unsigned int                 rewindFrameRequests;
    int                          rewindJumpRequest;
    bool                         rewindCaptureEnabled;
    bool                         rewindCaptureRequested;
    bool                         rewindCaptureUserEnabled;
    bool                         rewindCaptureSpeedAllowed;
    uint8_t                      mouseButtonState;
    bool                         rewindInputReconcilePending;

    static size_t rewindDeltaCapacity_(const RewindDelta& d);
    static size_t rewindDeltaVideoBytes_(const RewindDelta& d);
    static bool rewindEncodeVideoXor_(std::vector<unsigned char>& out,
             const std::vector<unsigned char>& target,
             const std::vector<unsigned char>& adjacent);
    static bool rewindDecodeVideoXor_(std::vector<unsigned char>& out,
             const std::vector<unsigned char>& code,
             const std::vector<unsigned char>& adjacent);
    static void rewindStoreDisplayDelta_(RewindDelta& d,
             const VideoDisplay::RewindState& target,
             const VideoDisplay::RewindState& adjacent);
    static bool rewindLoadDisplayDelta_(VideoDisplay::RewindState& out,
             const RewindDelta& d,
             const VideoDisplay::RewindState& adjacent);
    size_t rewindEstimatedAllocatedBytes_() const;
    void rewindUpdateTimelineStatus_();
    void rewindTrimToLimit_();
    void rewindClearHistory_();
    void rewindClearForwardHistory_();
    void rewindCaptureFrame_();
    bool rewindRestorePreviousFrame_(bool renderFrame = true);
    bool rewindRestoreNextFrame_(bool renderFrame = true);
    static void rewindEncodeDelta_(std::vector<unsigned char>& out,
                                   const std::vector<unsigned char>& oldState,
                                   const unsigned char *newState,
                                   size_t newSize);
    static bool rewindApplyDelta_(std::vector<unsigned char>& state,
                                  const std::vector<unsigned char>& delta);
    void rewindLoadState_(const std::vector<unsigned char>& state);

   public:
    VMThread(VirtualMachine& vm_, void *userData_ = (void *) 0);
    virtual ~VMThread();
    /*!
     * Block the execution of the emulation thread, so that the main thread
     * can safely access the virtual machine object. Returns zero on success,
     * a positive value if the emulation thread did not respond after 't'
     * milliseconds, and a negative value if the thread has terminated for
     * some reason.
     */
    int lock(size_t t);
    /*!
     * Allow the emulation thread to resume execution after a previous
     * successful call to lock().
     */
    void unlock();
    /*!
     * Run emulation (or just wait if paused) for a short period of time,
     * and update status information. This can be called by the main thread
     * after lock() in a loop for single-threaded emulation.
     * Returns false after quit() was called or a fatal error occured.
     */
    bool process();
    /*!
     * Pause emulation if 'n' is true, or continue if 'n' is false.
     * NOTE: the initial state is pause=true.
     */
    void pause(bool n);
    /*!
     * While paused, run until the next completed video frame and then
     * remain paused. Returns false if the request is not accepted.
     */
    bool advanceFrame();
    /*!
     * While paused, restore one previously completed video frame. The
     * request is accepted only when rewind history is available.
     */
    bool rewindFrame();
    /*!
     * While paused, move by up to 'n' frames inside already captured rewind
     * history. Returns the number of frames accepted for the jump. Forward
     * jumps never simulate new frames beyond the recorded timeline end.
     */
    size_t rewindFrames(size_t n);
    size_t advanceRecordedFrames(size_t n);
    /*!
     * Discard all captured rewind history.
     */
    void clearRewindHistory();
    // Requires the caller to hold the VM thread stopped via lock().
    // Used for synchronous demo-start and full configuration reset barriers.
    void clearRewindHistoryLocked(bool demoStarting = false);
    /*!
     * Set host-RAM budget for rewind history. The update is executed by the
     * emulation thread; lowering the limit trims only oldest past history and
     * never destroys recorded-future history in a frozen timeline.
     */
    void setRewindHistoryLimit(size_t limitBytes_);
    // T004 diagnostic/performance control: exact per-frame history capture.
    // Disabling immediately clears history at the next VM-thread boundary.
    void setRewindCaptureUserEnabled(bool enabled);
    /*!
     * Terminate emulation thread. If 'waitFlag_' is true, it will also
     * wait until the thread has actually stopped, and status information
     * is updated.
     */
    void quit(bool waitFlag_ = false);
    /*!
     * Set pointer to be passed to callback functions.
     */
    void setUserData(void *userData_);
    /*!
     * Set function to be called to print an error message on non-fatal
     * exceptions.
     */
    void setErrorCallback(void (*func)(void *userData_, const char *msg_));
    /*!
     * Reset emulated machine.
     */
    void reset(bool isColdReset_ = false);
    /*!
     * Queue File I/O state changes for execution by the emulation thread.
     * This avoids direct GUI-thread access to the virtual machine object.
     */
    void setFileIOState(const std::string& directory_, bool enabled_,
                        bool defaultDeviceIsFILE_);
    // Call only while VMThread::lock() is held: an explicit configuration
    // or reset supersedes all previously queued archive FILE: overrides.
    void invalidateQueuedFileIOStateLocked();
    /*!
     * Set amplitude scale for audio output.
     */
    // Runtime-only audio gate, FIFO-ordered with reset and keyboard events.
    // Used by Windows archive Full Speed startup to avoid PortAudio pacing.
    void setAudioOutputEnabled(bool enabled_);
    void setAudioOutputVolume(double ampScale_);
    /*!
     * Set state of key 'keyCode_' (0 to 127).
     */
    void setKeyboardState(uint8_t keyCode_, bool isPressed_);
    /*!
     * Send mouse event to the emulated machine. 'dX' and 'dY' are the
     * horizontal and vertical motion of the pointer relative to the position
     * at the time of the previous call, positive values move to the left and
     * up, respectively.
     * Each bit of 'buttonState' corresponds to the current state of a mouse
     * button (1 = pressed):
     *   b0 = left button
     *   b1 = right button
     *   b2 = middle button
     *   b3..b7 = buttons 4 to 8
     * 'mouseWheelEvents' can be the sum of any of the following:
     *   1: mouse wheel up
     *   2: mouse wheel down
     *   4: mouse wheel left
     *   8: mouse wheel right
     */
    void setMouseState(int8_t dX, int8_t dY,
                       uint8_t buttonState, uint8_t mouseWheelEvents);
    /*!
     * Set state of all keys and mouse buttons to released.
     */
    void resetKeyboard();
    /*!
     * Release all keyboard and mouse buttons immediately while the VM thread
     * is already locked by the caller. This avoids queueing an asynchronous
     * keyboard reset immediately before another VM operation.
     */
    void resetKeyboardLocked();
    /*!
     * Start tape playback.
     */
    void tapePlay();
    /*!
     * Start tape recording.
     */
    void tapeRecord();
    /*!
     * Stop tape playback or recording.
     */
    void tapeStop();
    /*!
     * Set tape position to the specified time (in seconds).
     */
    void tapeSeek(double seekTime_);
    /*!
     * Seek forward (if isForward = true) or backward (if isForward = false)
     * to the nearest cue point, or by 't' seconds if no cue point is found.
     */
    void tapeSeekToCuePoint(bool isForward = true, double t = 10.0);
    /*!
     * Stop playing or recording demo.
     */
    void stopDemo();
    /*!
     * Set function to be called by the emulation thread at an interval of
     * a few milliseconds. This function may throw an std::exception, in which
     * case the error callback will be called to display the error message.
     */
    void setProcessCallback(void (*func)(void *userData_));
    /*!
     * Set maximum emulation speed as a percentage (100 = normal speed).
     * A zero or negative value means no limit.
     */
    void setSpeedPercentage(int speedPercentage_, bool audioClockDriven = false);
    void setHostThreadPriority(int priority_);
  // --------------------------------------------------------------------------
   private:
    virtual void run();
    void cleanup();
    void resetKeyboard_();
    class Message {
     protected:
      VMThread& vmThread;
     public:
      Message   *nextMessage;
      Message(VMThread& vmThread_)
        : vmThread(vmThread_),
          nextMessage((Message *) 0)
      {
      }
      virtual ~Message();
      virtual void process() = 0;
    };
    class Message_Reset : public Message {
     private:
      bool    isColdReset;
     public:
      Message_Reset(VMThread& vmThread_, bool isColdReset_ = false)
        : Message(vmThread_),
          isColdReset(isColdReset_)
      {
      }
      virtual ~Message_Reset();
      virtual void process();
    };
    class Message_FileIOState : public Message {
     private:
      uint64_t generation;
     public:
      Message_FileIOState(VMThread& vmThread_, uint64_t generation_)
        : Message(vmThread_), generation(generation_)
      {
      }
      virtual ~Message_FileIOState();
      virtual void process();
    };
    class Message_ClearRewind : public Message {
     public:
      Message_ClearRewind(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_ClearRewind();
      virtual void process();
    };
    class Message_SetRewindLimit : public Message {
     private:
      size_t limitBytes;
     public:
      Message_SetRewindLimit(VMThread& vmThread_, size_t limitBytes_)
        : Message(vmThread_), limitBytes(limitBytes_)
      {
      }
      virtual ~Message_SetRewindLimit();
      virtual void process();
    };
    class Message_SetAudioEnabled : public Message {
     private:
      bool enabled;
     public:
      Message_SetAudioEnabled(VMThread& vmThread_, bool enabled_)
        : Message(vmThread_), enabled(enabled_) {}
      virtual ~Message_SetAudioEnabled();
      virtual void process();
    };
    class Message_SetVolume : public Message {
     private:
      double  ampScale;
     public:
      Message_SetVolume(VMThread& vmThread_, double ampScale_)
        : Message(vmThread_),
          ampScale(ampScale_)
      {
      }
      virtual ~Message_SetVolume();
      virtual void process();
    };
    class Message_KeyboardEvent : public Message {
     private:
      uint8_t keyCode;
      bool    isPressed;
     public:
      Message_KeyboardEvent(VMThread& vmThread_,
                            uint8_t keyCode_, bool isPressed_)
        : Message(vmThread_),
          keyCode(keyCode_),
          isPressed(isPressed_)
      {
      }
      virtual ~Message_KeyboardEvent();
      virtual void process();
    };
    class Message_MouseEvent : public Message {
     private:
      uint32_t  mouseData;
     public:
      Message_MouseEvent(VMThread& vmThread_, uint32_t mouseData_)
        : Message(vmThread_),
          mouseData(mouseData_)
      {
      }
      virtual ~Message_MouseEvent();
      virtual void process();
      static inline uint32_t packMouseEvent(int8_t dX, int8_t dY,
                                            uint8_t buttonState,
                                            uint8_t mouseWheelEvents)
      {
        uint32_t  mouseData_ = 0U;
        ((unsigned char *) &mouseData_)[0] = uint8_t(dX);
        ((unsigned char *) &mouseData_)[1] = uint8_t(dY);
        ((unsigned char *) &mouseData_)[2] = buttonState;
        ((unsigned char *) &mouseData_)[3] = mouseWheelEvents;
        return mouseData_;
      }
      inline void unpackMouseEvent(int8_t& dX, int8_t& dY, uint8_t& buttonState,
                                   uint8_t& mouseWheelEvents) const
      {
        dX = int8_t(uint8_t(((unsigned char *) &mouseData)[0]));
        dY = int8_t(uint8_t(((unsigned char *) &mouseData)[1]));
        buttonState = ((unsigned char *) &mouseData)[2];
        mouseWheelEvents = ((unsigned char *) &mouseData)[3];
      }
    };
    class Message_ResetKeyboard : public Message {
     public:
      Message_ResetKeyboard(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_ResetKeyboard();
      virtual void process();
    };
    class Message_TapePlay : public Message {
     public:
      Message_TapePlay(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_TapePlay();
      virtual void process();
    };
    class Message_TapeRecord : public Message {
     public:
      Message_TapeRecord(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_TapeRecord();
      virtual void process();
    };
    class Message_TapeStop : public Message {
     public:
      Message_TapeStop(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_TapeStop();
      virtual void process();
    };
    class Message_TapeSeek : public Message {
     private:
      double  seekTime;
      int8_t  seekDirection;
     public:
      Message_TapeSeek(VMThread& vmThread_,
                       int seekDirection_, double seekTime_)
        : Message(vmThread_),
          seekTime(seekTime_),
          seekDirection(int8_t(seekDirection_))
      {
      }
      virtual ~Message_TapeSeek();
      virtual void process();
    };
    class Message_StopDemo : public Message {
     public:
      Message_StopDemo(VMThread& vmThread_)
        : Message(vmThread_)
      {
      }
      virtual ~Message_StopDemo();
      virtual void process();
    };
    class Message_Dummy : public Message {
     private:
      // should have enough space for all the other message types
      double  dummy1;
      double  dummy2;
     public:
      Message_Dummy(VMThread& vmThread_)
        : Message(vmThread_),
          dummy1(0.0),
          dummy2(0.0)
      {
      }
      virtual ~Message_Dummy();
      virtual void process();
    };
    // ----------------
    Message *allocateMessage_();
    void queueMessage(Message *m);
    template <typename T>
    T *allocateMessage()
    {
      Message *m = allocateMessage_();
      if (m)
        return new((void *) m) T(*this);
      return ((T *) 0);
    }
    template <typename T, typename T1>
    T *allocateMessage(T1 arg1)
    {
      Message *m = allocateMessage_();
      if (m)
        return new((void *) m) T(*this, arg1);
      return ((T *) 0);
    }
    template <typename T, typename T1, typename T2>
    T *allocateMessage(T1 arg1, T2 arg2)
    {
      Message *m = allocateMessage_();
      if (m)
        return new((void *) m) T(*this, arg1, arg2);
      return ((T *) 0);
    }
    template <typename T, typename T1, typename T2, typename T3>
    T *allocateMessage(T1 arg1, T2 arg2, T3 arg3)
    {
      Message *m = allocateMessage_();
      if (m)
        return new((void *) m) T(*this, arg1, arg2, arg3);
      return ((T *) 0);
    }
  };

}       // namespace Ep128Emu

#endif  // EP128EMU_VMTHREAD_HPP

