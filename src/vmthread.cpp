
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

#include "ep128emu.hpp"
#include "system.hpp"
#include "vm.hpp"
#include "vmthread.hpp"
#include "fileio.hpp"
#include <cstring>

#ifdef __APPLE__
#  include <pthread/qos.h>
#endif

static void defaultErrorCallback(void *userData_, const char *msg)
{
  (void) userData_;
  std::fprintf(stderr, "ERROR: %s\n", msg);
}

static void dummyErrorCallback(void *userData_, const char *msg)
{
  (void) userData_;
  (void) msg;
}

namespace Ep128Emu {

  VMThread::VMThread(VirtualMachine& vm_, void *userData_)
    : vm(vm_),
      lockCnt(0UL),
      threadLock1(true),
      threadLock2(true),
      threadUnlockAck(false),
      messageQueue((Message *) 0),
      lastMessage((Message *) 0),
      freeMessageStack((Message *) 0),
      messageCnt(0),
      exitFlag(false),
      joinFlag(false),
      errorFlag(false),
      pauseFlag(true),
#ifdef WIN32
      audioPauseApplied(false),
#endif
      frameAdvanceRequests(0U),
      timesliceLength(0.0f),
      avgTimesliceLength(0.002f),
      hostThreadPriority(0),
      prvTime(0.0),
      nxtTime(0.0),
      userData(userData_),
      errorCallback(&defaultErrorCallback),
      processCallback((void (*)(void *)) 0),
      queuedFileIODirectory("."),
      queuedFileIOEnabled(false),
      queuedFileIODefaultDevice(false),
      queuedFileIOGeneration(0ULL),
      rewindHistoryBytes(0),
      rewindDeltaAllocatedBytes(0),
      rewindHistoryLimitBytes(size_t(16) * 1024 * 1024),
      rewindFramesAvailable(0),
      rewindTimelineStatus((uint64_t(1) << 32) | uint64_t(1)),
      rewindBufferUsageBytes(0),
      rewindAllocatedUsageBytes(0),
      rewindLastFrameSequence(0ULL),
      rewindLastHostIOEpoch(0ULL),
      rewindDemoGuardActive(false),
      rewindBlockedReason(0U),
      rewindFrameRequests(0U),
      rewindJumpRequest(0),
      rewindCaptureEnabled(true),
      rewindCaptureRequested(true),
      rewindCaptureUserEnabled(true),
      rewindCaptureSpeedAllowed(true),
      mouseButtonState(0x00),
      rewindInputReconcilePending(false)
  {
    vmStatus.isRecordingDemo = false;
    vmStatus.isPlayingDemo = false;
    vmStatus.tapeReadOnly = true;
    vmStatus.tapePosition = -1.0;
    vmStatus.tapeLength = -1.0;
    vmStatus.tapeSampleRate = 0L;
    vmStatus.tapeSampleSize = 0;
    vmStatus.floppyDriveLEDState = 0U;
    vmStatus.tapeRemoteState = 0U;
    for (int i = 0; i < 128; i++)
      keyboardState[i] = false;
    rewindLastFrameSequence =
        vm.getVideoDisplay().getFrameSequenceNumber();
    rewindLastHostIOEpoch = vm.getRewindHostIOEpoch();

    this->start();
  }

  VMThread::~VMThread()
  {
    this->quit(true);
  }

  void VMThread::cleanup()
  {
    mutex_.lock();
    exitFlag = true;
    pauseFlag = true;
    try {
      vm.stopDemo();
    }
    catch (...) {
      errorFlag = true;
    }
    vmStatus.isRecordingDemo = false;
    vmStatus.isPlayingDemo = false;
    try {
      vm.setTapeFileName(std::string(""));
    }
    catch (...) {
      errorFlag = true;
    }
    vmStatus.tapeReadOnly = true;
    vmStatus.tapePosition = -1.0;
    vmStatus.tapeLength = -1.0;
    vmStatus.tapeSampleRate = 0L;
    vmStatus.tapeSampleSize = 0;
    vmStatus.floppyDriveLEDState = 0U;
    vmStatus.tapeRemoteState = 0U;
    while (messageQueue) {
      Message *m = messageQueue;
      messageQueue = m->nextMessage;
      m->~Message();
      m->nextMessage = freeMessageStack;
      freeMessageStack = m;
    }
    lastMessage = (Message *) 0;
    messageCnt = 0;
    while (freeMessageStack) {
      Message *m = freeMessageStack;
      freeMessageStack = m->nextMessage;
      std::free(m);
    }
    mutex_.unlock();
  }

  static void rewindPutUInt32(std::vector<unsigned char>& b, uint32_t n)
  {
    b.push_back((unsigned char) (n & 0xFFU));
    b.push_back((unsigned char) ((n >> 8) & 0xFFU));
    b.push_back((unsigned char) ((n >> 16) & 0xFFU));
    b.push_back((unsigned char) ((n >> 24) & 0xFFU));
  }

  static uint32_t rewindGetUInt32(const unsigned char *p)
  {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
           | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }

  static void rewindPutBEUInt32(std::vector<unsigned char>& b, uint32_t n)
  {
    b.push_back((unsigned char) ((n >> 24) & 0xFFU));
    b.push_back((unsigned char) ((n >> 16) & 0xFFU));
    b.push_back((unsigned char) ((n >> 8) & 0xFFU));
    b.push_back((unsigned char) (n & 0xFFU));
  }

  static size_t rewindDisplayPayloadBytes_(
      const VideoDisplay::RewindState& s)
  {
    return s.frameData.size() + s.producerFrameData.size();
  }

  static size_t rewindDisplayAllocatedBytes_(
      const VideoDisplay::RewindState& s)
  {
    return s.frameData.capacity() + s.producerFrameData.capacity();
  }

  // Reversible video data uses a bounded, lossless XOR representation.
  static unsigned char rewindXorVideoByte_(
      const std::vector<unsigned char>& a,
      const std::vector<unsigned char>& b, size_t i)
  {
    return (i < a.size() ? a[i] : 0U) ^ (i < b.size() ? b[i] : 0U);
  }

  // Lossless XOR spans, bounded to one host-video frame.
  bool VMThread::rewindEncodeVideoXor_(std::vector<unsigned char>& out,
      const std::vector<unsigned char>& target,
      const std::vector<unsigned char>& adjacent)
  {
    const size_t limit = 1024U * 1024U;
    if (target.size() > limit || adjacent.size() > limit)
      return false;
    const size_t n = std::max(target.size(), adjacent.size());
    out.clear();
    out.push_back(1U);
    rewindPutUInt32(out, uint32_t(target.size()));
    rewindPutUInt32(out, uint32_t(adjacent.size()));
    size_t i = 0U;
    while (i < n) {
      while (i < n && rewindXorVideoByte_(target, adjacent, i) == 0U)
        ++i;
      if (i == n)
        break;
      const size_t start = i;
      size_t last = i;
      while (++i < n) {
        if (rewindXorVideoByte_(target, adjacent, i) != 0U)
          last = i;
        else if (i - last > 8U)
          break;
      }
      const size_t len = last - start + 1U;
      rewindPutUInt32(out, uint32_t(start));
      rewindPutUInt32(out, uint32_t(len));
      for (size_t k = 0U; k < len; ++k)
        out.push_back(rewindXorVideoByte_(target, adjacent, start + k));
    }
    if (out.size() >= n + 9U) {
      out.resize(n + 9U);
      out[0] = 0U;
      for (size_t k = 0U; k < n; ++k)
        out[k + 9U] = rewindXorVideoByte_(target, adjacent, k);
    }
    return true;
  }

  bool VMThread::rewindDecodeVideoXor_(std::vector<unsigned char>& out,
      const std::vector<unsigned char>& code,
      const std::vector<unsigned char>& adjacent)
  {
    if (code.size() < 9U || code[0] > 1U)
      return false;
    const size_t targetSize = size_t(rewindGetUInt32(&code[1]));
    const size_t baseSize = size_t(rewindGetUInt32(&code[5]));
    if (targetSize > 1024U * 1024U || baseSize > 1024U * 1024U
        || baseSize != adjacent.size())
      return false;
    const size_t n = std::max(targetSize, baseSize);
    std::vector<unsigned char> decoded(n, 0U);
    std::copy(adjacent.begin(), adjacent.end(), decoded.begin());
    if (code[0] == 0U) {
      if (code.size() != n + 9U)
        return false;
      for (size_t i = 0U; i < n; ++i)
        decoded[i] ^= code[9U + i];
    }
    else {
      size_t p = 9U, previousEnd = 0U;
      while (p < code.size()) {
        if (code.size() - p < 8U)
          return false;
        const size_t start = size_t(rewindGetUInt32(&code[p]));
        const size_t len = size_t(rewindGetUInt32(&code[p + 4U]));
        p += 8U;
        if (len == 0U || start < previousEnd || start > n
            || len > n - start || len > code.size() - p)
          return false;
        for (size_t j = 0U; j < len; ++j)
          decoded[start + j] ^= code[p + j];
        p += len;
        previousEnd = start + len;
      }
    }
    for (size_t i = targetSize; i < n; ++i)
      if (decoded[i] != 0U)
        return false;
    out.assign(decoded.begin(), decoded.begin() + targetSize);
    return true;
  }

