#import <Cocoa/Cocoa.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#include "macos_activity.hpp"

static NSWindow *ep128emuStatusWindow = nil;
static NSTitlebarAccessoryViewController *ep128emuStatusController = nil;
static NSTextField *ep128emuStatusField = nil;
static NSRunningApplication *ep128emuDragSourceApplication = nil;
static NSSound *ep128emuRelayClickSound = nil;

// Update the running application's Dock tile without modifying its .app
// bundle icon, executable identity or the existing machine-specific window
// icons. Startup/guest-machine switching creates a new emulation process,
// and this runs after its first FLTK window is shown.
void ep128emuSetMacOSMachineDockIcon(int machineIconIndex)
{
  if (machineIconIndex < 0 || machineIconIndex > 3)
    return;
  @autoreleasepool {
    static NSString * const iconNames[4] = {
      @"ep128emu", @"zx128emu", @"cpc464emu", @"tvc64emu"
    };
    NSString *extension = (machineIconIndex == 0 ? @"icns" : @"png");
    NSString *path = [[NSBundle mainBundle]
        pathForResource:iconNames[machineIconIndex] ofType:extension];
    if (!path)
      return; // Unbundled developer executable: preserve the default icon.
    NSImage *source = [[NSImage alloc] initWithContentsOfFile:path];
    if (!source)
      return;
    if (machineIconIndex == 0) {
      [[NSApplication sharedApplication] setApplicationIconImage:source];
    }
    else {
      // The original 32x32 retro icons should remain pixel-sharp in Dock,
      // not blurred by the normal high-DPI interpolation.
      NSSize size = NSMakeSize(256.0, 256.0);
      NSImage *dockImage = [[NSImage alloc] initWithSize:size];
      [dockImage lockFocus];
      [[NSGraphicsContext currentContext]
          setImageInterpolation:NSImageInterpolationNone];
      [source drawInRect:NSMakeRect(0.0, 0.0, size.width, size.height)
               fromRect:NSZeroRect
              operation:NSCompositingOperationCopy fraction:1.0];
      [dockImage unlockFocus];
      [[NSApplication sharedApplication] setApplicationIconImage:dockImage];
      [dockImage release];
    }
    [source release];
  }
}

// Prepare CoreAudio/NSSound BEFORE the guest VM starts. The first
// NSSound play can spend >1s opening CoreAudio and displace the startup
// click into the next program header; subsequent plays are much faster.
// Warming up at zero volume never changes REM, tape samples or guest time.
bool ep128emuPrepareMacOSRelayClick()
{
  @autoreleasepool {
    if (ep128emuRelayClickSound)
      return true;
    NSString *resourcePath = [[NSBundle mainBundle]
        pathForResource:@"EP_rele" ofType:@"wav"];
    if (!resourcePath)
      return false;
    NSSound *sound = [[NSSound alloc]
        initWithContentsOfFile:resourcePath byReference:YES];
    if (!sound)
      return false;
    [sound setVolume:0.0f];
    BOOL warmed = [sound play];
    if (warmed)
      [sound stop];
    [sound setVolume:1.0f];
    ep128emuRelayClickSound = sound;
    return warmed ? true : false;
  }
}

bool ep128emuPlayMacOSRelayClick()
{
  @autoreleasepool {
    if (!ep128emuRelayClickSound) {
      NSString *resourcePath = [[NSBundle mainBundle]
          pathForResource:@"EP_rele" ofType:@"wav"];
      if (!resourcePath)
        return false;
      ep128emuRelayClickSound = [[NSSound alloc]
          initWithContentsOfFile:resourcePath byReference:YES];
      if (!ep128emuRelayClickSound)
        return false;
      [ep128emuRelayClickSound setVolume:1.0f];
    }
    // Only one short relay sound at a time; avoid rapid retrigger/restarts.
    if ([ep128emuRelayClickSound isPlaying])
      return true;
    return ([ep128emuRelayClickSound play] ? true : false);
  }
}


