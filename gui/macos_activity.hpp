#ifndef EP128EMU_MACOS_ACTIVITY_HPP
#define EP128EMU_MACOS_ACTIVITY_HPP

#ifdef __APPLE__
class Fl_Window;
void *ep128emuBeginMacOSRealtimeActivity();
void ep128emuEndMacOSRealtimeActivity(void *activityToken);
void ep128emuSetMacOSTitlebarStatus(Fl_Window *window, const char *text,
                                 bool visible);
void ep128emuActivateMacOSWindow(Fl_Window *window);
float ep128emuGetMacOSBackingScale(Fl_Window *window);
int ep128emuGetMacOSWindowFrameExtraHeight(Fl_Window *window);
void ep128emuResizeMacOSWindowExact(Fl_Window *window, int x, int y, int w, int h);
void ep128emuRememberMacOSDragSourceApplication();
bool ep128emuHideRememberedMacOSDragSourceApplication();
void ep128emuUnhideRememberedMacOSDragSourceApplication();
void ep128emuClearRememberedMacOSDragSourceApplication();
// Asynchronous host-only acoustic relay click (does not alter DAVE audio).
bool ep128emuPrepareMacOSRelayClick();
bool ep128emuPlayMacOSRelayClick();
#endif

#endif
