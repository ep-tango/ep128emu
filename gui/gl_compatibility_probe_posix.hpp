#ifndef EP128EMU_GL_COMPATIBILITY_PROBE_POSIX_HPP
#define EP128EMU_GL_COMPATIBILITY_PROBE_POSIX_HPP
#if !defined(WIN32) && !defined(DISABLE_OPENGL_DISPLAY)
// Host-only disposable GL preflight. No guest framebuffer, ROM or VM state.
// X11/GLX tests a pbuffer; Cocoa tests a legacy CGL context and FBO.
// This deliberately does not reimplement the separate WGL probe.
#include "gl_compatibility_gate.hpp"
#include <string>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
# include <OpenGL/OpenGL.h>
# include <OpenGL/gl.h>
# include <OpenGL/glext.h>
#elif defined(__linux__)
# include <X11/Xlib.h>
# include <GL/gl.h>
# include <GL/glx.h>
# include <GL/glext.h>
#else
# error EP128emu_GL222_POSIX_requires_macOS_or_Linux
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
# define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
#ifndef GL_CONSTANT_COLOR
# define GL_CONSTANT_COLOR 0x8001
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
# define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif
namespace Ep128EmuGLCompat {
typedef void (*PosixBlendProc)(GLclampf,GLclampf,GLclampf,GLclampf);
struct PosixReport {
  Result result;
  std::string vendor,renderer,version;
  unsigned glError;
  bool doubleBuffered;
  PosixReport() : result(UNTESTED,"GL_UNTESTED",false,false),
      glError(0),doubleBuffered(false) {}
};
inline std::string safePosixGLName(const GLubyte* p) {
  std::string result;
  if (!p) return result;
  for (; *p && result.size()<80; ++p)
    result += (std::isalnum((unsigned char)*p) || *p==' ' || *p=='.' ||
               *p=='(' || *p==')' || *p=='_' || *p=='+' || *p=='-') ?
              char(*p) : '_';
  return result;
}
// Must run with a valid current context and a bound readable colour target.
// Callers own context creation/teardown and must restore the old context.
inline void renderPosix565Probe(PosixReport& report,PosixBlendProc blend,
                                GLenum readBuffer) {
  const GLubyte* v=glGetString(GL_VENDOR);
  const GLubyte* r=glGetString(GL_RENDERER);
  const GLubyte* n=glGetString(GL_VERSION);
  const GLubyte* e=glGetString(GL_EXTENSIONS);
  report.vendor=safePosixGLName(v);
  report.renderer=safePosixGLName(r);
  report.version=safePosixGLName(n);
  const bool blendOK=validProc(blend) && !testFault("blend");
  const char* classifiedVersion=testFault("legacy")?"1.1.0":(const char*)n;
  const char* classifiedExts=testFault("legacy")?"":(const char*)e;
  report.result=classify((const char*)v,(const char*)r,classifiedVersion,
                          classifiedExts,blendOK,0,true);
  if (report.result.state!=SUPPORTED) return;
  for (int i=0;i<8 && glGetError()!=GL_NO_ERROR;++i) {}
  const GLushort red[4]={0xf800,0xf800,0xf800,0xf800};
  GLuint texture=0;
  glGenTextures(1,&texture);
  glBindTexture(GL_TEXTURE_2D,texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,2,2,0,GL_RGB,
               GL_UNSIGNED_SHORT_5_6_5,red);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGB,
                  GL_UNSIGNED_SHORT_5_6_5,red);
  GLenum uploadError=glGetError();
  glViewport(0,0,16,16);
  glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
  glBegin(GL_QUADS);
  glTexCoord2f(0,0);glVertex2f(0,0);
  glTexCoord2f(1,0);glVertex2f(1,0);
  glTexCoord2f(1,1);glVertex2f(1,1);
  glTexCoord2f(0,1);glVertex2f(0,1);
  glEnd();
  glDisable(GL_TEXTURE_2D);
  blend(1.0f,0.0f,0.0f,1.0f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_CONSTANT_COLOR,GL_ONE_MINUS_CONSTANT_ALPHA);
  glDisable(GL_BLEND);
  glReadBuffer(readBuffer);
  GLubyte pixel[3]={0,0,0};
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  glReadPixels(8,8,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel);
  const GLenum err=glGetError();
  report.glError=(unsigned)(uploadError?uploadError:err);
  const bool pixelOK=(pixel[0]>160 && pixel[1]<100 && pixel[2]<100);
  report.result=classify((const char*)v,(const char*)r,classifiedVersion,
                          classifiedExts,blendOK,report.glError,
                          pixelOK && !testFault("readback"));
  glDeleteTextures(1,&texture);
}
inline bool runHiddenPosixProbe(bool doubleBuffered,PosixReport& report) {
  report.doubleBuffered=doubleBuffered;
  if (testFault("context")) {
    report.result=Result(PROBE_FAILED,"GL_TEST_CONTEXT_FAILURE",false,false);
    return false;
  }
#ifdef __APPLE__
  // FLTK's Cocoa renderer uses legacy fixed-function OpenGL. Check a real
  // accelerated CGL context with a private 16x16 FBO, not a guest surface.
  CGLPixelFormatAttribute attrs[9]={};
  int i=0;
  attrs[i++]=kCGLPFAAccelerated;
  attrs[i++]=kCGLPFAColorSize;
  attrs[i++]=(CGLPixelFormatAttribute)24;
  if (doubleBuffered)attrs[i++]=kCGLPFADoubleBuffer;
  attrs[i++]=(CGLPixelFormatAttribute)0;
  CGLPixelFormatObj pix=nullptr;
  GLint count=0;
  CGLError error=CGLChoosePixelFormat(attrs,&pix,&count);
  if (error!=kCGLNoError || !pix || count<1) {
    report.result=Result(PROBE_FAILED, "CGL_PIXEL_FORMAT_UNAVAILABLE",false,false);
    if (pix)CGLDestroyPixelFormat(pix);
    return false;
  }
  CGLContextObj context=nullptr;
  error=CGLCreateContext(pix,nullptr,&context);
  if (error!=kCGLNoError || !context) {
    report.result=Result(PROBE_FAILED, "CGL_CONTEXT_CREATE_FAILED",false,false);
    if (context)CGLDestroyContext(context);
    CGLDestroyPixelFormat(pix);
    return false;
  }
  const CGLContextObj prior=CGLGetCurrentContext();
  error=CGLSetCurrentContext(context);
  if (error==kCGLNoError) {
    GLuint fb=0,rb=0;
    // CGLSetOffScreen was removed from useful modern macOS paths. A legacy
    // GL_EXT_framebuffer_object works on the real macOS 2.1 context instead.
    const char* exts=(const char*)glGetString(GL_EXTENSIONS);
    if (!hasExtension(exts,"GL_EXT_framebuffer_object")) {
      report.result=Result(UNSUPPORTED, "CGL_OFFSCREEN_FBO_UNAVAILABLE",false,false);
    } else {
      glGenFramebuffersEXT(1,&fb);
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
      glGenRenderbuffersEXT(1,&rb);
      glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
      glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
      glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                                  GL_RENDERBUFFER_EXT,rb);
      if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=
          GL_FRAMEBUFFER_COMPLETE_EXT)
        report.result=Result(PROBE_FAILED, "CGL_OFFSCREEN_FBO_INCOMPLETE",false,false);
      else
        renderPosix565Probe(report,&glBlendColor,GL_COLOR_ATTACHMENT0_EXT);
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
      if (rb)glDeleteRenderbuffersEXT(1,&rb);
      if (fb)glDeleteFramebuffersEXT(1,&fb);
    }
  } else {
    report.result=Result(PROBE_FAILED, "CGL_CONTEXT_MAKE_CURRENT_FAILED",false,false);
  }
  CGLSetCurrentContext(prior);
  CGLDestroyContext(context);
  CGLDestroyPixelFormat(pix);