static void ep128emuEnsureMacOSTitlebarStatus(Fl_Window *window)
{
  if (!window)
    return;
  NSWindow *nsWindow = (NSWindow *) fl_xid(window);
  if (!nsWindow)
    return;
  if (ep128emuStatusController && ep128emuStatusWindow == nsWindow)
    return;

  ep128emuStatusWindow = nsWindow;
  ep128emuStatusController = [[NSTitlebarAccessoryViewController alloc] init];
  NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 128, 22)];
  NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 1, 128, 18)];
  [field setEditable:NO];
  [field setSelectable:NO];
  [field setBezeled:NO];
  [field setBordered:NO];
  [field setDrawsBackground:NO];
  [field setAlignment:NSTextAlignmentCenter];
  [field setTextColor:[NSColor secondaryLabelColor]];
  [field setFont:[NSFont userFixedPitchFontOfSize:11.0]];
  [field setStringValue:@"FPS  --.-"];
  [container addSubview:field];
  [ep128emuStatusController setView:container];
  [ep128emuStatusController setLayoutAttribute:NSLayoutAttributeRight];
  [nsWindow addTitlebarAccessoryViewController:ep128emuStatusController];
  ep128emuStatusField = field;
  [field release];
  [container release];
}

void ep128emuSetMacOSTitlebarStatus(Fl_Window *window, const char *text,
                                 bool visible)
{
  @autoreleasepool {
    ep128emuEnsureMacOSTitlebarStatus(window);
    if (!ep128emuStatusController || !ep128emuStatusField)
      return;
    NSView *view = [ep128emuStatusController view];
    NSString *newValue =
        ((text && text[0]) ? [NSString stringWithUTF8String:text] : @"");
    CGFloat visibleWidth = 128.0;
    if ([newValue length] > 0) {
      NSDictionary *attrs =
          [NSDictionary dictionaryWithObject:[ep128emuStatusField font]
                                      forKey:NSFontAttributeName];
      visibleWidth = ceil([newValue sizeWithAttributes:attrs].width) + 16.0;
      if (visibleWidth < 84.0)
        visibleWidth = 84.0;
      if (visibleWidth > 240.0)
        visibleWidth = 240.0;
    }
    NSRect r = [view frame];
    r.size.width = (visible ? visibleWidth : 0.0);
    [view setFrame:r];
    NSRect fr = [ep128emuStatusField frame];
    fr.size.width = visibleWidth;
    [ep128emuStatusField setFrame:fr];
    [view setHidden:(visible ? NO : YES)];
    if ([newValue length] > 0 &&
        ![[ep128emuStatusField stringValue] isEqualToString:newValue])
      [ep128emuStatusField setStringValue:newValue];
  }
}


float ep128emuGetMacOSBackingScale(Fl_Window *window)
{
  @autoreleasepool {
    NSScreen *screen = nil;
    if (window) {
      NSWindow *nsWindow = (NSWindow *) fl_xid(window);
      if (nsWindow)
        screen = [nsWindow screen];
    }
    if (!screen)
      screen = [NSScreen mainScreen];
    CGFloat scale = (screen ? [screen backingScaleFactor] : 1.0);
    if (scale < 1.0)
      scale = 1.0;
    return float(scale);
  }
}

int ep128emuGetMacOSWindowFrameExtraHeight(Fl_Window *window)
{
  @autoreleasepool {
    CGFloat extraHeight = 0.0;
    if (window) {
      NSWindow *nsWindow = (NSWindow *) fl_xid(window);
      if (nsWindow) {
        NSRect frameRect = [nsWindow frame];
        NSRect contentRect = [[nsWindow contentView] frame];
        extraHeight = frameRect.size.height - contentRect.size.height;
      }
    }
    if (extraHeight <= 0.0) {
      NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable |
                                NSWindowStyleMaskResizable;
      NSRect contentRect = NSMakeRect(0.0, 0.0, 1000.0, 800.0);
      NSRect frameRect =
          [NSWindow frameRectForContentRect:contentRect styleMask:style];
      extraHeight = frameRect.size.height - contentRect.size.height;
    }
    if (extraHeight < 0.0)
      extraHeight = 0.0;
    return int(ceil(extraHeight));
  }
}

