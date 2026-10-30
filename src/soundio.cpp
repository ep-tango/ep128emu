
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2017 Istvan Varga <istvanv@users.sourceforge.net>
// http://sourceforge.net/projects/ep128emu/
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
#include "soundio.hpp"
#include "vm.hpp"

#include <sndfile.h>
#include <portaudio.h>
#ifdef __APPLE__
#  include <lame/lame.h>
#endif
#ifdef ENABLE_MIDI_PORT
#  include <portmidi.h>
#  include <porttime.h>
#endif
#include <vector>

#ifdef ENABLE_SOUND_DEBUG

static bool isPortAudioError(const char *msg, PaError paError)
{
  std::fprintf(stderr, " === %s\n", msg);
  if (paError == paNoError)
    return false;
  std::fprintf(stderr,
               " *** PortAudio error (error code = %d)\n", int(paError));
  std::fprintf(stderr, " ***   %s\n", Pa_GetErrorText(paError));
#  ifndef USING_OLD_PORTAUDIO_API
  if (paError == paUnanticipatedHostError) {
    const PaHostErrorInfo   *errInfo = Pa_GetLastHostErrorInfo();
    std::fprintf(stderr, " *** host API error:\n");
    std::fprintf(stderr, " ***   %s\n", errInfo->errorText);
  }
#  endif
  return true;
}

#else

static inline bool isPortAudioError(const char *msg, PaError paError)
{
  (void) msg;
  return (paError != paNoError);
}

#endif

namespace Ep128Emu {

  static bool soundFileNameHasExtension(const std::string& fileName,
                                        const char *ext)
  {
    if (!ext)
      return false;
    size_t n = std::strlen(ext);
    if (fileName.length() < n)
      return false;
    size_t p = fileName.length() - n;
    for (size_t i = 0; i < n; i++) {
      char a = fileName[p + i];
      char b = ext[i];
      if (a >= 'A' && a <= 'Z')
        a = char(a + ('a' - 'A'));
      if (b >= 'A' && b <= 'Z')
        b = char(b + ('a' - 'A'));
      if (a != b)
        return false;
    }
    return true;
  }

  static SNDFILE *openSoundOutputFile(const std::string& fileName,
                                      float sampleRate)
  {
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(SF_INFO));
    // For write mode the final frame count is not known yet. libsndfile
    // accepts 0 for this; -1 happened to work for WAV but is rejected by
    // FLAC and MPEG writers as an incomplete SF_INFO structure.
    sfinfo.frames = 0;
    sfinfo.samplerate = int(sampleRate + 0.5);
    sfinfo.channels = 2;

    bool mp3Format = soundFileNameHasExtension(fileName, ".mp3");
    bool flacFormat = soundFileNameHasExtension(fileName, ".flac");
    if (mp3Format)
      sfinfo.format = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    else if (flacFormat)
      sfinfo.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
    else
      sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    if (!sf_format_check(&sfinfo))
      throw Exception("unsupported sound output format");

    SNDFILE *f = sf_open(fileName.c_str(), SFM_WRITE, &sfinfo);
    if (!f)
      throw Exception("error opening output sound file");