#elif defined(__linux__)
  // GLX PBuffer is a disposable, unmapped OpenGL target. On a Wayland-only
  // desktop without Xwayland this cannot represent EGL: fail safe to FLTK.
  Display* display=XOpenDisplay(nullptr);
  if (!display) {
    report.result=Result(PROBE_FAILED, "GLX_DISPLAY_UNAVAILABLE",false,false);
    return false;
  }
  int major=0,minor=0;
  if (!glXQueryVersion(display,&major,&minor) || major<1 ||
      (major==1 && minor<3)) {
    report.result=Result(UNSUPPORTED, "GLX_PBUFFER_UNAVAILABLE",false,false);
    XCloseDisplay(display);
    return false;
  }
  int attrs[]={GLX_DRAWABLE_TYPE,GLX_PBUFFER_BIT,GLX_RENDER_TYPE,GLX_RGBA_BIT,
               GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,None};
  int count=0;
  GLXFBConfig* formats=glXChooseFBConfig(display,DefaultScreen(display),
                                         attrs,&count);
  if (!formats || count<=0) {
    report.result=Result(PROBE_FAILED, "GLX_PIXEL_FORMAT_UNAVAILABLE",false,false);
    if (formats)XFree(formats);
    XCloseDisplay(display);
    return false;
  }
  GLXFBConfig format=formats[0];
  int pbAttrs[]={GLX_PBUFFER_WIDTH,16,GLX_PBUFFER_HEIGHT,16,None};
  GLXPbuffer pbuffer=glXCreatePbuffer(display,format,pbAttrs);
  GLXContext ctx=glXCreateNewContext(display,format,GLX_RGBA_TYPE,nullptr,True);
  const GLXContext previous=glXGetCurrentContext();
  const GLXDrawable previousDraw=glXGetCurrentDrawable();
  const GLXDrawable previousRead=glXGetCurrentReadDrawable();
  Display* previousDisplay=glXGetCurrentDisplay();
  bool current=false;
  if (pbuffer && ctx)
    current=(glXMakeContextCurrent(display,pbuffer,pbuffer,ctx)!=False);
  if (current) {
    PosixBlendProc blend=(PosixBlendProc)glXGetProcAddressARB(
        (const GLubyte*)"glBlendColor");
    if (!validProc(blend))
      blend=(PosixBlendProc)glXGetProcAddressARB(
          (const GLubyte*)"glBlendColorEXT");
    renderPosix565Probe(report,blend,GL_FRONT);
    glXMakeContextCurrent(display,None,None,nullptr);
  } else {
    report.result=Result(PROBE_FAILED, "GLX_CONTEXT_CREATE_FAILED",false,false);
  }
  if (previous && previousDisplay)
    glXMakeContextCurrent(previousDisplay,previousDraw,previousRead,previous);
  if (ctx)glXDestroyContext(display,ctx);
  if (pbuffer)glXDestroyPbuffer(display,pbuffer);
  XFree(formats);
  XCloseDisplay(display);