void ep128emuResizeMacOSWindowExact(Fl_Window *window,
                                    int x, int y, int w, int h)
{
  if (!window)
    return;

  @autoreleasepool {
    NSWindow *nsWindow = (NSWindow *) fl_xid(window);
    if (!nsWindow) {
      window->resize(x, y, w, h);
      return;
    }

    NSSize savedMin = [nsWindow contentMinSize];
    int screenNum = window->screen_num();
    if (screenNum < 0)
      screenNum = 0;
    CGFloat fltkScale = CGFloat(Fl::screen_scale(screenNum));
    if (fltkScale <= 0.0)
      fltkScale = 1.0;

    NSSize guardedMin =
        NSMakeSize(ceil(CGFloat(w) * fltkScale),
                   ceil(CGFloat(h) * fltkScale));
    if (guardedMin.width < savedMin.width)
      guardedMin.width = savedMin.width;
    if (guardedMin.height < savedMin.height)
      guardedMin.height = savedMin.height;

    [nsWindow setContentMinSize:guardedMin];
    window->resize(x, y, w, h);
    [nsWindow setContentMinSize:savedMin];
  }
}

void ep128emuActivateMacOSWindow(Fl_Window *window)
{
  @autoreleasepool {
    NSRunningApplication *current = [NSRunningApplication currentApplication];
    if (current) {
      [current activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
    if (window) {
      NSWindow *nsWindow = (NSWindow *) fl_xid(window);
      if (nsWindow)
        [nsWindow makeKeyAndOrderFront:nil];
    }
  }
}

void ep128emuRememberMacOSDragSourceApplication()
{
  @autoreleasepool {
    if (ep128emuDragSourceApplication)
      return;
    NSRunningApplication *source =
        [[NSWorkspace sharedWorkspace] frontmostApplication];
    NSRunningApplication *current = [NSRunningApplication currentApplication];
    if (!source || !current ||
        [source processIdentifier] == [current processIdentifier])
      return;
#if !__has_feature(objc_arc)
    [source retain];
#endif
    ep128emuDragSourceApplication = source;
  }
}

bool ep128emuHideRememberedMacOSDragSourceApplication()
{
  @autoreleasepool {
    if (!ep128emuDragSourceApplication ||
        [ep128emuDragSourceApplication isTerminated])
      return false;
    return ([ep128emuDragSourceApplication hide] ? true : false);
  }
}

void ep128emuUnhideRememberedMacOSDragSourceApplication()
{
  @autoreleasepool {
    if (!ep128emuDragSourceApplication ||
        [ep128emuDragSourceApplication isTerminated])
      return;
    [ep128emuDragSourceApplication unhide];
  }
}

void ep128emuClearRememberedMacOSDragSourceApplication()
{
  @autoreleasepool {
    if (!ep128emuDragSourceApplication)
      return;
#if !__has_feature(objc_arc)
    [ep128emuDragSourceApplication release];
#endif
    ep128emuDragSourceApplication = nil;
  }
}

void *ep128emuBeginMacOSRealtimeActivity()
{
  @autoreleasepool {
    NSActivityOptions options =
        NSActivityUserInitiatedAllowingIdleSystemSleep |
        NSActivityLatencyCritical;
    id token = [[NSProcessInfo processInfo]
        beginActivityWithOptions:options
        reason:@"ep128emu real-time emulation and audio"];
#if !__has_feature(objc_arc)
    [token retain];
#endif
    return (void *) token;
  }
}

void ep128emuEndMacOSRealtimeActivity(void *activityToken)
{
  if (!activityToken)
    return;
  @autoreleasepool {
    id token = (id) activityToken;
    [[NSProcessInfo processInfo] endActivity:token];
#if !__has_feature(objc_arc)
    [token release];
#endif
  }
}