  void VMThread::rewindStoreDisplayDelta_(RewindDelta& d,
      const VideoDisplay::RewindState& target,
      const VideoDisplay::RewindState& adjacent)
  {
    d.displayState = target;
    d.videoEncoded = false;
    if (target.frameData.empty() || adjacent.frameData.empty())
      return;
    if (!rewindEncodeVideoXor_(d.frameXor, target.frameData,
                               adjacent.frameData)
        || !rewindEncodeVideoXor_(d.producerXor, target.producerFrameData,
                                  adjacent.producerFrameData)) {
      d.frameXor.clear();
      d.producerXor.clear();
      return;
    }
    d.videoEncoded = true;
    std::vector<unsigned char>().swap(d.displayState.frameData);
    std::vector<unsigned char>().swap(d.displayState.producerFrameData);
  }

  bool VMThread::rewindLoadDisplayDelta_(VideoDisplay::RewindState& out,
      const RewindDelta& d, const VideoDisplay::RewindState& adjacent)
  {
    out = d.displayState;
    if (!d.videoEncoded)
      return true;
    return rewindDecodeVideoXor_(out.frameData, d.frameXor,
                                 adjacent.frameData)
           && rewindDecodeVideoXor_(out.producerFrameData, d.producerXor,
                                    adjacent.producerFrameData);
  }

  size_t VMThread::rewindDeltaVideoBytes_(const RewindDelta& d)
  {
    return rewindDisplayPayloadBytes_(d.displayState)
           + d.frameXor.size() + d.producerXor.size();
  }

  size_t VMThread::rewindDeltaCapacity_(const RewindDelta& d)
  {
    return d.data.capacity() + d.runtimeState.machineData.capacity()
           + rewindDisplayAllocatedBytes_(d.displayState)
           + d.frameXor.capacity() + d.producerXor.capacity();
  }

  size_t VMThread::rewindEstimatedAllocatedBytes_() const
  {
    // Rewind-retained heap estimate, not whole-process RSS.
    return rewindDeltaAllocatedBytes + rewindCurrentState.capacity()
           + rewindCurrentRuntimeState.machineData.capacity()
           + rewindDisplayAllocatedBytes_(rewindCurrentDisplayState)
           + rewindCaptureFile.getBufferAllocatedCapacity()
           + rewindHistory.size() * sizeof(RewindDelta)
           + rewindForwardHistory.capacity() * sizeof(RewindDelta);
  }