#endif
  return report.result.state==SUPPORTED;
}
inline void optInPosixDiagnostic(const PosixReport& report,const char* backend) {
  const char* enabled=std::getenv("EP128EMU_GL_DIAG");
  if (!enabled || std::strcmp(enabled,"1"))return;
  const char* tmp=std::getenv("TMPDIR");
  std::string filename=(tmp && *tmp)?std::string(tmp):std::string("/tmp");
  if (filename[filename.size()-1]!='/')filename+='/';
  filename+="ep128emu-gl.log";
  FILE* f=std::fopen(filename.c_str(),"a+b");
  if (!f)return;
  std::fseek(f,0,SEEK_END);
  const long len=std::ftell(f);
  if (len<0 || len>60000) {
    std::fclose(f);
    f=std::fopen(filename.c_str(),"wb");
  }
  if (!f)return;
  std::fprintf(f,"selected_backend=%s GL_VENDOR=%s GL_RENDERER=%s "
               "GL_VERSION=%s double_buffer=%d packed565=%d blend_color=%d "
               "probe=%s gl_error=0x%X fallback_reason=%s\n",
    backend,report.vendor.c_str(),report.renderer.c_str(),
    report.version.c_str(),int(report.doubleBuffered),
    int(report.result.packed565),int(report.result.blendColor),
    report.result.state==SUPPORTED?"PASS":"FAIL",
    report.glError,report.result.reason);
  std::fclose(f);
}
} // namespace Ep128EmuGLCompat
#endif // !WIN32 && !DISABLE_OPENGL_DISPLAY
#endif // include guard