    if (mp3Format) {
      // Native libsndfile/LAME VBR. Quality 0.25 is intentionally on the
      // high side for clean emulator audio while remaining much smaller than PCM.
      int mode = SF_BITRATE_MODE_VARIABLE;
      (void) sf_command(f, SFC_SET_BITRATE_MODE, &mode, sizeof(mode));
      double quality = 0.25;
      (void) sf_command(f, SFC_SET_VBR_ENCODING_QUALITY,
                        &quality, sizeof(quality));
      int actualMode = sf_command(f, SFC_GET_BITRATE_MODE, (void *) 0, 0);
      if (actualMode != SF_BITRATE_MODE_VARIABLE) {
        sf_close(f);
        throw Exception("MP3 VBR mode is not supported by this sound library");
      }
    }
    else if (flacFormat) {
      // Lossless: compression affects CPU/file size only, never audio quality.
      double compressionLevel = 0.5;
      (void) sf_command(f, SFC_SET_COMPRESSION_LEVEL,
                        &compressionLevel, sizeof(compressionLevel));
    }
    return f;
  }

  class AudioOutput::MP3WriterThread : private Thread
  {
   private:
#ifdef __APPLE__
    std::FILE *file_;
    lame_t lame_;
    std::vector<unsigned char> mp3Buf_;
#else
    SNDFILE *file_;
#endif
    std::vector<int16_t> ring_;
    size_t capacityFrames_;
    std::atomic<uint64_t> writeFrame_;
    std::atomic<uint64_t> readFrame_;
    std::atomic<bool> stopping_;
    std::atomic<bool> failed_;
    std::string errorText_;

    void setFailure_(const char *msg)
    {
      errorText_ = (msg ? msg : "MP3 encoder error");
      failed_.store(true, std::memory_order_release);
    }

   protected:
    virtual void run()
    {
      std::vector<int16_t> tmp(8192U * 2U);
      for (;;) {
        uint64_t r = readFrame_.load(std::memory_order_relaxed);
        uint64_t w = writeFrame_.load(std::memory_order_acquire);
        size_t nFrames = size_t(w - r);
        if (nFrames > 8192U)
          nFrames = 8192U;

        if (nFrames > 0U) {
          size_t pos = size_t(r % uint64_t(capacityFrames_));
          size_t n1 = nFrames;
          if (n1 > (capacityFrames_ - pos))
            n1 = capacityFrames_ - pos;
          std::memcpy(&(tmp[0]), &(ring_[pos * 2U]),
                      n1 * 2U * sizeof(int16_t));
          if (n1 < nFrames) {
            std::memcpy(&(tmp[n1 * 2U]), &(ring_[0]),
                        (nFrames - n1) * 2U * sizeof(int16_t));
          }

          if (!failed_.load(std::memory_order_relaxed)) {
#ifdef __APPLE__
            int n = lame_encode_buffer_interleaved(
                        lame_, reinterpret_cast<short *>(&(tmp[0])),
                        int(nFrames), &(mp3Buf_[0]), int(mp3Buf_.size()));
            if (n < 0 || (n > 0 && std::fwrite(&(mp3Buf_[0]), 1, size_t(n), file_)
                                      != size_t(n))) {
              setFailure_("error writing MP3 sound file -- is the disk full ?");
            }
#else
            if (sf_writef_short(file_,
                                reinterpret_cast<short *>(&(tmp[0])),
                                sf_count_t(nFrames)) != sf_count_t(nFrames)) {
              setFailure_("error writing MP3 sound file -- is the disk full ?");
            }
#endif
          }
          readFrame_.store(r + uint64_t(nFrames), std::memory_order_release);
          continue;
        }

        if (stopping_.load(std::memory_order_acquire))
          break;
        (void) wait(2);
      }

      if (file_) {
#ifdef __APPLE__
        if (!failed_.load(std::memory_order_relaxed)) {
          int n = lame_encode_flush(lame_, &(mp3Buf_[0]), int(mp3Buf_.size()));
          if (n < 0 || (n > 0 && std::fwrite(&(mp3Buf_[0]), 1, size_t(n), file_)
                                    != size_t(n)))
            setFailure_("error flushing MP3 sound file");
        }
        if (std::fflush(file_) != 0 && !failed_.load(std::memory_order_relaxed))
          setFailure_("error flushing MP3 sound file");
        if (!failed_.load(std::memory_order_relaxed))
          lame_mp3_tags_fid(lame_, file_);
        if (std::fclose(file_) != 0 && !failed_.load(std::memory_order_relaxed))
          setFailure_("error closing MP3 sound file");
        file_ = (std::FILE *) 0;
        if (lame_) {
          lame_close(lame_);
          lame_ = (lame_t) 0;
        }
#else
        sf_write_sync(file_);
        if (sf_close(file_) != 0 &&
            !failed_.load(std::memory_order_relaxed))
          setFailure_("error closing MP3 sound file");
        file_ = (SNDFILE *) 0;
#endif
      }
    }

   public:
    MP3WriterThread(const std::string& fileName, float sampleRate)
#ifdef __APPLE__
      : file_((std::FILE *) 0), lame_((lame_t) 0),
#else
      : file_((SNDFILE *) 0),
#endif
        capacityFrames_(size_t(sampleRate * 8.0f + 0.5f)),
        writeFrame_(0ULL), readFrame_(0ULL),
        stopping_(false), failed_(false), errorText_("")
    {
      if (capacityFrames_ < 65536U)
        capacityFrames_ = 65536U;
      ring_.resize(capacityFrames_ * 2U);
#ifdef __APPLE__
      file_ = fileOpen(fileName.c_str(), "w+b");
      if (!file_)
        throw Exception("error opening output sound file");
      lame_ = lame_init();
      if (!lame_) {
        std::fclose(file_);
        file_ = (std::FILE *) 0;
        throw Exception("error initializing LAME MP3 encoder");
      }
      if (lame_set_in_samplerate(lame_, int(sampleRate + 0.5f)) < 0 ||
          lame_set_out_samplerate(lame_, int(sampleRate + 0.5f)) < 0 ||
          lame_set_num_channels(lame_, 2) < 0 ||
          lame_set_mode(lame_, JOINT_STEREO) < 0 ||
          lame_set_VBR(lame_, vbr_default) < 0 ||
          lame_set_VBR_q(lame_, 2) < 0 ||
          lame_set_quality(lame_, 2) < 0 ||
          lame_init_params(lame_) < 0) {
        lame_close(lame_);
        lame_ = (lame_t) 0;
        std::fclose(file_);
        file_ = (std::FILE *) 0;
        throw Exception("error configuring LAME MP3 encoder");
      }
      mp3Buf_.resize(65536U);
#else
      file_ = openSoundOutputFile(fileName, sampleRate);
#endif
      start();
    }

    virtual ~MP3WriterThread()
    {
      stop();
    }

    bool push(const int16_t *buf, size_t nFrames, std::string& errorText)
    {
      if (failed_.load(std::memory_order_acquire)) {
        errorText = errorText_;
        return false;
      }
      uint64_t w = writeFrame_.load(std::memory_order_relaxed);
      uint64_t r = readFrame_.load(std::memory_order_acquire);
      uint64_t used = w - r;
      if (uint64_t(nFrames) > uint64_t(capacityFrames_) - used) {
        errorText = "MP3 encoder buffer overflow";
        return false;
      }

      size_t pos = size_t(w % uint64_t(capacityFrames_));
      size_t n1 = nFrames;
      if (n1 > (capacityFrames_ - pos))
        n1 = capacityFrames_ - pos;
      std::memcpy(&(ring_[pos * 2U]), buf,
                  n1 * 2U * sizeof(int16_t));
      if (n1 < nFrames) {
        std::memcpy(&(ring_[0]), &(buf[n1 * 2U]),
                    (nFrames - n1) * 2U * sizeof(int16_t));
      }
      writeFrame_.store(w + uint64_t(nFrames), std::memory_order_release);
      return true;
    }

    void stop()
    {
      if (!stopping_.exchange(true, std::memory_order_acq_rel))
        start();
      join();
    }

    bool failed(std::string& errorText) const
    {
      if (!failed_.load(std::memory_order_acquire))
        return false;
      errorText = errorText_;
      return true;
    }
  };

  AudioOutput::AudioOutput()
    : outputFileName(""),
      soundFile((SNDFILE *) 0),
      mp3Writer((MP3WriterThread *) 0),
      deviceNumber(-1),
      sampleRate(0.0f),
      totalLatency(0.0f),
      nPeriodsHW(0),
      nPeriodsSW(0)
  {
  }

  AudioOutput::~AudioOutput()
  {
    // NOTE: the destructor of a derived class is responsible for closing
    // the audio device if it is open
    closeOutputFile_();
  }

  void AudioOutput::closeOutputFile_()
  {
    if (mp3Writer) {
      mp3Writer->stop();
      delete mp3Writer;
      mp3Writer = (MP3WriterThread *) 0;
    }
    if (soundFile) {
      sf_close(soundFile);
      soundFile = (SNDFILE *) 0;
    }
  }

  void AudioOutput::openOutputFile_()
  {
    if (outputFileName.empty() || sampleRate <= 0.0f)
      return;
    if (soundFileNameHasExtension(outputFileName, ".mp3"))
      mp3Writer = new MP3WriterThread(outputFileName, sampleRate);
    else
      soundFile = openSoundOutputFile(outputFileName, sampleRate);
  }

  void AudioOutput::setParameters(int deviceNumber_, float sampleRate_,
                                  float totalLatency_,
                                  int nPeriodsHW_, int nPeriodsSW_)
  {
    deviceNumber_ = (deviceNumber_ >= 0 ? deviceNumber_ : -1);
    sampleRate_ = (sampleRate_ > 11025.0f ?
                   (sampleRate_ < 192000.0f ? sampleRate_ : 192000.0f)
                   : 11025.0f);
    totalLatency_ = (totalLatency_ > 0.005f ?
                     (totalLatency_ < 0.5f ? totalLatency_ : 0.5f)
                     : 0.005f);
    nPeriodsHW_ = (nPeriodsHW_ > 2 ? (nPeriodsHW_ < 16 ? nPeriodsHW_ : 16) : 2);
    nPeriodsSW_ = (nPeriodsSW_ > 1 ? (nPeriodsSW_ < 16 ? nPeriodsSW_ : 16) : 1);
    if (deviceNumber_ == deviceNumber &&
        sampleRate_ == sampleRate &&
        totalLatency_ == totalLatency &&
        nPeriodsHW_ == nPeriodsHW &&
        nPeriodsSW_ == nPeriodsSW)
      return;
    if (deviceNumber >= 0) {
      try {
        closeDevice();
      }
      catch (...) {
        // FIXME: should not ignore errors, although not likely to happen here
      }
    }
    deviceNumber = -1;
    totalLatency = totalLatency_;
    nPeriodsHW = nPeriodsHW_;
    nPeriodsSW = nPeriodsSW_;
    if (sampleRate_ != sampleRate) {
      sampleRate = sampleRate_;
      closeOutputFile_();
      if (outputFileName.length() != 0) {
        try {
          openOutputFile_();
        }
        catch (...) {
          outputFileName = "";
          throw;
        }
      }
    }
    deviceNumber = deviceNumber_;
    sampleRate = sampleRate_;
    if (deviceNumber >= 0) {
      try {
        openDevice();
      }
      catch (...) {
        deviceNumber = -1;
        throw;
      }
    }
  }

  void AudioOutput::setOutputFile(const std::string& fileName)
  {
    if (fileName == outputFileName)
      return;
    closeOutputFile_();
    outputFileName = "";
    if (fileName.length() != 0) {
      outputFileName = fileName;
      if (sampleRate > 0.0f) {
        try {
          openOutputFile_();
        }
        catch (...) {
          outputFileName = "";
          throw;
        }
      }
    }
  }

  void AudioOutput::sendAudioData(const int16_t *buf, size_t nFrames)
  {
    // MP3 encoding is deliberately isolated from the emulation/audio thread.
    // The producer only copies PCM into an 8-second lock-free SPSC ring;
    // there is no mutex or condition-variable signal on this path.
    // The MP3 codec runs exclusively on MP3WriterThread. On macOS this
    // calls libmp3lame directly, bypassing libsndfile's MP3 writer.
    if (mp3Writer) {
      std::string errorText;
      if (!mp3Writer->push(buf, nFrames, errorText)) {
        outputFileName = "";
        throw Exception(errorText.c_str());
      }
    }
    else if (soundFile) {
      // WAV/FLAC writing is cheap enough to retain the historical direct path.
      if (sf_writef_short(soundFile,
                          reinterpret_cast<short *>(const_cast<int16_t *>(buf)),
                          sf_count_t(nFrames))
          != sf_count_t(nFrames)) {
        sf_close(soundFile);
        soundFile = (SNDFILE *) 0;
        outputFileName = "";
        throw Exception("error writing sound file -- is the disk full ?");
      }
    }
  }

  void AudioOutput::setEmulationPaused(bool paused)
  {
    (void) paused;
    // Host audio may have platform-specific queued device output; recording
    // continues to use the original PCM source and must never be truncated.
  }

  void AudioOutput::closeDevice()
  {
    // NOTE: AudioOutput::closeDevice() should be called by derived classes
    // to reset internal data
    deviceNumber = -1;
  }

  std::vector< std::string > AudioOutput::getDeviceList()
  {
    std::vector< std::string >  tmp;
    return tmp;
  }

  void AudioOutput::openDevice()
  {
  }

  // --------------------------------------------------------------------------

  AudioOutput_PortAudio::AudioOutput_PortAudio()
    : AudioOutput(),
      paInitialized(false),
      disableRingBuffer(false),
      usingBlockingInterface(false),