  void VMThread::rewindUpdateTimelineStatus_()
  {
    // Publish position and length as one atomic value so the GUI can never
    // observe a mixed pair while the VM thread moves deltas between past and
    // recorded-future history. Supported rewind budgets (16/32/64 MiB)
    // keep both values far below the 32-bit packing limit.
    const uint64_t past =
        uint64_t(rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
    const uint64_t future = uint64_t(rewindForwardHistory.size());
    const uint64_t position = past + 1;
    const uint64_t length = position + future;
    rewindTimelineStatus =
        ((length & 0xFFFFFFFFULL) << 32) | (position & 0xFFFFFFFFULL);
    // Payload size only; allocated vector capacity, container overhead
    // and transient copies are not represented. This is NOT process RSS.
    rewindBufferUsageBytes =
        rewindHistoryBytes + rewindCurrentState.size()
        + rewindCurrentRuntimeState.machineData.size()
        + rewindDisplayPayloadBytes_(rewindCurrentDisplayState);
    rewindAllocatedUsageBytes = rewindEstimatedAllocatedBytes_();
  }

  void VMThread::rewindTrimToLimit_()
  {
    // Never delete recorded-future entries here: while paused they are part
    // of the immutable frozen timeline. Trim oldest past entries only. If the
    // future + current state alone exceeds a newly lowered limit, usage can
    // temporarily remain above the cap until Resume/branch clears the future.
    while (rewindHistory.size() > 2
           && rewindEstimatedAllocatedBytes_() > rewindHistoryLimitBytes) {
      rewindHistoryBytes -= rewindHistory.front().data.size();
      rewindHistoryBytes -= rewindHistory.front().runtimeState.machineData.size();
      rewindHistoryBytes -= rewindDeltaVideoBytes_(rewindHistory.front());
      rewindDeltaAllocatedBytes -=
          rewindDeltaCapacity_(rewindHistory.front());
      rewindHistory.pop_front();
    }
    rewindFramesAvailable =
        (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
    rewindUpdateTimelineStatus_();
  }

  void VMThread::rewindClearForwardHistory_()
  {
    while (!rewindForwardHistory.empty()) {
      RewindDelta& d = rewindForwardHistory.back();
      rewindHistoryBytes -= d.data.size();
      rewindHistoryBytes -= d.runtimeState.machineData.size();
      rewindHistoryBytes -= rewindDeltaVideoBytes_(d);
      rewindDeltaAllocatedBytes -= rewindDeltaCapacity_(d);
      rewindForwardHistory.pop_back();
    }
    std::vector<RewindDelta>().swap(rewindForwardHistory);
    rewindUpdateTimelineStatus_();
  }

  void VMThread::rewindClearHistory_()
  {
    rewindHistory.clear();
    rewindForwardHistory.clear();
    rewindCurrentState.clear();
    rewindCurrentDisplayState = VideoDisplay::RewindState();
    rewindCurrentRuntimeState = VirtualMachine::RewindRuntimeState();
    rewindHistoryBytes = 0;
    rewindDeltaAllocatedBytes = 0;
    std::deque<RewindDelta>().swap(rewindHistory);
    std::vector<RewindDelta>().swap(rewindForwardHistory);
    std::vector<unsigned char>().swap(rewindCurrentState);
    rewindFramesAvailable = 0;
    rewindUpdateTimelineStatus_();
    rewindFrameRequests = 0U;
    rewindJumpRequest = 0;
    rewindInputReconcilePending = false;
    rewindLastFrameSequence =
        vm.getVideoDisplay().getFrameSequenceNumber();
    rewindLastHostIOEpoch = vm.getRewindHostIOEpoch();
  }

  void VMThread::rewindEncodeDelta_(
      std::vector<unsigned char>& out,
      const std::vector<unsigned char>& oldState,
      const unsigned char *newState, size_t newSize)
  {
    out.clear();
    if (oldState.size() != newSize)
      return;

    size_t i = 0;
    while (i < newSize) {
      while (i < newSize && oldState[i] == newState[i])
        i++;
      if (i >= newSize)
        break;
      size_t start = i;
      while (i < newSize && oldState[i] != newState[i])
        i++;
      size_t len = i - start;
      if (start > 0xFFFFFFFFUL || len > 0xFFFFFFFFUL) {
        out.clear();
        return;
      }
      rewindPutUInt32(out, uint32_t(start));
      rewindPutUInt32(out, uint32_t(len));
      out.insert(out.end(), oldState.begin() + start,
                 oldState.begin() + (start + len));
    }
  }

  bool VMThread::rewindApplyDelta_(
      std::vector<unsigned char>& state,
      const std::vector<unsigned char>& delta)
  {
    size_t p = 0;
    while (p < delta.size()) {
      if ((delta.size() - p) < 8)
        return false;
      uint32_t offset = rewindGetUInt32(&(delta[p]));
      uint32_t len = rewindGetUInt32(&(delta[p + 4]));
      p += 8;
      if (size_t(offset) > state.size()
          || size_t(len) > (state.size() - size_t(offset))
          || size_t(len) > (delta.size() - p)) {
        return false;
      }
      if (len > 0)
        std::memcpy(&(state[size_t(offset)]), &(delta[p]), size_t(len));
      p += size_t(len);
    }
    return true;
  }

  void VMThread::rewindLoadState_(
      const std::vector<unsigned char>& state)
  {
    if (state.empty())
      throw Exception("empty rewind state");

    std::vector<unsigned char> loadData(state);
    size_t eofPos = loadData.size();
    rewindPutBEUInt32(loadData,
                      uint32_t(File::EP128EMU_CHUNKTYPE_END_OF_FILE));
    rewindPutBEUInt32(loadData, 0U);
    uint32_t crc = File::hash_32(&(loadData[eofPos]), 8);
    rewindPutBEUInt32(loadData, crc);

    File f(&(loadData.front()), loadData.size());
    vm.registerChunkTypes(f);
    vm.setRewindRestoreMode(true);
    try {
      f.processAllChunks();
    }
    catch (...) {
      vm.setRewindRestoreMode(false);
      throw;
    }
    vm.setRewindRestoreMode(false);
  }

  void VMThread::rewindCaptureFrame_()
  {
    if (!rewindCaptureEnabled || !vm.getIsRewindRestoreSupported()
        || rewindDemoGuardActive)
      return;

    if (vm.getIsRecordingDemo() || vm.getIsPlayingDemo()) {
      rewindClearHistory_();
      return;
    }

    rewindCaptureFile.resetForReuse();
    vm.saveState(rewindCaptureFile);
    const size_t stateSize = rewindCaptureFile.getBufferDataSize();
    const unsigned char *stateData = rewindCaptureFile.getBufferData();
    VideoDisplay::RewindState displayState;
    if (!vm.getVideoDisplay().getRewindState(displayState) ||
        !displayState.valid ||
        (vm.getIsRewindDirectFrameRequired() && displayState.frameData.empty())) {
      rewindClearHistory_();
      return;
    }
    VirtualMachine::RewindRuntimeState runtimeState;
    if (!vm.getRewindRuntimeState(runtimeState) || !runtimeState.valid) {
      // Never publish a guest snapshot with a missing FDC checkpoint.
      rewindClearHistory_();
      return;
    }

    if (rewindCurrentState.empty()) {
      rewindCurrentState.assign(stateData, stateData + stateSize);
      rewindCurrentDisplayState = displayState;
      rewindCurrentRuntimeState = runtimeState;
      rewindFramesAvailable = 0;
      rewindUpdateTimelineStatus_();
      return;
    }

    if (rewindCurrentState.size() != stateSize) {
      rewindClearHistory_();
      rewindCurrentState.assign(stateData, stateData + stateSize);
      rewindCurrentDisplayState = displayState;
      rewindCurrentRuntimeState = runtimeState;
      rewindUpdateTimelineStatus_();
      return;
    }

    RewindDelta d;
    rewindStoreDisplayDelta_(d, rewindCurrentDisplayState, displayState);
    d.runtimeState = rewindCurrentRuntimeState;
    rewindEncodeDelta_(d.data, rewindCurrentState, stateData, stateSize);
    rewindHistoryBytes += d.data.size() + d.runtimeState.machineData.size()
                          + rewindDeltaVideoBytes_(d);
    rewindHistory.push_back(RewindDelta());
    rewindHistory.back().data.swap(d.data);
    rewindHistory.back().displayState = d.displayState;
    rewindHistory.back().frameXor.swap(d.frameXor);
    rewindHistory.back().producerXor.swap(d.producerXor);
    rewindHistory.back().videoEncoded = d.videoEncoded;
    rewindHistory.back().runtimeState = d.runtimeState;
    rewindDeltaAllocatedBytes += rewindDeltaCapacity_(rewindHistory.back());
    rewindCurrentState.assign(stateData, stateData + stateSize);
    rewindCurrentDisplayState = displayState;
    rewindCurrentRuntimeState = runtimeState;

    rewindTrimToLimit_();
  }

  bool VMThread::rewindRestorePreviousFrame_(bool renderFrame)
  {
    if (rewindHistory.size() < 3 || rewindCurrentState.empty())
      return false;
    // Always revalidate the actual IMG path and write epoch immediately
    // before touching historical state. External replacement while paused
    // must never make an old frame restorable.
    const uint64_t liveEpoch = vm.getRewindHostIOEpoch();
    const bool mediaSafe = vm.getIsRewindRestoreSupported();
    if (liveEpoch != rewindLastHostIOEpoch || !mediaSafe ||
        vm.getIsRecordingDemo() || vm.getIsPlayingDemo()) {
      rewindBlockedReason = (mediaSafe ? 0U
                                  : vm.getRewindUnavailableReason());
      rewindClearHistory_();
      return false;
    }

    const size_t storedCapacity =
        rewindDeltaCapacity_(rewindHistory.back());
    RewindDelta d;
    d.data.swap(rewindHistory.back().data);
    d.displayState = rewindHistory.back().displayState;
    d.frameXor.swap(rewindHistory.back().frameXor);
    d.producerXor.swap(rewindHistory.back().producerXor);
    d.videoEncoded = rewindHistory.back().videoEncoded;
    d.runtimeState = rewindHistory.back().runtimeState;
    rewindHistory.pop_back();
    rewindHistoryBytes -= d.data.size();
    rewindHistoryBytes -= d.runtimeState.machineData.size();
    rewindHistoryBytes -= rewindDeltaVideoBytes_(d);
    rewindDeltaAllocatedBytes -= storedCapacity;

    // Preserve the exact state being left behind. The forward delta transforms
    // the restored past state back into the current future state, so paused
    // Next can traverse recorded history without re-simulating guest code or
    // replaying a different input stream.
    std::vector<unsigned char> futureState(rewindCurrentState);
    VideoDisplay::RewindState futureDisplayState(rewindCurrentDisplayState);
    VirtualMachine::RewindRuntimeState futureRuntimeState(
        rewindCurrentRuntimeState);

    if (!rewindApplyDelta_(rewindCurrentState, d.data)) {
      rewindClearHistory_();
      throw Exception("invalid rewind delta");
    }

    VideoDisplay::RewindState targetDisplayState;
    if (!rewindLoadDisplayDelta_(targetDisplayState, d,
                                 rewindCurrentDisplayState)) {
      rewindClearHistory_();
      throw Exception("invalid rewind video delta");
    }
    RewindDelta forwardStep;
    rewindStoreDisplayDelta_(forwardStep, futureDisplayState,
                             targetDisplayState);
    forwardStep.runtimeState = futureRuntimeState;
    rewindEncodeDelta_(forwardStep.data, futureState,
                       &(rewindCurrentState.front()),
                       rewindCurrentState.size());
    rewindHistoryBytes += forwardStep.data.size();
    rewindHistoryBytes += forwardStep.runtimeState.machineData.size();
    rewindHistoryBytes += rewindDeltaVideoBytes_(forwardStep);
    rewindForwardHistory.push_back(RewindDelta());
    rewindForwardHistory.back().data.swap(forwardStep.data);
    rewindForwardHistory.back().displayState = forwardStep.displayState;
    rewindForwardHistory.back().frameXor.swap(forwardStep.frameXor);
    rewindForwardHistory.back().producerXor.swap(forwardStep.producerXor);
    rewindForwardHistory.back().videoEncoded = forwardStep.videoEncoded;
    rewindForwardHistory.back().runtimeState = forwardStep.runtimeState;
    rewindDeltaAllocatedBytes += rewindDeltaCapacity_(rewindForwardHistory.back());

    std::vector<unsigned char> targetState(rewindCurrentState);

    VirtualMachine::RewindRuntimeState targetRuntimeState(d.runtimeState);
    if (!renderFrame) {
      rewindCurrentDisplayState = targetDisplayState;
      rewindCurrentRuntimeState = targetRuntimeState;
      rewindFramesAvailable =
          (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
      rewindUpdateTimelineStatus_();
      rewindInputReconcilePending = true;
      return true;
    }
    if (targetRuntimeState.machineData.size() > 14U &&
        targetRuntimeState.machineData[0] == 3U) {
      if (!targetDisplayState.valid || targetDisplayState.frameData.empty()
          || !vm.canRestoreRewindRuntimeState(targetRuntimeState)) {
        rewindClearHistory_();
        return false;
      }
      try {
        // No guest cycles, host FILE: calls or disk commands on Previous.
        rewindLoadState_(targetState);
        if (!vm.restoreRewindRuntimeState(targetRuntimeState) ||
            !vm.getVideoDisplay().restoreRewindState(targetDisplayState, true))
          throw Exception("mounted IMG rewind restore failed");
        rewindCurrentDisplayState = targetDisplayState;
        rewindCurrentRuntimeState = targetRuntimeState;
      }
      catch (...) {
        rewindClearHistory_();
        throw;
      }
      rewindLastFrameSequence = vm.getVideoDisplay().getFrameSequenceNumber();
      rewindFramesAvailable =
          (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
      rewindUpdateTimelineStatus_();
      rewindInputReconcilePending = true;
      return true;
    }
    std::vector<unsigned char> renderState(targetState);
    const RewindDelta& renderStepDelta = rewindHistory.back();
    const RewindDelta& renderAnchorDelta =
        rewindHistory[rewindHistory.size() - 2];
    // Reverse history now stores video relative to its newer neighbour.
    // Reconstruct both intermediate frames before rendering the older anchor.
    VideoDisplay::RewindState renderStepDisplayState;
    VideoDisplay::RewindState renderDisplayState;
    if (!rewindLoadDisplayDelta_(renderStepDisplayState, renderStepDelta,
                                 targetDisplayState)
        || !rewindLoadDisplayDelta_(renderDisplayState, renderAnchorDelta,
                                    renderStepDisplayState)) {
      rewindClearHistory_();
      throw Exception("invalid rewind render anchor video delta");
    }
    VirtualMachine::RewindRuntimeState renderRuntimeState(
        renderAnchorDelta.runtimeState);
    if (!rewindApplyDelta_(renderState, renderStepDelta.data) ||
        !rewindApplyDelta_(renderState, renderAnchorDelta.data)) {
      rewindClearHistory_();
      throw Exception("invalid rewind anchor delta");
    }

    const bool audioWasEnabled = vm.getEnableAudioOutput();
    if (!vm.canRestoreRewindRuntimeState(targetRuntimeState) ||
        !vm.canRestoreRewindRuntimeState(renderRuntimeState)) {
      rewindClearHistory_();
      return false;
    }
    vm.setEnableAudioOutput(false);
    try {
      rewindLoadState_(renderState);
      if (renderRuntimeState.valid)
        (void) vm.restoreRewindRuntimeState(renderRuntimeState);
      if (renderDisplayState.valid)
        (void) vm.getVideoDisplay().restoreRewindState(renderDisplayState,
                                                       true);
      vm.setRewindRenderMode(true);
      const uint64_t firstFrame =
          vm.getVideoDisplay().getFrameSequenceNumber();
      size_t elapsedMicroseconds = 0;
      while (vm.getVideoDisplay().getFrameSequenceNumber() == firstFrame
             && elapsedMicroseconds < 50000) {
        vm.run(2000);
        elapsedMicroseconds += 2000;
      }
      if (vm.getVideoDisplay().getFrameSequenceNumber() == firstFrame)
        throw Exception("rewind render first frame timeout");

      // restoreRewindState(..., true) suppresses output until the first real
      // frame boundary. Render one more complete frame from that boundary;
      // this is the frame that corresponds to targetState.
      const uint64_t secondFrame =
          vm.getVideoDisplay().getFrameSequenceNumber();
      elapsedMicroseconds = 0;
      while (vm.getVideoDisplay().getFrameSequenceNumber() == secondFrame
             && elapsedMicroseconds < 50000) {
        vm.run(2000);
        elapsedMicroseconds += 2000;
      }
      if (vm.getVideoDisplay().getFrameSequenceNumber() == secondFrame)
        throw Exception("rewind render complete frame timeout");

      vm.setRewindRenderMode(false);
      rewindLoadState_(targetState);
      if (targetRuntimeState.valid)
        (void) vm.restoreRewindRuntimeState(targetRuntimeState);
      if (targetDisplayState.valid)
        (void) vm.getVideoDisplay().restoreRewindState(targetDisplayState,
                                                       false);
      rewindCurrentDisplayState = targetDisplayState;
      rewindCurrentRuntimeState = targetRuntimeState;
    }
    catch (...) {
      vm.setRewindRenderMode(false);
      vm.setEnableAudioOutput(audioWasEnabled);
      rewindClearHistory_();
      throw;
    }
    vm.setRewindRenderMode(false);
    vm.setEnableAudioOutput(audioWasEnabled);

    rewindLastFrameSequence =
        vm.getVideoDisplay().getFrameSequenceNumber();
    rewindFramesAvailable =
        (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
    rewindUpdateTimelineStatus_();
    // The snapshot contains historical guest keyboard state. Keep that state
    // while paused/frame-stepping, but before normal execution resumes the
    // guest must be reconciled to the keys that are physically held now.
    rewindInputReconcilePending = true;
    return true;
  }

  bool VMThread::rewindRestoreNextFrame_(bool renderFrame)
  {
    if (rewindForwardHistory.empty() || rewindCurrentState.empty())
      return false;
    // A recorded future is just as sensitive to external host media changes
    // as a recorded past. No cached file identity may bypass this preflight.
    const uint64_t liveEpoch = vm.getRewindHostIOEpoch();
    const bool mediaSafe = vm.getIsRewindRestoreSupported();
    if (liveEpoch != rewindLastHostIOEpoch || !mediaSafe ||
        vm.getIsRecordingDemo() || vm.getIsPlayingDemo()) {
      rewindBlockedReason = (mediaSafe ? 0U
                                  : vm.getRewindUnavailableReason());
      rewindClearHistory_();
      return false;
    }

    const size_t storedCapacity =
        rewindDeltaCapacity_(rewindForwardHistory.back());
    RewindDelta fwd;
    fwd.data.swap(rewindForwardHistory.back().data);
    fwd.displayState = rewindForwardHistory.back().displayState;
    fwd.frameXor.swap(rewindForwardHistory.back().frameXor);
    fwd.producerXor.swap(rewindForwardHistory.back().producerXor);
    fwd.videoEncoded = rewindForwardHistory.back().videoEncoded;
    fwd.runtimeState = rewindForwardHistory.back().runtimeState;
    rewindForwardHistory.pop_back();
    rewindHistoryBytes -= fwd.data.size();
    rewindHistoryBytes -= fwd.runtimeState.machineData.size();
    rewindHistoryBytes -= rewindDeltaVideoBytes_(fwd);
    rewindDeltaAllocatedBytes -= storedCapacity;

    std::vector<unsigned char> targetState(rewindCurrentState);
    if (!rewindApplyDelta_(targetState, fwd.data)) {
      rewindClearHistory_();
      throw Exception("invalid forward rewind delta");
    }

    // Recreate the reverse transition before replacing currentState. This
    // makes Previous available again after an exact forward step.
    VideoDisplay::RewindState targetDisplayState;
    if (!rewindLoadDisplayDelta_(targetDisplayState, fwd,
                                 rewindCurrentDisplayState)) {
      rewindClearHistory_();
      throw Exception("invalid forward rewind video delta");
    }
    RewindDelta rev;
    rewindStoreDisplayDelta_(rev, rewindCurrentDisplayState,
                             targetDisplayState);
    rev.runtimeState = rewindCurrentRuntimeState;
    rewindEncodeDelta_(rev.data, rewindCurrentState,
                       &(targetState.front()), targetState.size());

    rewindHistoryBytes += rev.data.size();
    rewindHistoryBytes += rev.runtimeState.machineData.size();
    rewindHistoryBytes += rewindDeltaVideoBytes_(rev);
    rewindHistory.push_back(RewindDelta());
    rewindHistory.back().data.swap(rev.data);
    rewindHistory.back().displayState = rev.displayState;
    rewindHistory.back().frameXor.swap(rev.frameXor);
    rewindHistory.back().producerXor.swap(rev.producerXor);
    rewindHistory.back().videoEncoded = rev.videoEncoded;
    rewindHistory.back().runtimeState = rev.runtimeState;
    rewindDeltaAllocatedBytes += rewindDeltaCapacity_(rewindHistory.back());

    if (!renderFrame) {
      rewindCurrentState.swap(targetState);
      rewindCurrentDisplayState = targetDisplayState;
      rewindCurrentRuntimeState = fwd.runtimeState;
      rewindFramesAvailable =
          (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
      rewindUpdateTimelineStatus_();
      return true;
    }

    // Fail closed before restoring any machine state, including the
    // intermediate historical display anchor.
    if (!vm.canRestoreRewindRuntimeState(fwd.runtimeState)) {
      rewindClearHistory_();
      return false;
    }
    if (fwd.runtimeState.machineData.size() > 14U &&
        fwd.runtimeState.machineData[0] == 3U) {
      if (!targetDisplayState.valid || targetDisplayState.frameData.empty()) {
        rewindClearHistory_();
        return false;
      }
      try {
        // No vm.run(): this is the exact already-recorded future.
        rewindLoadState_(targetState);
        if (!vm.restoreRewindRuntimeState(fwd.runtimeState) ||
            !vm.getVideoDisplay().restoreRewindState(targetDisplayState, true))
          throw Exception("mounted IMG recorded-next restore failed");
      }
      catch (...) {
        rewindClearHistory_();
        throw;
      }
      rewindCurrentState.swap(targetState);
      rewindCurrentDisplayState = targetDisplayState;
      rewindCurrentRuntimeState = fwd.runtimeState;
      rewindLastFrameSequence = vm.getVideoDisplay().getFrameSequenceNumber();
      rewindFramesAvailable =
          (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
      rewindUpdateTimelineStatus_();
      rewindInputReconcilePending = true;
      return true;
    }
    // For now reconstruct only the host-visible frame, then restore the exact
    // recorded machine/runtime state. Unlike the old Next implementation, the
    // resulting guest state is never taken from re-simulation.
    const bool audioWasEnabled = vm.getEnableAudioOutput();
    vm.setEnableAudioOutput(false);
    try {
      // One stored reverse step behind the current past state is the anchor
      // needed by the existing complete-frame reconstruction path.
      if (rewindHistory.size() >= 2) {
        std::vector<unsigned char> renderState(rewindCurrentState);
        // The newest reverse delta is the transition we just recreated from
        // current past -> target future. The display anchor must be one state
        // earlier than current past, so use the preceding reverse delta.
        const RewindDelta& anchorDelta =
            rewindHistory[rewindHistory.size() - 2];
        if (rewindApplyDelta_(renderState, anchorDelta.data)) {
          VideoDisplay::RewindState anchorDisplayState;
          if (!rewindLoadDisplayDelta_(anchorDisplayState, anchorDelta,
                                       rewindCurrentDisplayState))
            throw Exception("invalid forward render anchor video delta");
          rewindLoadState_(renderState);
          if (anchorDelta.runtimeState.valid)
            (void) vm.restoreRewindRuntimeState(anchorDelta.runtimeState);
          if (anchorDisplayState.valid)
            (void) vm.getVideoDisplay().restoreRewindState(
                anchorDisplayState, true);

          vm.setRewindRenderMode(true);
          uint64_t firstFrame =
              vm.getVideoDisplay().getFrameSequenceNumber();
          size_t elapsedMicroseconds = 0;
          while (vm.getVideoDisplay().getFrameSequenceNumber() == firstFrame
                 && elapsedMicroseconds < 50000) {
            vm.run(2000);
            elapsedMicroseconds += 2000;
          }
          if (vm.getVideoDisplay().getFrameSequenceNumber() == firstFrame)
            throw Exception("forward rewind render first frame timeout");

          uint64_t secondFrame =
              vm.getVideoDisplay().getFrameSequenceNumber();
          elapsedMicroseconds = 0;
          while (vm.getVideoDisplay().getFrameSequenceNumber() == secondFrame
                 && elapsedMicroseconds < 50000) {
            vm.run(2000);
            elapsedMicroseconds += 2000;
          }
          if (vm.getVideoDisplay().getFrameSequenceNumber() == secondFrame)
            throw Exception("forward rewind render complete frame timeout");
        }
      }

      vm.setRewindRenderMode(false);
      rewindLoadState_(targetState);
      if (fwd.runtimeState.valid)
        (void) vm.restoreRewindRuntimeState(fwd.runtimeState);
      if (targetDisplayState.valid)
        (void) vm.getVideoDisplay().restoreRewindState(targetDisplayState, false);
    }
    catch (...) {
      vm.setRewindRenderMode(false);
      vm.setEnableAudioOutput(audioWasEnabled);
      rewindClearHistory_();
      throw;
    }
    vm.setRewindRenderMode(false);
    vm.setEnableAudioOutput(audioWasEnabled);

    rewindCurrentState.swap(targetState);
    rewindCurrentDisplayState = targetDisplayState;
    rewindCurrentRuntimeState = fwd.runtimeState;
    rewindLastFrameSequence =
        vm.getVideoDisplay().getFrameSequenceNumber();
    rewindFramesAvailable =
        (rewindHistory.size() > 2 ? rewindHistory.size() - 2 : 0);
    rewindUpdateTimelineStatus_();

    // Keep reconciliation pending while browsing recorded future. It is
    // applied only when the user actually branches/resumes live execution.
    return true;
  }

  bool VMThread::process()
  {
    // check and process any pending messages
    mutex_.lock();
    while (true) {
      if (exitFlag) {
        mutex_.unlock();
        this->cleanup();
        return false;
      }
      Message *m = messageQueue;
      if (!m)
        break;
      messageQueue = m->nextMessage;
      if (!messageQueue)
        lastMessage = (Message *) 0;
      messageCnt--;
      try {
        m->process();
      }
      catch (Exception& e) {
        pauseFlag = true;
        mutex_.unlock();
        errorCallback(userData, e.what());
        mutex_.lock();
      }
      catch (...) {
        errorFlag = true;
        mutex_.unlock();
        m->~Message();
        std::free(m);
        this->cleanup();
        return false;
      }
      m->~Message();
      m->nextMessage = freeMessageStack;
      freeMessageStack = m;
    }

    // Exact live rewind requires one full VM-state capture per emulated video
    // frame. At high-speed settings this becomes the dominant workload and
    // prevents the requested speed from being reached. Keep exact rewind only
    // at <= 200%; 400% / custom >200% / No limit run without live capture.
    // Apply the transition inside VMThread so history cannot race an in-flight
    // capture. Clearing at both edges guarantees that no rewind timeline spans
    // a period in which frames were intentionally not recorded.
    if (rewindCaptureEnabled != rewindCaptureRequested) {
      rewindCaptureEnabled = rewindCaptureRequested;
      rewindClearHistory_();
    }

#ifdef WIN32
    const bool newAudioPause = pauseFlag;
#endif
    nxtTime += double(timesliceLength);
    mutex_.unlock();
#ifdef WIN32
    if (newAudioPause != audioPauseApplied) {
      // Keep the host device transition on the same thread as audio writes,
      // outside VMThread's mutex to avoid blocking host UI/input on PortAudio.
      vm.getAudioOutput().setEmulationPaused(newAudioPause);
      audioPauseApplied = newAudioPause;
    }
#endif
    // The live VM step is only 2 ms. The former implementation opened each
    // mounted IMG many times per step to verify its host file identity, even
    // with rewind recording disabled. On Windows that induced thousands of
    // CreateFile calls/sec and repeated antivirus checks.
    // Media identity is checked at each *published frame* below, and again
    // immediately before every Previous/Next restore. Guest writes and host
    // FILE: activity therefore cannot enter a captured/restored history.
    vm.getVideoDisplay().setRewindFrameCaptureEnabled(
        vm.getIsRewindDirectFrameRequired());
    rewindDemoGuardActive = vm.getIsRecordingDemo()
                            || vm.getIsPlayingDemo();
    // Paused sessions do not publish frames, but a host/guest disk write can
    // still occur while VMThread is locked by the GUI. Recheck the media
    // while paused (the paused loop sleeps for 10 ms) so stale history is
    // evicted even when no further display frame is generated. This path
    // never runs in the normal 2 ms live emulation loop.
    if (pauseFlag && (!rewindCurrentState.empty() ||
                      !rewindHistory.empty() || !rewindForwardHistory.empty())) {
      const uint64_t hostIOEpoch = vm.getRewindHostIOEpoch();
      const bool mediaSafe = vm.getIsRewindRestoreSupported();
      rewindBlockedReason = (mediaSafe ? 0U : vm.getRewindUnavailableReason());
      if (hostIOEpoch != rewindLastHostIOEpoch || !mediaSafe ||
          rewindDemoGuardActive) {
        rewindClearHistory_();
      }
      rewindLastHostIOEpoch = hostIOEpoch;
    }
    // run emulation, or wait if paused
    double  curTime = prvTime;
    try {
      if (processCallback)
        processCallback(userData);
      bool doFrameAdvance = false;
      bool doFrameRewind = false;
      bool doExactForward = false;
      int doFrameJump = 0;
      bool doInputReconcile = false;
      bool currentKeyboardState[128];
      uint8_t currentMouseButtonState = 0x00;
      mutex_.lock();
      if (rewindJumpRequest < 0 && pauseFlag) {
        if (rewindFramesAvailable > 0) {
          doFrameJump = -1;
          rewindJumpRequest++;
        }
        else {
          rewindJumpRequest = 0;
        }
      }
      else if (rewindJumpRequest > 0 && pauseFlag) {
        if (!rewindForwardHistory.empty()) {
          doFrameJump = 1;
          rewindJumpRequest--;
        }
        else {
          rewindJumpRequest = 0;
        }
      }
      if (!doFrameJump && rewindFrameRequests > 0U && pauseFlag
          && rewindFramesAvailable > 0) {
        // Keep this request reserved until the restore has completed. This
        // prevents the GUI thread from over-queuing against history that is
        // already being consumed by an in-flight rewind.
        doFrameRewind = true;
      }
      else if (rewindFrameRequests > 0U && rewindFramesAvailable == 0) {
        // Defensive cleanup for any stale request left at the history edge.
        rewindFrameRequests = 0U;
      }
      if (!doFrameJump && !doFrameRewind && frameAdvanceRequests > 0U) {
        frameAdvanceRequests--;
        doFrameAdvance = true;
        doExactForward = !rewindForwardHistory.empty();
      }
      if (rewindInputReconcilePending
          && (!pauseFlag || (doFrameAdvance && !doExactForward))) {
        for (int i = 0; i < 128; i++)
          currentKeyboardState[i] = keyboardState[i];
        currentMouseButtonState = mouseButtonState;
        rewindInputReconcilePending = false;
        doInputReconcile = true;
      }
      mutex_.unlock();
      if (doInputReconcile) {
        for (int i = 0; i < 128; i++)
          vm.setKeyboardState(i, currentKeyboardState[i]);
        vm.setMouseState(0, 0, currentMouseButtonState, 0x00);
      }
      if (doFrameJump < 0) {
        // Animated coarse scrub: execute exactly one ordinary rendered
        // Previous step per VMThread iteration. The remaining request is kept
        // in rewindJumpRequest, so the user sees every historical frame.
        (void) rewindRestorePreviousFrame_();
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
      else if (doFrameJump > 0) {
        // Forward coarse scrub is the same recorded-history operation as one
        // ordinary Next step. It never creates a new frame while paused.
        (void) rewindRestoreNextFrame_();
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
      else if (doFrameRewind) {
        (void) rewindRestorePreviousFrame_();
        mutex_.lock();
        if (rewindFrameRequests > 0U)
          rewindFrameRequests--;
        mutex_.unlock();
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
      else if (doFrameAdvance && doExactForward) {
        (void) rewindRestoreNextFrame_();
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
      else if (doFrameAdvance) {
        // There is no recorded future left: this is a new paused timeline
        // branch, so any stale forward states are discarded before simulation.
        rewindClearForwardHistory_();
        const uint64_t startFrame =
            vm.getVideoDisplay().getFrameSequenceNumber();
        const bool audioWasEnabled = vm.getEnableAudioOutput();
        vm.setEnableAudioOutput(false);
        try {
          // Use the same 2000 us VM quantum as normal live execution.
          // Rewind history is captured after a live 2000 us slice crosses a
          // frame boundary; using a finer quantum here stops at a different
          // emulation phase and makes Previous -> Next drift away from the
          // original future timeline even with deterministic guest code.
          // The 50 ms guard protects against a broken/missing frame callback.
          size_t elapsedMicroseconds = 0;
          while (vm.getVideoDisplay().getFrameSequenceNumber() == startFrame &&
                 elapsedMicroseconds < 50000) {
            vm.run(2000);
            elapsedMicroseconds += 2000;
          }
        }
        catch (...) {
          vm.setEnableAudioOutput(audioWasEnabled);
          throw;
        }
        vm.setEnableAudioOutput(audioWasEnabled);
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
      else if (!pauseFlag) {
        rewindClearForwardHistory_();
        vm.run(2000);
        curTime = speedTimer.getRealTime();
        if (curTime < nxtTime)
          Timer::wait(nxtTime - curTime);
        else if (curTime > (nxtTime + 0.25))
          nxtTime = curTime;
      }
      else {
        Timer::wait(0.01);
        curTime = speedTimer.getRealTime();
        nxtTime = curTime;
      }
    }
    catch (Exception& e) {
      mutex_.lock();
      pauseFlag = true;
      mutex_.unlock();
      errorCallback(userData, e.what());
    }
    catch (...) {
      mutex_.lock();
      errorFlag = true;
      mutex_.unlock();
      this->cleanup();
      return false;
    }
    {
      // Only a completed host-visible frame can be published to rewind.
      // Do not touch host IMG paths during the intervening 2 ms quanta.
      const uint64_t frameSequence =
          vm.getVideoDisplay().getFrameSequenceNumber();
      if (frameSequence != rewindLastFrameSequence) {
        rewindLastFrameSequence = frameSequence;
        if (rewindCaptureEnabled || !rewindCurrentState.empty() ||
            !rewindHistory.empty() || !rewindForwardHistory.empty()) {
          const uint64_t hostIOEpoch = vm.getRewindHostIOEpoch();
          const bool mediaSafe = vm.getIsRewindRestoreSupported();
          rewindBlockedReason = (mediaSafe ? 0U
                                      : vm.getRewindUnavailableReason());
          if (hostIOEpoch != rewindLastHostIOEpoch || !mediaSafe ||
              vm.getIsRecordingDemo() || vm.getIsPlayingDemo()) {
            if (!rewindCurrentState.empty() || !rewindHistory.empty() ||
                !rewindForwardHistory.empty())
              rewindClearHistory_();
          }
          rewindLastHostIOEpoch = hostIOEpoch;
        }
        rewindCaptureFrame_();
      }
    }

    // update status information
    mutex_.lock();
    float   deltaTime = float(curTime - prvTime);
    prvTime = curTime;
    deltaTime = (deltaTime > 0.0f ? deltaTime : 0.0f);
    deltaTime = (deltaTime < 1.0f ? deltaTime : 1.0f);
    avgTimesliceLength = (avgTimesliceLength * 0.995f) + (deltaTime * 0.005f);
    try {
      vm.getVMStatus(vmStatus);
    }
    catch (...) {
      errorFlag = true;
      mutex_.unlock();
      this->cleanup();
      return false;
    }
    mutex_.unlock();
    return true;
  }

  void VMThread::run()
  {
#ifdef __APPLE__
    int appliedHostThreadPriority = 99;
#endif
    while (true) {
#ifdef __APPLE__
      mutex_.lock();
      int requestedHostThreadPriority = hostThreadPriority;
      mutex_.unlock();
      if (requestedHostThreadPriority != appliedHostThreadPriority) {
        qos_class_t qosClass = QOS_CLASS_DEFAULT;
        switch (requestedHostThreadPriority) {
        case -2:
          qosClass = QOS_CLASS_BACKGROUND;
          break;
        case -1:
          qosClass = QOS_CLASS_UTILITY;
          break;
        case 1:
          qosClass = QOS_CLASS_USER_INITIATED;
          break;
        case 2:
          qosClass = QOS_CLASS_USER_INTERACTIVE;
          break;
        default:
          qosClass = QOS_CLASS_DEFAULT;
          break;
        }
        int err = pthread_set_qos_class_self_np(qosClass, 0);
        appliedHostThreadPriority = requestedHostThreadPriority;
        if (err != 0)
          errorCallback(userData, "cannot set macOS emulation-thread QoS");
      }
#endif
      mutex_.lock();
      bool  lockFlag = (lockCnt != 0UL);
      if (lockFlag)
        threadLock2.notify();
      mutex_.unlock();
      if (lockFlag) {
        threadLock1.wait();
        // Acknowledge that the VM thread has actually consumed the unlock
        // signal before the GUI is allowed to start another lock handshake.
        threadUnlockAck.notify();
      }
      if (!this->process())
        break;
    }
    this->cleanup();
  }

  VMThread::VMThreadStatus::VMThreadStatus(VMThread& vmThread_)
    : threadStatus(0)
  {
    vmThread_.mutex_.lock();
    if (vmThread_.avgTimesliceLength > 0.0000002f)
      speedPercentage = 0.2f / vmThread_.avgTimesliceLength;
    else
      speedPercentage = 1000000.0f;
    isPaused = vmThread_.pauseFlag;
    rewindFramesAvailable = vmThread_.rewindFramesAvailable;
    // The rewind engine is shared by all emulated machine types. The VM
    // thread publishes position/length as one packed atomic pair.
    const uint64_t timelineStatus = vmThread_.rewindTimelineStatus;
    rewindTimelinePosition = size_t(timelineStatus & 0xFFFFFFFFULL);
    rewindTimelineLength = size_t(timelineStatus >> 32);
    rewindBufferUsageBytes = vmThread_.rewindBufferUsageBytes;
    rewindAllocatedUsageBytes = vmThread_.rewindAllocatedUsageBytes;
    rewindBufferLimitBytes = vmThread_.rewindHistoryLimitBytes;
    rewindUnavailableReason = (!vmThread_.rewindCaptureUserEnabled ? 6U
                               : !vmThread_.rewindCaptureSpeedAllowed ? 7U
                               : vmThread_.rewindBlockedReason.load());
    isRecordingDemo = vmThread_.vmStatus.isRecordingDemo;
    isPlayingDemo = vmThread_.vmStatus.isPlayingDemo;
    tapeReadOnly = vmThread_.vmStatus.tapeReadOnly;
    tapePosition = vmThread_.vmStatus.tapePosition;
    tapeLength = vmThread_.vmStatus.tapeLength;
    tapeSampleRate = vmThread_.vmStatus.tapeSampleRate;
    tapeSampleSize = vmThread_.vmStatus.tapeSampleSize;
    floppyDriveLEDState = vmThread_.vmStatus.floppyDriveLEDState;
    tapeRemoteState = vmThread_.vmStatus.tapeRemoteState;
    tapeMechanicalEngagementCount =
        vmThread_.vmStatus.tapeMechanicalEngagementCount;
    if (vmThread_.exitFlag)
      threadStatus = (vmThread_.errorFlag ? -1 : 1);
    vmThread_.mutex_.unlock();
  }

  int VMThread::lock(size_t t)
  {
    mutex_.lock();
    if (exitFlag) {
      lockCnt = 0UL;
      threadLock1.notify();
      mutex_.unlock();
      return -1;
    }
    lockCnt++;
    if (lockCnt > 1UL) {
      mutex_.unlock();
      return 0;
    }
    threadLock1.wait(0);
    threadLock2.wait(0);
    mutex_.unlock();
    bool  tmp = threadLock2.wait(t);
    mutex_.lock();
    if (exitFlag || !tmp) {
      lockCnt--;
      int   retval = (exitFlag ? -1 : 1);
      threadLock1.notify();
      mutex_.unlock();
      return retval;
    }
    mutex_.unlock();
    return 0;
  }

  void VMThread::unlock()
  {
    bool waitForAck = false;
    mutex_.lock();
    if (lockCnt) {
      lockCnt--;
      if (!lockCnt) {
        // Clear any stale acknowledgement before releasing the VM thread.
        threadUnlockAck.wait(0);
        threadLock1.notify();
        waitForAck = true;
      }
    }
    mutex_.unlock();

    // The old Timer::wait(0.0) only yielded the host thread and did not
    // guarantee that the VM had consumed threadLock1. An immediate relock
    // could therefore clear that wake-up signal and time out. Wait for the
    // VM's explicit acknowledgement instead. The timeout only protects
    // shutdown/error paths; normal acknowledgement is immediate.
    if (waitForAck)
      (void) threadUnlockAck.wait(2000);
  }

  // --------------------------------------------------------------------------

  void VMThread::pause(bool n)
  {
    mutex_.lock();
    const bool wasPaused = pauseFlag;
    pauseFlag = n;
    if (!n) {
      frameAdvanceRequests = 0U;
      rewindFrameRequests = 0U;
      rewindJumpRequest = 0;
      if (wasPaused) {
        // Resume is the only normal branch point from the frozen timeline.
        // A restored historical frame can contain keyboard/joystick state that
        // no longer matches the host, so always reconcile current host input
        // before live emulation is allowed to run.
        rewindInputReconcilePending = true;
      }
    }
    mutex_.unlock();
  }

  bool VMThread::advanceFrame()
  {
    mutex_.lock();
    bool accepted = (pauseFlag && !exitFlag && vm.getEnableDisplay() &&
                     frameAdvanceRequests < 16U);
    if (accepted)
      frameAdvanceRequests++;
    mutex_.unlock();
    return accepted;
  }

  bool VMThread::rewindFrame()
  {
    mutex_.lock();
    const size_t available = rewindFramesAvailable;
    // rewindFrameRequests counts both queued and the currently executing
    // rewind request. Never reserve more Previous Frame operations than the
    // current history can actually satisfy; otherwise stale requests can
    // survive at the history boundary and consume frames created by later
    // Next Frame operations.
    bool accepted = (pauseFlag && !exitFlag && !rewindDemoGuardActive
                     && size_t(rewindFrameRequests) < available
                     && rewindFrameRequests < 16U);
    if (accepted)
      rewindFrameRequests++;
    mutex_.unlock();
    return accepted;
  }

  size_t VMThread::rewindFrames(size_t n)
  {
    if (!n)
      return 0;
    mutex_.lock();
    size_t accepted = 0;
    if (pauseFlag && !exitFlag && !rewindDemoGuardActive && rewindJumpRequest == 0
        && frameAdvanceRequests == 0U && rewindFrameRequests == 0U) {
      const size_t available = rewindFramesAvailable;
      accepted = (n < available ? n : available);
      if (accepted > size_t(0x7FFFFFFF))
        accepted = size_t(0x7FFFFFFF);
      if (accepted)
        rewindJumpRequest = -int(accepted);
    }
    mutex_.unlock();
    return accepted;
  }

  size_t VMThread::advanceRecordedFrames(size_t n)
  {
    if (!n)
      return 0;
    mutex_.lock();
    size_t accepted = 0;
    if (pauseFlag && !exitFlag && !rewindDemoGuardActive && rewindJumpRequest == 0
        && frameAdvanceRequests == 0U && rewindFrameRequests == 0U) {
      const uint64_t timelineStatus = rewindTimelineStatus;
      const size_t position = size_t(timelineStatus & 0xFFFFFFFFULL);
      const size_t length = size_t(timelineStatus >> 32);
      const size_t available = (length > position ? length - position : 0);
      accepted = (n < available ? n : available);
      if (accepted > size_t(0x7FFFFFFF))
        accepted = size_t(0x7FFFFFFF);
      if (accepted)
        rewindJumpRequest = int(accepted);
    }
    mutex_.unlock();
    return accepted;
  }

  void VMThread::clearRewindHistory()
  {
    queueMessage(allocateMessage<Message_ClearRewind>());
  }

  void VMThread::clearRewindHistoryLocked(bool demoStarting)
  {
    // Caller already stopped VMThread with VMThread::lock(). This is an
    // immediate barrier, unlike queued clearRewindHistory().
    mutex_.lock();
    rewindClearHistory_();
    if (demoStarting)
      rewindDemoGuardActive = true;
    mutex_.unlock();
  }

  void VMThread::setRewindHistoryLimit(size_t limitBytes_)
  {
    queueMessage(allocateMessage<Message_SetRewindLimit, size_t>(limitBytes_));
  }

  void VMThread::quit(bool waitFlag_)
  {
    mutex_.lock();
    exitFlag = true;
    pauseFlag = true;
    lockCnt = 0UL;
    threadLock1.notify();
    if (joinFlag || !waitFlag_) {
      mutex_.unlock();
      return;
    }
    joinFlag = true;
    mutex_.unlock();
    this->join();
  }

  void VMThread::setUserData(void *userData_)
  {
    userData = userData_;
  }

  void VMThread::setErrorCallback(void (*func)(void *userData_,
                                               const char *msg_))
  {
    if (func)
      errorCallback = func;
    else
      errorCallback = &dummyErrorCallback;
  }

  void VMThread::reset(bool isColdReset_)
  {
    queueMessage(allocateMessage<Message_Reset, bool>(isColdReset_));
  }

  void VMThread::setFileIOState(const std::string& directory_, bool enabled_,
                                bool defaultDeviceIsFILE_)
  {
    mutex_.lock();
    queuedFileIODirectory = directory_;
    queuedFileIOEnabled = enabled_;
    queuedFileIODefaultDevice = defaultDeviceIsFILE_;
    const uint64_t generation = ++queuedFileIOGeneration;
    mutex_.unlock();
    queueMessage(allocateMessage<Message_FileIOState, uint64_t>(generation));
  }

  void VMThread::invalidateQueuedFileIOStateLocked()
  {
    // Do not remove list entries or destroy pending message allocations here:
    // the VM thread safely discards obsolete generations on dequeue.
    mutex_.lock();
    ++queuedFileIOGeneration;
    mutex_.unlock();
  }

  void VMThread::setAudioOutputEnabled(bool enabled_)
  {
    queueMessage(allocateMessage<Message_SetAudioEnabled, bool>(enabled_));
  }

  void VMThread::setAudioOutputVolume(double ampScale_)
  {
    queueMessage(allocateMessage<Message_SetVolume, double>(ampScale_));
  }

  void VMThread::setKeyboardState(uint8_t keyCode_, bool isPressed_)
  {
    queueMessage(allocateMessage<Message_KeyboardEvent, uint8_t, bool>(
                     keyCode_, isPressed_));
  }

  void VMThread::setMouseState(int8_t dX, int8_t dY,
                               uint8_t buttonState, uint8_t mouseWheelEvents)
  {
    queueMessage(allocateMessage<Message_MouseEvent, uint32_t>(
                     Message_MouseEvent::packMouseEvent(dX, dY, buttonState,
                                                        mouseWheelEvents)));
  }

  void VMThread::resetKeyboard()
  {
    queueMessage(allocateMessage<Message_ResetKeyboard>());
  }

  void VMThread::resetKeyboardLocked()
  {
    mutex_.lock();
    resetKeyboard_();
    mutex_.unlock();
  }

  void VMThread::tapePlay()
  {
    queueMessage(allocateMessage<Message_TapePlay>());
  }

  void VMThread::tapeRecord()
  {
    queueMessage(allocateMessage<Message_TapeRecord>());
  }

  void VMThread::tapeStop()
  {
    queueMessage(allocateMessage<Message_TapeStop>());
  }

  void VMThread::tapeSeek(double seekTime_)
  {
    queueMessage(allocateMessage<Message_TapeSeek, int, double>(0, seekTime_));
  }

  void VMThread::tapeSeekToCuePoint(bool isForward, double t)
  {
    queueMessage(allocateMessage<Message_TapeSeek, int, double>((isForward ?
                                                                 1 : -1), t));
  }

  void VMThread::stopDemo()
  {
    queueMessage(allocateMessage<Message_StopDemo>());
  }

  void VMThread::setProcessCallback(void (*func)(void *userData_))
  {
    processCallback = func;
  }

  void VMThread::setRewindCaptureUserEnabled(bool enabled)
  {
    mutex_.lock();
    rewindCaptureUserEnabled = enabled;
    rewindCaptureRequested = enabled && rewindCaptureSpeedAllowed;
    mutex_.unlock();
  }

  void VMThread::setSpeedPercentage(int speedPercentage_,
                                    bool audioClockDriven)
  {
    mutex_.lock();
    if (speedPercentage_ > 0 && !audioClockDriven)
      timesliceLength = 0.2f / float(speedPercentage_);
    else
      timesliceLength = 0.0f;

    // Per-frame rewind capture is intentionally suspended above 200% and in
    // No limit mode. The VM thread applies the transition and clears history
    // at a safe boundary, so a later rewind can never cross an uncaptured gap.
    rewindCaptureSpeedAllowed =
        (speedPercentage_ > 0 && speedPercentage_ <= 200);
    rewindCaptureRequested =
        rewindCaptureUserEnabled && rewindCaptureSpeedAllowed;
    mutex_.unlock();
  }

  void VMThread::setHostThreadPriority(int priority_)
  {
    priority_ = (priority_ > -2 ? (priority_ < 2 ? priority_ : 2) : -2);
    mutex_.lock();
    hostThreadPriority = priority_;
    mutex_.unlock();
  }

  VMThread::Message * VMThread::allocateMessage_()
  {
    mutex_.lock();
    if (freeMessageStack) {
      Message *m = freeMessageStack;
      freeMessageStack = m->nextMessage;
      mutex_.unlock();
      return m;
    }
    if (messageCnt >= 1024) {
      // too many messages, delete oldest one
      messageCnt--;
      Message *m = messageQueue;
      messageQueue = m->nextMessage;
      mutex_.unlock();
      m->~Message();
      return m;
    }
    mutex_.unlock();
    Message *m = (Message *) std::malloc(sizeof(Message_Dummy));
    if (!m)
      errorCallback(userData, "cannot allocate memory for message");
    return m;
  }

  void VMThread::queueMessage(VMThread::Message *m)
  {
    if (!m)
      return;
    mutex_.lock();
    if (exitFlag) {
      mutex_.unlock();
      m->~Message();
      std::free(m);
      return;
    }
    m->nextMessage = (Message *) 0;
    if (lastMessage)
      lastMessage->nextMessage = m;
    else
      messageQueue = m;
    lastMessage = m;
    messageCnt++;
    mutex_.unlock();
  }

  // --------------------------------------------------------------------------

  VMThread::Message::~Message()
  {
  }

  VMThread::Message_Reset::~Message_Reset()
  {
  }

  void VMThread::Message_Reset::process()
  {
    vmThread.rewindClearHistory_();
    vmThread.vm.reset(isColdReset);
  }

  VMThread::Message_FileIOState::~Message_FileIOState()
  {
  }

  void VMThread::Message_FileIOState::process()
  {
    if (generation != vmThread.queuedFileIOGeneration)
      return; // F11 or a later archive request superseded this message.
    vmThread.rewindClearHistory_();
    vmThread.vm.setWorkingDirectory(vmThread.queuedFileIODirectory);
    vmThread.vm.setEnableFileIO(vmThread.queuedFileIOEnabled);
    vmThread.vm.setFileIODefaultDevice(vmThread.queuedFileIODefaultDevice);
  }

  VMThread::Message_ClearRewind::~Message_ClearRewind()
  {
  }

  void VMThread::Message_ClearRewind::process()
  {
    vmThread.rewindClearHistory_();
  }

  VMThread::Message_SetRewindLimit::~Message_SetRewindLimit()
  {
  }

  void VMThread::Message_SetRewindLimit::process()
  {
    vmThread.rewindHistoryLimitBytes = limitBytes;
    vmThread.rewindTrimToLimit_();
  }

  VMThread::Message_SetAudioEnabled::~Message_SetAudioEnabled()
  {
  }

  void VMThread::Message_SetAudioEnabled::process()
  {
    vmThread.vm.setEnableAudioOutput(enabled);
  }

  VMThread::Message_SetVolume::~Message_SetVolume()
  {
  }

  void VMThread::Message_SetVolume::process()
  {
    vmThread.vm.setAudioOutputVolume(float(ampScale));
  }

  VMThread::Message_KeyboardEvent::~Message_KeyboardEvent()
  {
  }

  void VMThread::Message_KeyboardEvent::process()
  {
    if (vmThread.keyboardState[keyCode & 0x7F] == isPressed)
      return;

    vmThread.keyboardState[keyCode & 0x7F] = isPressed;
    if (vmThread.pauseFlag) {
      // Pause is a frozen timeline browser. Host input is remembered for the
      // eventual branch/resume point, but must not mutate historical guest
      // state or destroy recorded future.
      vmThread.rewindInputReconcilePending = true;
      return;
    }

    vmThread.rewindClearForwardHistory_();
    vmThread.vm.setKeyboardState(keyCode & 0x7F, isPressed);
  }

  VMThread::Message_MouseEvent::~Message_MouseEvent()
  {
  }

  void VMThread::Message_MouseEvent::process()
  {
    int8_t  dX = 0;
    int8_t  dY = 0;
    uint8_t buttonState = 0x00;
    uint8_t mouseWheelEvents = 0x00;
    unpackMouseEvent(dX, dY, buttonState, mouseWheelEvents);
    vmThread.mouseButtonState = buttonState;

    if (vmThread.pauseFlag) {
      // Relative movement/wheel activity while browsing history is discarded;
      // the current button state is reconciled only when execution resumes.
      vmThread.rewindInputReconcilePending = true;
      return;
    }

    vmThread.rewindClearForwardHistory_();
    vmThread.vm.setMouseState(dX, dY, buttonState, mouseWheelEvents);
  }

  void VMThread::resetKeyboard_()
  {
    bool changed = (mouseButtonState != 0x00);
    for (uint8_t i = 0; i <= 127; i++) {
      if (keyboardState[i]) {
        changed = true;
        keyboardState[i] = false;
      }
    }
    mouseButtonState = 0x00;

    if (!changed)
      return;

    if (pauseFlag) {
      rewindInputReconcilePending = true;
      return;
    }

    rewindClearForwardHistory_();
    for (uint8_t i = 0; i <= 127; i++)
      vm.setKeyboardState(i, false);
    vm.setMouseState(0, 0, 0x00, 0x00);
  }

  VMThread::Message_ResetKeyboard::~Message_ResetKeyboard()
  {
  }

  void VMThread::Message_ResetKeyboard::process()
  {
    vmThread.resetKeyboard_();
  }

  VMThread::Message_TapePlay::~Message_TapePlay()
  {
  }

  void VMThread::Message_TapePlay::process()
  {
    vmThread.vm.tapePlay();
  }

  VMThread::Message_TapeRecord::~Message_TapeRecord()
  {
  }

  void VMThread::Message_TapeRecord::process()
  {
    vmThread.vm.tapeRecord();
  }

  VMThread::Message_TapeStop::~Message_TapeStop()
  {
  }

  void VMThread::Message_TapeStop::process()
  {
    vmThread.vm.tapeStop();
  }

  VMThread::Message_TapeSeek::~Message_TapeSeek()
  {
  }

  void VMThread::Message_TapeSeek::process()
  {
    if (seekDirection == 0)
      vmThread.vm.tapeSeek(seekTime);
    else if (seekDirection > 0)
      vmThread.vm.tapeSeekToCuePoint(true, seekTime);
    else
      vmThread.vm.tapeSeekToCuePoint(false, seekTime);
  }

  VMThread::Message_StopDemo::~Message_StopDemo()
  {
  }

  void VMThread::Message_StopDemo::process()
  {
    vmThread.vm.stopDemo();
    vmThread.rewindClearHistory_();
    vmThread.rewindDemoGuardActive = false;
  }

  VMThread::Message_Dummy::~Message_Dummy()
  {
  }

  void VMThread::Message_Dummy::process()
  {
    throw std::exception();
  }

}       // namespace Ep128Emu

