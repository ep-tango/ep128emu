#ifndef EP128EMU_WIN_NATIVE_DROP_HPP
#define EP128EMU_WIN_NATIVE_DROP_HPP
#if defined(WIN32)
#include <string>
namespace Ep128EmuWinDrop {
  typedef void (*FileDropCallback)(void *,const std::string&);
  bool attach(void *mainHwnd,void *videoHwnd,
              FileDropCallback callback,void *user);
  void detach();
}
#endif
#endif