#ifdef WIN32
      emulationPaused(false),
#endif
      paLockTimeout(0U),
      writeBufIndex(0),
      readBufIndex(0),
      paStream((PaStream *) 0),
      latencyFramesHW(4096L),
      nextTime(0.0),
      closeDeviceLock(true)
#ifdef __APPLE__
      , usingMacAudioRing(false),
      macAudioRingCapacityFrames(0),
      macAudioRingStartThresholdFrames(0),
      macAudioWriteFrame(0ULL),
      macAudioReadFrame(0ULL),
      macAudioRingStarted(false)
#endif
  {
    // initialize PortAudio
    if (isPortAudioError("calling Pa_Initialize()", Pa_Initialize()))
      throw Exception("error initializing PortAudio");
    paInitialized = true;
  }

  AudioOutput_PortAudio::~AudioOutput_PortAudio()
  {
    paLockTimeout = 0U;
    if (paStream) {
      (void) isPortAudioError("calling Pa_StopStream()",
                              Pa_StopStream(paStream));
      closeDeviceLock.wait();
      (void) isPortAudioError("calling Pa_CloseStream()",
                              Pa_CloseStream(paStream));
      closeDeviceLock.notify();
      paStream = (PaStream *) 0;
    }
    disableRingBuffer = false;
    usingBlockingInterface = false;
    writeBufIndex = 0;
    readBufIndex = 0;
    buffers.clear();
    if (paInitialized) {
      (void) isPortAudioError("calling Pa_Terminate()", Pa_Terminate());
      paInitialized = false;
    }
  }

  void AudioOutput_PortAudio::setEmulationPaused(bool paused)
  {
#ifdef WIN32
    // This is invoked only on VMThread, never from PortAudio's callback:
    // the 2 ms VM run quantum is complete and no Pa_WriteStream is active.
    // Pa_AbortStream (unlike Pa_StopStream) discards queued host samples so
    // Windows tape audio cannot play out for seconds after F10 Pause.
    if (emulationPaused == paused)
      return;
    emulationPaused = paused;
    if (!paStream)
      return;
    if (paused) {
      (void) isPortAudioError("aborting output on Pause",
                             Pa_AbortStream(paStream));
      return;
    }
    // The previous stream is stopped. Discard all pending software buffers
    // before restarting, otherwise the callback can replay pre-Pause sound.
    // VMThread exclusively owns the producer and PortAudio has stopped its
    // callback, so rebuilding buffer handshakes here is race-free.
    const size_t bufferCount = buffers.size();
    const size_t samplesPerBuffer =
        (bufferCount ? buffers[0].audioData.size() : 0U);
    buffers.clear();
    buffers.resize(bufferCount);
    for (size_t i=0; i<bufferCount; ++i)
      buffers[i].audioData.resize(samplesPerBuffer, int16_t(0));
    writeBufIndex = 0;
    readBufIndex = 0;
    timer_.reset();
    nextTime = 0.0;
    (void) isPortAudioError("restarting output after Pause",
                           Pa_StartStream(paStream));
#else
    (void) paused;
#endif
  }

  void AudioOutput_PortAudio::sendAudioData(const int16_t *buf, size_t nFrames)
  {
    if (paStream) {
#ifndef USING_OLD_PORTAUDIO_API
      if (usingBlockingInterface) {
        // reduce timing jitter
        double  t = nextTime - timer_.getRealTime();
        double  periodTime = double(long(nFrames)) / double(sampleRate);
        long    framesToWrite = Pa_GetStreamWriteAvailable(paStream);
        switch (int((framesToWrite << 3) / latencyFramesHW)) {
        case 0:
          periodTime = periodTime * 2.0;
          break;
        case 1:
          periodTime = periodTime * 1.25;
          break;
        case 2:
          periodTime = periodTime * 1.1;
          break;
        case 3:
          periodTime = periodTime * 1.05;
          break;
        case 4:
          periodTime = periodTime * 0.95;
          break;
        case 5:
          periodTime = periodTime * 0.9;
          break;
        default:
          timer_.reset();
          nextTime = 0.0;
          periodTime = 0.0;
          break;
        }
        nextTime = nextTime + periodTime;
        if (t > 0.00075) {
          Timer::wait(t);
        }
        else if (t < -0.5) {
          timer_.reset();
          nextTime = 0.0;
        }
        // ring buffer is not used for blocking I/O, so assume nPeriodsSW == 1
        for (size_t i = 0; i < nFrames; i++) {
          Buffer& buf_ = buffers[0];
          buf_.audioData[buf_.writePos++] = buf[(i << 1) + 0];
          buf_.audioData[buf_.writePos++] = buf[(i << 1) + 1];
          if (buf_.writePos >= buf_.audioData.size()) {
            buf_.writePos = 0;
            Pa_WriteStream(paStream,
                           &(buf_.audioData[0]), buf_.audioData.size() >> 1);
          }
        }
      }
      else
#endif
      {
#ifdef __APPLE__
        if (usingMacAudioRing) {
          unsigned long long w =
              macAudioWriteFrame.load(std::memory_order_relaxed);
          unsigned long long r =
              macAudioReadFrame.load(std::memory_order_acquire);
          for (size_t i = 0; i < nFrames; i++) {
            if ((w - r) >=
                (unsigned long long) macAudioRingCapacityFrames) {
              r = macAudioReadFrame.load(std::memory_order_acquire);
              continue;
            }
            size_t p = size_t(w % macAudioRingCapacityFrames) << 1;
            macAudioRing[p] = buf[(i << 1) + 0];
            macAudioRing[p + 1] = buf[(i << 1) + 1];
            w++;
          }
          macAudioWriteFrame.store(w, std::memory_order_release);
        }
        else
#endif
        for (size_t i = 0; i < nFrames; i++) {
          Buffer& buf_ = buffers[writeBufIndex];
          buf_.audioData[buf_.writePos++] = buf[(i << 1) + 0];
          buf_.audioData[buf_.writePos++] = buf[(i << 1) + 1];
          if (buf_.writePos >= buf_.audioData.size()) {
            buf_.writePos = 0;
            buf_.paLock.notify();
            if (buf_.epLock.wait(1000)) {
              if (++writeBufIndex >= buffers.size())
                writeBufIndex = 0;
            }
          }
        }
      }
    }
    else {
      // if there is no audio device, only synchronize to real time
      double  curTime = timer_.getRealTime();
      double  waitTime = nextTime - curTime;
      if (waitTime > 0.0)
        Timer::wait(waitTime);
      if (waitTime < double(-totalLatency)) {
        timer_.reset();
        nextTime = 0.0;
      }
      nextTime += (double(long(nFrames)) / sampleRate);
    }
    // call base class to write sound file
    AudioOutput::sendAudioData(buf, nFrames);
  }

  void AudioOutput_PortAudio::closeDevice()
  {
    paLockTimeout = 0U;
    if (paStream) {
      (void) isPortAudioError("calling Pa_StopStream()",
                              Pa_StopStream(paStream));
      closeDeviceLock.wait();
      (void) isPortAudioError("calling Pa_CloseStream()",
                              Pa_CloseStream(paStream));
      closeDeviceLock.notify();
      paStream = (PaStream *) 0;
    }
    disableRingBuffer = false;
    usingBlockingInterface = false;
    writeBufIndex = 0;
    readBufIndex = 0;
    buffers.clear();
#ifdef __APPLE__
    usingMacAudioRing = false;
    macAudioRing.clear();
    macAudioRingCapacityFrames = 0;
    macAudioRingStartThresholdFrames = 0;
    macAudioWriteFrame.store(0ULL, std::memory_order_relaxed);
    macAudioReadFrame.store(0ULL, std::memory_order_relaxed);
    macAudioRingStarted.store(false, std::memory_order_relaxed);
#endif
    // call base class to reset internal state
    AudioOutput::closeDevice();
  }

  std::vector< std::string > AudioOutput_PortAudio::getDeviceList()
  {
    std::vector< std::string >  tmp;
    if (paInitialized) {
#ifndef USING_OLD_PORTAUDIO_API
      PaDeviceIndex devCnt = Pa_GetDeviceCount();
      PaDeviceIndex i;
#else
      PaDeviceID    devCnt = PaDeviceID(Pa_CountDevices());
      PaDeviceID    i;
#endif
      for (i = 0; i < devCnt; i++) {
        const PaDeviceInfo  *devInfo = Pa_GetDeviceInfo(i);
        if (devInfo) {
          if (devInfo->maxOutputChannels >= 2) {
            std::string s("");
            if (devInfo->name)
              s = devInfo->name;
#ifndef USING_OLD_PORTAUDIO_API
            const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(devInfo->hostApi);
            if (apiInfo) {
              s += " (";
              if (apiInfo->name)
                s += apiInfo->name;
              s += ")";
            }
#endif
            tmp.push_back(s);
          }
        }
      }
    }
    return tmp;
  }

