
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2009 Istvan Varga <istvanv@users.sourceforge.net>
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
#include "cfg_db.hpp"
#include "joystick.hpp"

#include <cmath>
#ifdef __APPLE__
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/hid/IOHIDManager.h>
#  include <IOKit/hid/IOHIDKeys.h>
#elif defined(HAVE_SDL_H)
#  include <SDL/SDL.h>
#endif

namespace Ep128Emu {

  static const int joystickHatUp = 0x01;
  static const int joystickHatRight = 0x02;
  static const int joystickHatDown = 0x04;
  static const int joystickHatLeft = 0x08;

  JoystickInput::JoystickConfiguration::JoystickConfiguration()
    : enableJoystick(true),
      enablePWM(false),
      enableAutoFire(false),
      axisThreshold(0.5),
      pwmFrequency(17.5),
      autoFireFrequency(8.0),
      autoFirePulseWidth(0.5)
  {
  }

  void JoystickInput::JoystickConfiguration::registerConfigurationVariables(
      ConfigurationDB& config_)
  {
    config_.createKey("joystick.enableJoystick", enableJoystick);
    config_.createKey("joystick.enablePWM", enablePWM);
    config_.createKey("joystick.enableAutoFire", enableAutoFire);
    config_.createKey("joystick.axisThreshold", axisThreshold);
    config_["joystick.axisThreshold"].setRange(0.01, 0.99);
    config_.createKey("joystick.pwmFrequency", pwmFrequency);
    config_["joystick.pwmFrequency"].setRange(1.0, 100.0);
    config_.createKey("joystick.autoFireFrequency", autoFireFrequency);
    config_["joystick.autoFireFrequency"].setRange(0.5, 50.0);
    config_.createKey("joystick.autoFirePulseWidth", autoFirePulseWidth);
    config_["joystick.autoFirePulseWidth"].setRange(0.01, 0.99);
  }

  JoystickInput::JoystickInput(bool sdlInitFlag)
    : eventCnt(0),
      eventIndex(0),
      axisCnt(0),
      buttonCnt(0),
      hatCnt(0),
      updateTimer(),
      pwmTimer(),
      autoFireTimer(),
      mutex_(),
      nativeManager((void *) 0),
      haveJoystick(false),
      lockFlag(false),
      sdlInitialized(false),
      config()
  {
    sdlDevices[0] = (void *) 0;
    sdlDevices[1] = (void *) 0;
#ifdef __APPLE__
    (void) sdlInitFlag;

    IOHIDManagerRef manager =
        IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager)
      return;

    // macOS 14 can expose older USB gamepads through IOHID while SDL 1.2/2/3
    // enumerate zero devices. Match all HID devices here and keep only Generic
    // Desktop joystick/gamepad/multi-axis devices.
    IOHIDManagerSetDeviceMatching(manager, (CFDictionaryRef) 0);

