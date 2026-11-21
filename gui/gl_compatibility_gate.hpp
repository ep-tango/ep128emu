#ifndef EP128EMU_GL_COMPATIBILITY_GATE_HPP
#define EP128EMU_GL_COMPATIBILITY_GATE_HPP

#include <stdint.h>
#include <cstdlib>
#include <cstring>

namespace Ep128EmuGLCompat {
enum State { UNTESTED, SUPPORTED, UNSUPPORTED, PROBE_FAILED };
struct Result {
  State state;
  const char* reason;
  bool packed565;
  bool blendColor;
  Result(State s=UNTESTED,const char* r="GL_UNTESTED",bool p=false,bool b=false)
      : state(s),reason(r),packed565(p),blendColor(b) {}
  bool fallback() const { return state != SUPPORTED; }
};
// Fault injection exists only in explicitly compiled private QA builds.
// Production builds cannot read or react to EP128EMU_GL_FAULT.
inline bool testFault(const char* expected) {
#ifdef EP128EMU_GL_FAULT_TEST
  const char* mode=std::getenv("EP128EMU_GL_FAULT");
  return mode && std::strcmp(mode,expected)==0;
#else
  (void) expected;
  return false;
#endif
}
// Windows wglGetProcAddress can return these non-null failure sentinels.
inline bool validProcAddress(uintptr_t p) {
  return p > 3U && p != UINTPTR_MAX;
}
template <class T> inline bool validProc(T p) {
  return validProcAddress(reinterpret_cast<uintptr_t>(p));
}
inline bool atLeast(const char* v, unsigned major, unsigned minor) {
  if (!v) return false;
  // A desktop GL_VERSION string starts with numeric major.minor.
  char* end = NULL;
  unsigned long a = std::strtoul(v, &end, 10);
  if (!end || end == v || *end != '.' || a > 255UL) return false;
  const char* start = end + 1;
  unsigned long b = std::strtoul(start, &end, 10);
  if (!end || end == start || b > 255UL) return false;
  return a > major || (a == major && b >= minor);
}
inline bool hasExtension(const char* list, const char* key) {
  if (!list || !key || !*key || std::strchr(key, ' ')) return false;
  const std::size_t n = std::strlen(key);
  for (const char* p = list; (p = std::strstr(p,key)); ++p)
    if ((p == list || p[-1] == ' ') &&
        (p[n] == '\0' || p[n] == ' ')) return true;
  return false;
}
// Probe conclusions only; the live driver test is run with a current WGL
// context by gl_compatibility_probe_win.hpp before VM construction. Black
// guest content is never consulted in this function.
inline Result classify(const char* vendor, const char* renderer,
                       const char* version, const char* extensions,
                       bool validBlendProc, unsigned glError,
                       bool texturedQuadReadbackOK) {
  if (!vendor || !renderer || !version || !*vendor || !*renderer ||
      !*version)
    return Result(PROBE_FAILED, "GL_CONTEXT_INFO_NULL", false, false);
  const bool packed565 = atLeast(version,1,2) ||
    hasExtension(extensions,"GL_EXT_packed_pixels");
  const bool blendColor = (atLeast(version,1,4) ||
    hasExtension(extensions,"GL_EXT_blend_color")) && validBlendProc;
  if (!packed565) return Result(UNSUPPORTED, "GL_UNSUPPORTED_565", false, blendColor);
  if (!blendColor) return Result(UNSUPPORTED, "GL_UNSUPPORTED_BLEND", packed565, false);
  if (glError) return Result(PROBE_FAILED, "GL_PROBE_ERROR", packed565, blendColor);
  if (!texturedQuadReadbackOK)
    return Result(PROBE_FAILED, "GL_PROBE_READBACK_FAILED", packed565, blendColor);
  return Result(SUPPORTED, "GL_PROBE_PASS", packed565, blendColor);
}
} // namespace Ep128EmuGLCompat
#endif