#ifndef USING_OLD_PORTAUDIO_API
  int AudioOutput_PortAudio::portAudioCallback(const void *input, void *output,
                                               unsigned long frameCount,
                                               const PaStreamCallbackTimeInfo
                                                   *timeInfo,
                                               PaStreamCallbackFlags
                                                   statusFlags,
                                               void *userData)
#else
  int AudioOutput_PortAudio::portAudioCallback(void *input, void *output,
                                               unsigned long frameCount,
                                               PaTimestamp outTime,
                                               void *userData)
#endif
  {
    AudioOutput_PortAudio *p =
        reinterpret_cast<AudioOutput_PortAudio *>(userData);
    if (!p->closeDeviceLock.wait(0)) {
#ifndef USING_OLD_PORTAUDIO_API
      return int(paAbort);
#else
      return 1;
#endif
    }
    int16_t *buf = reinterpret_cast<int16_t *>(output);
    size_t  i = 0, nFrames = frameCount;
    (void) input;
#ifndef USING_OLD_PORTAUDIO_API
    (void) timeInfo;
    (void) statusFlags;
#else
    (void) outTime;
#endif
#ifdef __APPLE__
    if (p->usingMacAudioRing) {
      size_t copiedFrames = 0;
      unsigned long long r =
          p->macAudioReadFrame.load(std::memory_order_relaxed);
      unsigned long long w =
          p->macAudioWriteFrame.load(std::memory_order_acquire);
      unsigned long long available = w - r;
      bool started = p->macAudioRingStarted.load(std::memory_order_relaxed);
      if (!started &&
          available >=
              (unsigned long long) p->macAudioRingStartThresholdFrames) {
        started = true;
        p->macAudioRingStarted.store(true, std::memory_order_relaxed);
      }
      if (started) {
        copiedFrames = size_t(available < (unsigned long long) frameCount ?
                              available : (unsigned long long) frameCount);
        for (size_t j = 0; j < copiedFrames; j++) {
          size_t q = size_t((r + j) % p->macAudioRingCapacityFrames) << 1;
          buf[(j << 1) + 0] = p->macAudioRing[q];
          buf[(j << 1) + 1] = p->macAudioRing[q + 1];
        }
        if (copiedFrames)
          p->macAudioReadFrame.store(r + copiedFrames,
                                     std::memory_order_release);
        if (copiedFrames < frameCount)
          p->macAudioRingStarted.store(false, std::memory_order_relaxed);
      }
      for (size_t j = copiedFrames; j < frameCount; j++) {
        buf[(j << 1) + 0] = 0;
        buf[(j << 1) + 1] = 0;
      }
      p->closeDeviceLock.notify();
#ifndef USING_OLD_PORTAUDIO_API
      return int(paContinue);
#else
      return 0;
#endif
    }
#endif
    if (nFrames > (p->buffers[p->readBufIndex].audioData.size() >> 1))
      nFrames = p->buffers[p->readBufIndex].audioData.size() >> 1;
    nFrames <<= 1;
    if (p->buffers[p->readBufIndex].paLock.wait(p->paLockTimeout)) {
      for ( ; i < nFrames; i++)
        buf[i] = p->buffers[p->readBufIndex].audioData[i];
    }
    p->buffers[p->readBufIndex].epLock.notify();
    if (++(p->readBufIndex) >= p->buffers.size())
      p->readBufIndex = 0;
    for ( ; i < (frameCount << 1); i++)
      buf[i] = 0;
    p->closeDeviceLock.notify();
#ifndef USING_OLD_PORTAUDIO_API
    return int(paContinue);
#else
    return 0;
#endif
  }

  void AudioOutput_PortAudio::openDevice()
  {
#ifdef __APPLE__
    usingMacAudioRing = false;
    macAudioRing.clear();
    macAudioRingCapacityFrames = 0;
    macAudioRingStartThresholdFrames = 0;
    macAudioWriteFrame.store(0ULL, std::memory_order_relaxed);
    macAudioReadFrame.store(0ULL, std::memory_order_relaxed);
    macAudioRingStarted.store(false, std::memory_order_relaxed);
#endif
    writeBufIndex = 0;
    readBufIndex = 0;
    paStream = (PaStream *) 0;
    // find audio device
#ifndef USING_OLD_PORTAUDIO_API
    int     devCnt = int(Pa_GetDeviceCount());
#else
    int     devCnt = int(Pa_CountDevices());
#endif
    if (devCnt < 1)
      throw Exception("no audio device is available");
    int     devIndex;
    int     devNum = deviceNumber;
    for (devIndex = 0; devIndex < devCnt; devIndex++) {
      const PaDeviceInfo  *devInfo;
#ifndef USING_OLD_PORTAUDIO_API
      devInfo = Pa_GetDeviceInfo(PaDeviceIndex(devIndex));
#else
      devInfo = Pa_GetDeviceInfo(PaDeviceID(devIndex));
#endif
      if (!devInfo)
        throw Exception("error querying audio device information");
      if (devInfo->maxOutputChannels >= 2) {
        if (--devNum == -1) {
          devNum = devIndex;
          break;
        }
      }
    }
    if (devIndex >= devCnt)
      throw Exception("device number is out of range");
    usingBlockingInterface = false;
    bool    usingCoreAudioInterface = false;
    int     nPeriodsHW_ = nPeriodsHW;
#ifndef USING_OLD_PORTAUDIO_API
    int     nPeriodsSW_ = nPeriodsSW;
    const PaDeviceInfo  *devInfo = Pa_GetDeviceInfo(PaDeviceIndex(devIndex));
    if (devInfo) {
      const PaHostApiInfo *hostApiInfo = Pa_GetHostApiInfo(devInfo->hostApi);
      if (hostApiInfo) {
        if ((1UL << uint8_t(hostApiInfo->type))
            & ((1UL << uint8_t(paMME)) | (1UL << uint8_t(paOSS))
               | (1UL << uint8_t(paALSA)) | (1UL << uint8_t(paWASAPI)))) {
          nPeriodsHW_++;
          nPeriodsSW_ = 1;
          usingBlockingInterface = true;
        }
#  ifdef WIN32
        else if (hostApiInfo->type == paDirectSound) {
          nPeriodsSW_ = 1;
        }
        else {
          // ASIO or WDM-KS: force double buffering
          nPeriodsHW_ = 2;
          nPeriodsSW_ = (nPeriodsSW_ >= 2 ? nPeriodsSW_ : 2);
        }
#  elif defined(__APPLE__)
        else if (hostApiInfo->type == paCoreAudio) {
          usingCoreAudioInterface = true;
          usingMacAudioRing = true;
          // CoreAudio is a callback API; do not split the requested latency
          // by the historical generic hardware-buffer count.
          nPeriodsHW_ = 2;
          nPeriodsSW_ = (nPeriodsSW_ >= 2 ? nPeriodsSW_ : 2);
        }
        else {
          nPeriodsSW_ = (nPeriodsSW_ >= 2 ? nPeriodsSW_ : 2);
        }
#  else
        else {
          nPeriodsSW_ = (nPeriodsSW_ >= 2 ? nPeriodsSW_ : 2);
        }
#  endif
      }
    }
#else
    int     nPeriodsSW_ = 1;
#endif
    // calculate buffer size
    disableRingBuffer = (nPeriodsSW_ < 2);
    int     periodSize =
        int(totalLatency * (usingBlockingInterface ? 1.4142f : 0.7071f)
            * sampleRate + 0.5f)
        / (nPeriodsHW_ + nPeriodsSW_ - 2);
    for (int i = 16; i < 16384; i <<= 1) {
      if (i >= periodSize) {
        periodSize = i;
        break;
      }
    }
    if (periodSize > 16384)
      periodSize = 16384;
#ifdef __APPLE__
    if (usingCoreAudioInterface && periodSize < 128)
      periodSize = 128;
#endif
    latencyFramesHW = long(nPeriodsHW_ - 1) * long(periodSize);
    if (disableRingBuffer) {
      paLockTimeout = (unsigned int) (double(latencyFramesHW) * 1000.0
                                      / double(sampleRate) + 0.999);
    }
    else {
      paLockTimeout = 0U;
    }
    // initialize buffers
    buffers.resize(size_t(nPeriodsSW_));
    for (int i = 0; i < nPeriodsSW_; i++) {
      buffers[i].audioData.resize(size_t(periodSize) << 1);
      for (int j = 0; j < (periodSize << 1); j++)
        buffers[i].audioData[j] = 0;
    }
#ifdef __APPLE__
    if (usingMacAudioRing) {
      // Minimum 1024 frames capacity and 512 frames prefill with the 128-frame
      // CoreAudio floor: enough headroom for short background scheduler jitter.
      size_t ringPeriods = size_t(nPeriodsSW_ >= 8 ? nPeriodsSW_ : 8);
      macAudioRingCapacityFrames = size_t(periodSize) * ringPeriods;
      macAudioRingStartThresholdFrames = size_t(periodSize) * 4U;
      macAudioRing.resize(macAudioRingCapacityFrames << 1, int16_t(0));
    }
#endif
    // open audio stream
#ifndef USING_OLD_PORTAUDIO_API
    PaStreamParameters  streamParams;
    std::memset(&streamParams, 0, sizeof(PaStreamParameters));
    streamParams.device = PaDeviceIndex(devNum);
    streamParams.channelCount = 2;
    streamParams.sampleFormat = paInt16;
    streamParams.suggestedLatency =
        PaTime(double(periodSize) * (nPeriodsHW_ - 1) / sampleRate);
    streamParams.hostApiSpecificStreamInfo = (void *) 0;
    PaError err = paNoError;
    if (usingBlockingInterface)
      err = Pa_OpenStream(&paStream, (PaStreamParameters *) 0, &streamParams,
                          sampleRate, unsigned(periodSize),
                          paNoFlag, (PaStreamCallback *) 0, (void *) this);
    else
      err = Pa_OpenStream(&paStream, (PaStreamParameters *) 0, &streamParams,
                          sampleRate, unsigned(periodSize),
                          paNoFlag, &portAudioCallback, (void *) this);
    if (err == paNoError) {
      const PaStreamInfo  *streamInfo = Pa_GetStreamInfo(paStream);
      if (streamInfo) {
        // find out the actual buffer size
        long    tmp = long(double(streamInfo->outputLatency)
                           * streamInfo->sampleRate + 0.5);
        if (tmp >= 16L && tmp <= 262144L)
          latencyFramesHW = tmp;
      }
    }
#else
    PaError err =
        Pa_OpenStream(&paStream,
                      paNoDevice, 0, paInt16, (void *) 0,
                      PaDeviceID(devNum), 2, paInt16, (void *) 0,
                      sampleRate, unsigned(periodSize), unsigned(nPeriodsHW_),
                      paNoFlag, &portAudioCallback, (void *) this);
#endif
    if (isPortAudioError("calling Pa_OpenStream()", err)) {
      paStream = (PaStream *) 0;
      throw Exception("error opening audio device");
    }
    Pa_StartStream(paStream);
#ifdef WIN32
    // The audio device can be reconfigured while the VM is paused. New
    // streams must not resume queued sound until the guest itself resumes.
    if (emulationPaused)
      (void) Pa_AbortStream(paStream);
#endif
  }

  // --------------------------------------------------------------------------