    // Do not open the IOHIDManager itself. On current macOS versions that
    // operation is guarded by Input Monitoring TCC and can return
    // kIOReturnNotPermitted for an otherwise fully readable game controller.
    // We only need the manager for enumeration; direct IOHIDDeviceOpen() +
    // IOHIDDeviceGetValue() polling works without that privacy permission.
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) {
      CFRelease(manager);
      return;
    }

    CFIndex deviceCount = CFSetGetCount(devices);
    const void **deviceList =
        new const void *[size_t(deviceCount > 0 ? deviceCount : 1)];
    CFSetGetValues(devices, deviceList);
    int nDevices = 0;
    for (CFIndex i = 0; i < deviceCount && nDevices < 2; i++) {
      IOHIDDeviceRef d = (IOHIDDeviceRef) deviceList[i];
      CFTypeRef pageObj =
          IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsagePageKey));
      CFTypeRef usageObj =
          IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsageKey));
      int page = -1;
      int usage = -1;
      if (pageObj && CFGetTypeID(pageObj) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef) pageObj, kCFNumberIntType, &page);
      if (usageObj && CFGetTypeID(usageObj) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef) usageObj, kCFNumberIntType, &usage);
      if (page != 0x01 || !(usage == 0x04 || usage == 0x05 || usage == 0x08))
        continue;
      if (IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone) != kIOReturnSuccess)
        continue;
      CFRetain(d);
      sdlDevices[nDevices++] = (void *) d;
    }
    delete[] deviceList;
    CFRelease(devices);

    if (!nDevices) {
      CFRelease(manager);
      return;
    }
    nativeManager = (void *) manager;

    for (int j = 0; j < nDevices; j++) {
      IOHIDDeviceRef d = (IOHIDDeviceRef) sdlDevices[j];
      CFArrayRef elements =
          IOHIDDeviceCopyMatchingElements(d, (CFDictionaryRef) 0,
                                          kIOHIDOptionsTypeNone);
      if (!elements)
        continue;
      CFIndex nElements = CFArrayGetCount(elements);
      for (CFIndex k = 0; k < nElements; k++) {
        IOHIDElementRef e =
            (IOHIDElementRef) CFArrayGetValueAtIndex(elements, k);
        uint32_t page = IOHIDElementGetUsagePage(e);
        uint32_t usage = IOHIDElementGetUsage(e);
        IOHIDElementType type = IOHIDElementGetType(e);

        if (page == 0x01 && usage == 0x39 &&
            (type == kIOHIDElementTypeInput_Misc ||
             type == kIOHIDElementTypeInput_Axis) && hatCnt < 2) {
          povHats[hatCnt].devNum = j;
          povHats[hatCnt].hatNum = int(usage);
          povHats[hatCnt].nativeElement = (void *) e;
          CFRetain(e);
          povHats[hatCnt].prvState = 0;
          hatCnt++;
          continue;
        }

        if (page == 0x09 && type == kIOHIDElementTypeInput_Button &&
            buttonCnt < 16) {
          buttons[buttonCnt].devNum = j;
          buttons[buttonCnt].buttonNum = int(usage);
          buttons[buttonCnt].nativeElement = (void *) e;
          CFRetain(e);
          buttons[buttonCnt].prvInputState = false;
          buttons[buttonCnt].prvOutputState = false;
          buttonCnt++;
          continue;
        }

        bool isAxisUsage =
            (page == 0x01 &&
             (usage == 0x30 || usage == 0x31 || usage == 0x32 ||
              usage == 0x33 || usage == 0x34 || usage == 0x35 ||
              usage == 0x36 || usage == 0x37));
        if (isAxisUsage &&
            (type == kIOHIDElementTypeInput_Misc ||
             type == kIOHIDElementTypeInput_Axis) && axisCnt < 8) {
          CFIndex mn = IOHIDElementGetLogicalMin(e);
          CFIndex mx = IOHIDElementGetLogicalMax(e);
          if (mx <= mn)
            continue;
          axes[axisCnt].devNum = j;
          axes[axisCnt].axisNum = int(usage);
          axes[axisCnt].nativeElement = (void *) e;
          CFRetain(e);
          axes[axisCnt].prvInputState = 0;
          axes[axisCnt].prvOutputState = 0;
          axisCnt++;
        }
      }
      CFRelease(elements);
    }
    haveJoystick = (axisCnt > 0 || buttonCnt > 0 || hatCnt > 0);

    // Seed previous states from current hardware values so a held control at
    // startup does not synthesize a spurious edge.
    for (int i = 0; i < axisCnt; i++)
      axes[i].prvInputState = readAxisValue(i);
    for (int i = 0; i < buttonCnt; i++) {
      buttons[i].prvInputState = readButtonValue(i);
      buttons[i].prvOutputState = buttons[i].prvInputState;
    }
    for (int i = 0; i < hatCnt; i++)
      povHats[i].prvState = readHatValue(i);

#elif defined(HAVE_SDL_H)
    if (sdlInitFlag) {
      if (SDL_Init(SDL_INIT_JOYSTICK) != 0)
        return;
      sdlInitialized = true;
    }
    int   nDevices = SDL_NumJoysticks();
    int   j = 0;
    int   prvDevNum = -1;
    for (int i = 0; i < nDevices && j < 2; i++) {
      SDL_Joystick  *joy_ = SDL_JoystickOpen(i);
      if (!joy_)
        continue;
      int   axisCnt_ = SDL_JoystickNumAxes(joy_);
      int   buttonCnt_ = SDL_JoystickNumButtons(joy_);
      int   hatCnt_ = SDL_JoystickNumHats(joy_);
      if (!(axisCnt_ >= 4 && buttonCnt_ >= 4 && hatCnt_ >= 1)) {
        SDL_JoystickClose(joy_);
        continue;
      }
      sdlDevices[j++] = joy_;
      prvDevNum = i;
    }
    for (int i = 0; i < nDevices && j < 2; i++) {
      if (i == prvDevNum)
        continue;
      SDL_Joystick  *joy_ = SDL_JoystickOpen(i);
      if (!joy_)
        continue;
      int   axisCnt_ = SDL_JoystickNumAxes(joy_);
      int   buttonCnt_ = SDL_JoystickNumButtons(joy_);
      int   hatCnt_ = SDL_JoystickNumHats(joy_);
      if (!(axisCnt_ >= 2 && buttonCnt_ >= 1 && hatCnt_ >= 1)) {
        SDL_JoystickClose(joy_);
        continue;
      }
      sdlDevices[j++] = joy_;
      prvDevNum = i;
    }
    for (int i = 0; i < nDevices && j < 2; i++) {
      if (i == prvDevNum)
        continue;
      SDL_Joystick  *joy_ = SDL_JoystickOpen(i);
      if (!joy_)
        continue;
      sdlDevices[j++] = joy_;
    }
    if (!j)
      return;
    nDevices = j;
    for (j = 0; j < nDevices; j++) {
      SDL_Joystick  *joy_ = reinterpret_cast<SDL_Joystick *>(sdlDevices[j]);
      int   axisCnt_ = SDL_JoystickNumAxes(joy_);
      int   buttonCnt_ = SDL_JoystickNumButtons(joy_);
      int   hatCnt_ = SDL_JoystickNumHats(joy_);
      if (j == 0 && nDevices > 1) {
        axisCnt_ = (axisCnt_ < 4 ? axisCnt_ : 4);
        buttonCnt_ = (buttonCnt_ < 8 ? buttonCnt_ : 8);
        hatCnt_ = (hatCnt_ < 1 ? hatCnt_ : 1);
      }
      for (int i = 0; i < axisCnt_ && axisCnt < 8; i++) {
        axes[axisCnt].devNum = j;
        axes[axisCnt].axisNum = i;
        axes[axisCnt].nativeElement = (void *) 0;
        axes[axisCnt].prvInputState = 0;
        axes[axisCnt].prvOutputState = 0;
        axisCnt++;
      }
      for (int i = 0; i < buttonCnt_ && buttonCnt < 16; i++) {
        buttons[buttonCnt].devNum = j;
        buttons[buttonCnt].buttonNum = i;
        buttons[buttonCnt].nativeElement = (void *) 0;
        buttons[buttonCnt].prvInputState = false;
        buttons[buttonCnt].prvOutputState = false;
        buttonCnt++;
      }
      for (int i = 0; i < hatCnt_ && hatCnt < 2; i++) {
        povHats[hatCnt].devNum = j;
        povHats[hatCnt].hatNum = i;
        povHats[hatCnt].nativeElement = (void *) 0;
        povHats[hatCnt].prvState = 0;
        hatCnt++;
      }
    }
    haveJoystick = (axisCnt > 0 || buttonCnt > 0 || hatCnt > 0);
#else
    (void) sdlInitFlag;