#ifdef ENABLE_MIDI_PORT

#define MIDI_BUFFER_SIZE        128

  void MIDIPort::portTimeInCallback(PtTimestamp timestamp, void *userData)
  {
    PmEvent   evtBuf[MIDI_BUFFER_SIZE];
    MIDIPort& midiPort = *(reinterpret_cast< MIDIPort * >(userData));
    VirtualMachine& vm = midiPort.vm;
    (void) timestamp;
    if (EP128EMU_EXPECT(bool(midiPort.portMidiInStream))) {
      while (Pm_Poll(midiPort.portMidiInStream) == TRUE) {
        int     n = Pm_Read(midiPort.portMidiInStream,
                            &(evtBuf[0]), MIDI_BUFFER_SIZE);
        for (int i = 0; i < n; i++)
          vm.midiInReceiveEvent(int32_t(evtBuf[i].message));
      }
    }
  }

  void MIDIPort::portTimeOutCallback(PtTimestamp timestamp, void *userData)
  {
    PmEvent   evtBuf[MIDI_BUFFER_SIZE];
    MIDIPort& midiPort = *(reinterpret_cast< MIDIPort * >(userData));
    VirtualMachine& vm = midiPort.vm;
    (void) timestamp;
    if (EP128EMU_EXPECT(bool(midiPort.portMidiOutStream))) {
      int     bufPos = 0;
      int32_t evt;
      while ((evt = vm.midiOutSendEvent()) >= 0) {
        evtBuf[bufPos].message = PmMessage(evt);
        evtBuf[bufPos].timestamp = PmTimestamp(0);
        if (++bufPos >= MIDI_BUFFER_SIZE) {
          bufPos = 0;
          (void) Pm_Write(midiPort.portMidiOutStream,
                          &(evtBuf[0]), MIDI_BUFFER_SIZE);
        }
      }
      if (EP128EMU_EXPECT(bool(bufPos)))
        (void) Pm_Write(midiPort.portMidiOutStream, &(evtBuf[0]), bufPos);
    }
  }

  MIDIPort::MIDIPort(VirtualMachine& vm_)
    : vm(vm_),
      portMidiInStream((PortMidiStream *) 0),
      portMidiOutStream((PortMidiStream *) 0),
      portMidiDevNum(-1)
  {
    if (Pm_Initialize() != pmNoError)
      throw Exception("error initializing PortMidi");
  }

  MIDIPort::~MIDIPort()
  {
    try {
      openDevice(-1);
    }
    catch (...) {
    }
    (void) Pm_Terminate();
  }

  std::vector< std::string > MIDIPort::getDeviceList()
  {
    std::vector< std::string >  devList;
    static const char *midiDevInfoStrings[8] = {
      "",       " (I)",   " (O)",   " (IO)",
      " (",     " (I, ",  " (O, ",  " (IO, "
    };
    int     n = Pm_CountDevices();
    for (int i = 0; i < n; i++) {
      std::string devName("unknown");
      const PmDeviceInfo  *devInfo = Pm_GetDeviceInfo(PmDeviceID(i));
      if (devInfo && (devInfo->interf || devInfo->name)) {
        if (devInfo->name)
          devName = devInfo->name;
        devName += midiDevInfoStrings[int(bool(devInfo->input))
                                      | (int(bool(devInfo->output)) << 1)
                                      | (int(bool(devInfo->interf)) << 2)];
        if (devInfo->interf) {
          devName += devInfo->interf;
          devName += ')';
        }
      }
      devList.push_back(devName);
    }
    return devList;
  }

  void MIDIPort::openDevice(int n)
  {
    if (n < 0)
      n = -1;
    if (n == portMidiDevNum)
      return;
    portMidiDevNum = -1;
    if (portMidiInStream || portMidiOutStream) {
      PortMidiStream  *p = portMidiInStream;
      if (!p)
        p = portMidiOutStream;
      portMidiInStream = (PortMidiStream *) 0;
      portMidiOutStream = (PortMidiStream *) 0;
      (void) Pt_Stop();
      (void) Pm_Close(p);
    }
    vm.midiSetDeviceType(0);
    if (n >= 0) {
      const PmDeviceInfo  *devInfo = Pm_GetDeviceInfo(PmDeviceID(n));
      if (!devInfo || !(devInfo->input | devInfo->output))
        throw Exception("invalid PortMidi device number");
      bool    isInput = bool(devInfo->input);
      if (Pt_Start(1, (isInput ? &portTimeInCallback : &portTimeOutCallback),
                   (void *) this) != ptNoError) {
        throw Exception("error starting PortTime");
      }
      PortMidiStream  *p = (PortMidiStream *) 0;
      if (isInput) {
        if (Pm_OpenInput(&p, PmDeviceID(n), (void *) 0, MIDI_BUFFER_SIZE,
                         PmTimeProcPtr(0), (void *) 0) != pmNoError) {
          (void) Pt_Stop();
          throw Exception("error opening PortMidi input device");
        }
        // ignore system messages with the exception of
        // 0xF8 (Clock), 0xFA (Start), 0xFB (Continue) and 0xFC (Stop)
        if (Pm_SetFilter(p, 0xE2FF) != pmNoError) {
          (void) Pt_Stop();
          (void) Pm_Close(p);
          throw Exception("error setting PortMidi input filter");
        }
        portMidiDevNum = n;
        portMidiInStream = p;
        vm.midiSetDeviceType(1);
      }
      else {
        if (Pm_OpenOutput(&p, PmDeviceID(n), (void *) 0, MIDI_BUFFER_SIZE,
                          PmTimeProcPtr(0), (void *) 0, 0) != pmNoError) {
          (void) Pt_Stop();
          throw Exception("error opening PortMidi output device");
        }
        portMidiDevNum = n;
        portMidiOutStream = p;
        vm.midiSetDeviceType(2);
      }
    }
  }

#endif  // ENABLE_MIDI_PORT

}       // namespace Ep128Emu