#endif
  }

  void JoystickInput::closeMacOSDevices()
  {
#ifdef __APPLE__
    for (int i = 0; i < axisCnt; i++) {
      if (axes[i].nativeElement)
        CFRelease((CFTypeRef) axes[i].nativeElement);
      axes[i].nativeElement = (void *) 0;
    }
    for (int i = 0; i < buttonCnt; i++) {
      if (buttons[i].nativeElement)
        CFRelease((CFTypeRef) buttons[i].nativeElement);
      buttons[i].nativeElement = (void *) 0;
    }
    for (int i = 0; i < hatCnt; i++) {
      if (povHats[i].nativeElement)
        CFRelease((CFTypeRef) povHats[i].nativeElement);
      povHats[i].nativeElement = (void *) 0;
    }
    for (int i = 0; i < 2; i++) {
      if (sdlDevices[i]) {
        IOHIDDeviceRef d = (IOHIDDeviceRef) sdlDevices[i];
        IOHIDDeviceClose(d, kIOHIDOptionsTypeNone);
        CFRelease(d);
        sdlDevices[i] = (void *) 0;
      }
    }
    if (nativeManager) {
      IOHIDManagerRef manager = (IOHIDManagerRef) nativeManager;
      CFRelease(manager);
      nativeManager = (void *) 0;
    }
    axisCnt = 0;
    buttonCnt = 0;
    hatCnt = 0;
    haveJoystick = false;
    eventCnt = 0;
    eventIndex = 0;
#endif
  }

  JoystickInput::~JoystickInput()
  {
#ifdef __APPLE__
    closeMacOSDevices();
#elif defined(HAVE_SDL_H)
    if (sdlDevices[0])
      SDL_JoystickClose(reinterpret_cast<SDL_Joystick *>(sdlDevices[0]));
    if (sdlDevices[1])
      SDL_JoystickClose(reinterpret_cast<SDL_Joystick *>(sdlDevices[1]));
    if (sdlInitialized)
      SDL_Quit();
#endif
  }

  int JoystickInput::readAxisValue(int n) const
  {
#ifdef __APPLE__
    IOHIDElementRef e = (IOHIDElementRef) axes[n].nativeElement;
    IOHIDDeviceRef d = (IOHIDDeviceRef) sdlDevices[axes[n].devNum];
    if (!e || !d)
      return axes[n].prvInputState;
    IOHIDValueRef v = (IOHIDValueRef) 0;
    if (IOHIDDeviceGetValue(d, e, &v) != kIOReturnSuccess || !v)
      return axes[n].prvInputState;
    long long raw = IOHIDValueGetIntegerValue(v);
    long long mn = (long long) IOHIDElementGetLogicalMin(e);
    long long mx = (long long) IOHIDElementGetLogicalMax(e);
    if (mx <= mn)
      return 0;
    long long out = ((raw - mn) * 65535LL) / (mx - mn) - 32768LL;
    if (out < -32768LL)
      out = -32768LL;
    else if (out > 32767LL)
      out = 32767LL;
    return int(out);
#elif defined(HAVE_SDL_H)
    return int(SDL_JoystickGetAxis(
        reinterpret_cast<SDL_Joystick *>(sdlDevices[axes[n].devNum]),
        axes[n].axisNum));
#else
    (void) n;
    return 0;
#endif
  }

  bool JoystickInput::readButtonValue(int n) const
  {
#ifdef __APPLE__
    IOHIDElementRef e = (IOHIDElementRef) buttons[n].nativeElement;
    IOHIDDeviceRef d = (IOHIDDeviceRef) sdlDevices[buttons[n].devNum];
    if (!e || !d)
      return buttons[n].prvInputState;
    IOHIDValueRef v = (IOHIDValueRef) 0;
    if (IOHIDDeviceGetValue(d, e, &v) != kIOReturnSuccess || !v)
      return buttons[n].prvInputState;
    return (IOHIDValueGetIntegerValue(v) != 0);
#elif defined(HAVE_SDL_H)
    return bool(SDL_JoystickGetButton(
        reinterpret_cast<SDL_Joystick *>(sdlDevices[buttons[n].devNum]),
        buttons[n].buttonNum));
#else
    (void) n;
    return false;
#endif
  }

  int JoystickInput::readHatValue(int n) const
  {
#ifdef __APPLE__
    IOHIDElementRef e = (IOHIDElementRef) povHats[n].nativeElement;
    IOHIDDeviceRef d = (IOHIDDeviceRef) sdlDevices[povHats[n].devNum];
    if (!e || !d)
      return povHats[n].prvState;
    IOHIDValueRef v = (IOHIDValueRef) 0;
    if (IOHIDDeviceGetValue(d, e, &v) != kIOReturnSuccess || !v)
      return povHats[n].prvState;
    long long raw = IOHIDValueGetIntegerValue(v);
    long long mn = (long long) IOHIDElementGetLogicalMin(e);
    long long mx = (long long) IOHIDElementGetLogicalMax(e);
    if (raw < mn || raw > mx)
      return 0;
    int p = int(raw - mn);
    switch (p) {
    case 0: return 0x01;             // up
    case 1: return 0x01 | 0x02;      // up + right
    case 2: return 0x02;             // right
    case 3: return 0x02 | 0x04;      // right + down
    case 4: return 0x04;             // down
    case 5: return 0x04 | 0x08;      // down + left
    case 6: return 0x08;             // left
    case 7: return 0x08 | 0x01;      // left + up
    default: return 0;
    }
#elif defined(HAVE_SDL_H)
    return int(SDL_JoystickGetHat(
        reinterpret_cast<SDL_Joystick *>(sdlDevices[povHats[n].devNum]),
        povHats[n].hatNum));
#else
    (void) n;
    return 0;
#endif
  }

  int JoystickInput::getEvent(bool ignoreConfig, bool checkLock)
  {
#if defined(__APPLE__) || defined(HAVE_SDL_H)
    if (!haveJoystick)
      return 0;
    mutex_.lock();
    if ((!ignoreConfig && !config.enableJoystick) || (checkLock && lockFlag)) {
      mutex_.unlock();
      return 0;
    }
    if (eventIndex < eventCnt) {
      // consume any buffered events first
      int     retval = events[eventIndex];
      if (++eventIndex == eventCnt) {
        eventCnt = 0;
        eventIndex = 0;
      }
      mutex_.unlock();
      return retval;
    }
    {
      double  t = updateTimer.getRealTime();
      if (t < 0.005) {
        mutex_.unlock();
        return 0;
      }
      // poll joystick input at 5 ms intervals
      updateTimer.reset((t - 0.005) * 0.5);
    }
#ifndef __APPLE__
    SDL_JoystickUpdate();
#endif
    eventCnt = 0;
    eventIndex = 0;
    if (!ignoreConfig) {
      int     thresholdInt = int(config.axisThreshold * 32767.0 + 0.5);
      bool    autoFireState = false;
      double  pwmPhase = 0.0;
      if (config.enablePWM) {
        double  tt = 1.0 / config.pwmFrequency;
        double  t = pwmTimer.getRealTime();
        if (t >= tt) {
          t = std::fmod(t, tt);
          pwmTimer.reset(t);
        }
        pwmPhase = t * config.pwmFrequency;
        pwmPhase *= (1.0 - config.axisThreshold);
        pwmPhase += (config.axisThreshold * 0.5);
      }
      if (config.enableAutoFire) {
        double  tt = 1.0 / config.autoFireFrequency;
        double  t = autoFireTimer.getRealTime();
        if (t >= tt) {
          t = std::fmod(t, tt);
          autoFireTimer.reset(t);
        }
        autoFireState = (t < (tt * config.autoFirePulseWidth));
      }
      for (int i = 0; i < axisCnt; i++) {
        int     newState = readAxisValue(i);
        if (!config.enablePWM) {
          if (newState > axes[i].prvInputState) {
            if (newState >= (-thresholdInt)) {
              if (axes[i].prvOutputState < 0)
                events[eventCnt++] = -(keyCodeBase + (i << 1));
              if (newState >= thresholdInt) {
                if (axes[i].prvOutputState <= 0)
                  events[eventCnt++] = (keyCodeBase + (i << 1) + 1);
                axes[i].prvOutputState = 1;
              }
              else
                axes[i].prvOutputState = 0;
            }
          }
          else if (newState < axes[i].prvInputState) {
            if (newState < thresholdInt) {
              if (axes[i].prvOutputState > 0)
                events[eventCnt++] = -(keyCodeBase + (i << 1) + 1);
              if (newState < (-thresholdInt)) {
                if (axes[i].prvOutputState >= 0)
                  events[eventCnt++] = (keyCodeBase + (i << 1));
                axes[i].prvOutputState = -1;
              }
              else
                axes[i].prvOutputState = 0;
            }
          }
        }
        else {
          int     newOutputState = 0;
          if (newState > 0) {
            if ((double(newState) * (1.0 / 32768.0)) >= pwmPhase)
              newOutputState = 1;
          }
          else if (newState < 0) {
            if ((double(newState) * (-1.0 / 32768.0)) > pwmPhase)
              newOutputState = -1;
          }
          if (newOutputState != axes[i].prvOutputState) {
            if (axes[i].prvOutputState < 0)
              events[eventCnt++] = -(keyCodeBase + (i << 1));
            else if (axes[i].prvOutputState > 0)
              events[eventCnt++] = -(keyCodeBase + (i << 1) + 1);
            if (newOutputState < 0)
              events[eventCnt++] = (keyCodeBase + (i << 1));
            else if (newOutputState > 0)
              events[eventCnt++] = (keyCodeBase + (i << 1) + 1);
            axes[i].prvOutputState = newOutputState;
          }
        }
        axes[i].prvInputState = newState;
      }
      for (int i = 0; i < buttonCnt; i++) {
        bool    newState = readButtonValue(i);
        if (config.enableAutoFire) {
          if (newState && !buttons[i].prvInputState) {
            autoFireState = true;
            autoFireTimer.reset();
          }
          buttons[i].prvInputState = newState;
          newState = newState && autoFireState;
        }
        else
          buttons[i].prvInputState = newState;
        if (newState != buttons[i].prvOutputState) {
          if (newState)
            events[eventCnt++] = (keyCodeBase + 16 + i);
          else
            events[eventCnt++] = -(keyCodeBase + 16 + i);
          buttons[i].prvOutputState = newState;
        }
      }
    }
    else {
      // special mode with high threshold and hysteresis, no pulse width
      // modulation and auto fire; used when defining the keyboard map, as
      // stable input is required
      for (int i = 0; i < axisCnt; i++) {
        int     newState = readAxisValue(i);
        if (newState > axes[i].prvInputState) {
          if (newState >= -8192) {
            if (axes[i].prvOutputState < 0)
              events[eventCnt++] = -(keyCodeBase + (i << 1));
            if (newState >= 24576) {
              if (axes[i].prvOutputState <= 0)
                events[eventCnt++] = (keyCodeBase + (i << 1) + 1);
              axes[i].prvOutputState = 1;
            }
            else if (newState < 8192)
              axes[i].prvOutputState = 0;
          }
        }
        else if (newState < axes[i].prvInputState) {
          if (newState < 8192) {
            if (axes[i].prvOutputState > 0)
              events[eventCnt++] = -(keyCodeBase + (i << 1) + 1);
            if (newState < -24576) {
              if (axes[i].prvOutputState >= 0)
                events[eventCnt++] = (keyCodeBase + (i << 1));
              axes[i].prvOutputState = -1;
            }
            else if (newState >= -8192)
              axes[i].prvOutputState = 0;
          }
        }
        axes[i].prvInputState = newState;
      }
      for (int i = 0; i < buttonCnt; i++) {
        bool    newState = readButtonValue(i);
        if (newState != buttons[i].prvInputState) {
          if (newState)
            events[eventCnt++] = (keyCodeBase + 16 + i);
          else
            events[eventCnt++] = -(keyCodeBase + 16 + i);
          buttons[i].prvInputState = newState;
          buttons[i].prvOutputState = newState;
        }
      }
    }
    for (int i = 0; i < hatCnt; i++) {
      int     newState = readHatValue(i);
      int     changeMask = newState ^ povHats[i].prvState;
      if (changeMask) {
        if (changeMask & int(joystickHatRight)) {
          if (newState & int(joystickHatRight))
            events[eventCnt++] = (keyCodeBase + 32 + (i << 2));
          else
            events[eventCnt++] = -(keyCodeBase + 32 + (i << 2));
        }
        if (changeMask & int(joystickHatUp)) {
          if (newState & int(joystickHatUp))
            events[eventCnt++] = (keyCodeBase + 32 + (i << 2) + 1);
          else
            events[eventCnt++] = -(keyCodeBase + 32 + (i << 2) + 1);
        }
        if (changeMask & int(joystickHatLeft)) {
          if (newState & int(joystickHatLeft))
            events[eventCnt++] = (keyCodeBase + 32 + (i << 2) + 2);
          else
            events[eventCnt++] = -(keyCodeBase + 32 + (i << 2) + 2);
        }
        if (changeMask & int(joystickHatDown)) {
          if (newState & int(joystickHatDown))
            events[eventCnt++] = (keyCodeBase + 32 + (i << 2) + 3);
          else
            events[eventCnt++] = -(keyCodeBase + 32 + (i << 2) + 3);
        }
        povHats[i].prvState = newState;
      }
    }
    int     retval = 0;
    if (eventCnt) {
      // return first event from buffer
      retval = events[0];
      if (++eventIndex == eventCnt) {
        eventCnt = 0;
        eventIndex = 0;
      }
    }
    mutex_.unlock();
    return retval;
#else
    (void) ignoreConfig;
    (void) checkLock;
    return 0;
#endif  // __APPLE__ || HAVE_SDL_H
  }

  void JoystickInput::flushEvents()
  {
    mutex_.lock();
    eventCnt = 0;
    eventIndex = 0;
    mutex_.unlock();
  }

  void JoystickInput::refreshDevices()
  {
#ifdef __APPLE__
    // Build a fresh device set first, while the current one can still be used
    // by the emulation thread. Swap under the instance mutex afterwards.
    JoystickInput tmp(true);

    mutex_.lock();
    closeMacOSDevices();

    axisCnt = tmp.axisCnt;
    buttonCnt = tmp.buttonCnt;
    hatCnt = tmp.hatCnt;
    for (int i = 0; i < axisCnt; i++)
      axes[i] = tmp.axes[i];
    for (int i = 0; i < buttonCnt; i++)
      buttons[i] = tmp.buttons[i];
    for (int i = 0; i < hatCnt; i++)
      povHats[i] = tmp.povHats[i];
    sdlDevices[0] = tmp.sdlDevices[0];
    sdlDevices[1] = tmp.sdlDevices[1];
    nativeManager = tmp.nativeManager;
    haveJoystick = tmp.haveJoystick;

    // Ownership has moved to this instance; prevent tmp's destructor from
    // releasing the transferred HID objects.
    tmp.axisCnt = 0;
    tmp.buttonCnt = 0;
    tmp.hatCnt = 0;
    tmp.sdlDevices[0] = (void *) 0;
    tmp.sdlDevices[1] = (void *) 0;
    tmp.nativeManager = (void *) 0;
    tmp.haveJoystick = false;

    eventCnt = 0;
    eventIndex = 0;
    updateTimer.reset();
    mutex_.unlock();
#else
    // The legacy SDL backend is unchanged on non-macOS platforms.
#endif
  }

  void JoystickInput::lock()
  {
    mutex_.lock();
    lockFlag = true;
    mutex_.unlock();
  }

  void JoystickInput::unlock()
  {
    mutex_.lock();
    lockFlag = false;
    mutex_.unlock();
  }

  void JoystickInput::setConfiguration(const JoystickConfiguration& config_)
  {
    mutex_.lock();
    config.enableJoystick = config_.enableJoystick;
    config.enablePWM = config_.enablePWM;
    config.enableAutoFire = config_.enableAutoFire;
    config.axisThreshold = config_.axisThreshold;
    config.pwmFrequency = config_.pwmFrequency;
    config.autoFireFrequency = config_.autoFireFrequency;
    config.autoFirePulseWidth = config_.autoFirePulseWidth;
    mutex_.unlock();
  }

}       // namespace Ep128Emu

