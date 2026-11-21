#ifndef EP128EMU_GL_COMPATIBILITY_PROBE_WIN_HPP
#define EP128EMU_GL_COMPATIBILITY_PROBE_WIN_HPP
// Windows-only, hidden, single-shot context check before any guest VM exists.
// The temporary WGL context is destroyed before the final FLTK renderer is
// created; it never takes focus, displays pixels or modifies user settings.
#ifdef WIN32
#include "gl_compatibility_gate.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR 0x8001
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif

namespace Ep128EmuGLCompat {
typedef void (APIENTRY *BlendColorProc)(GLclampf, GLclampf, GLclampf, GLclampf);
struct Report {
  Result result;
  std::string vendor,renderer,version;
  unsigned pixelFormat, glError;
  bool doubleBuffered;
  Report():result{UNTESTED,"GL_UNTESTED",false,false},
           pixelFormat(0),glError(0),doubleBuffered(false){}
};
inline std::string safeGLName(const GLubyte* name) {
  if (!name) return std::string();
  std::string out;
  for (const unsigned char* p=name; *p && out.size()<80; ++p)
    out += (std::isalnum(*p) || *p==' ' || *p=='.' || *p=='(' ||
            *p==')' || *p=='_' || *p=='+' || *p=='-' ? char(*p) : '_');
  return out;
}
inline bool runHiddenWGLProbe(bool doubleBuffered, Report& report) {
  report.doubleBuffered=doubleBuffered;
  if (testFault("context")) {
    report.result=Result(PROBE_FAILED,"GL_TEST_CONTEXT_FAILURE",false,false);
    return false;
  }
  HINSTANCE app=GetModuleHandleW(NULL);
  const wchar_t* name=L"EP128emu_GL_Probe_222";
  WNDCLASSW cls={};
  cls.style=CS_OWNDC;
  cls.lpfnWndProc=DefWindowProcW;
  cls.hInstance=app;
  cls.lpszClassName=name;
  ATOM registered=RegisterClassW(&cls);
  if (!registered) {
    report.result={PROBE_FAILED,"GL_PROBE_WINDOW_CLASS",false,false};
    return false;
  }
  HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,name,L"",WS_POPUP,
                            -32000,-32000,16,16,NULL,NULL,app,NULL);
  HDC dc=hwnd?GetDC(hwnd):NULL;
  HGLRC context=NULL;
  HDC previousDC=wglGetCurrentDC();
  HGLRC previousContext=wglGetCurrentContext();
  bool current=false;
  if (dc) {
    PIXELFORMATDESCRIPTOR want={};
    want.nSize=sizeof(want);
    want.nVersion=1;
    want.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|
                  (doubleBuffered?PFD_DOUBLEBUFFER:0);
    want.iPixelType=PFD_TYPE_RGBA;
    want.cColorBits=24;
    want.cDepthBits=16;
    want.iLayerType=PFD_MAIN_PLANE;
    const int pixelFormat=ChoosePixelFormat(dc,&want);
    if (pixelFormat>0 && SetPixelFormat(dc,pixelFormat,&want)) {
      report.pixelFormat=unsigned(pixelFormat);
      PIXELFORMATDESCRIPTOR actual={};
      if (DescribePixelFormat(dc,pixelFormat,sizeof(actual),&actual)) {
        if (((actual.dwFlags & PFD_DOUBLEBUFFER)!=0) != doubleBuffered)
          report.result={UNSUPPORTED,"GL_PIXEL_FORMAT_BUFFER_MISMATCH",false,false};
        else
          context=wglCreateContext(dc);
      }
    }
  }
  if (context)current=(wglMakeCurrent(dc,context)!=FALSE);
  if (!current) {
    if (report.result.state==UNTESTED)
      report.result={PROBE_FAILED,"GL_CONTEXT_CREATE_FAILED",false,false};
  } else {
    const GLubyte* v=glGetString(GL_VENDOR);
    const GLubyte* r=glGetString(GL_RENDERER);
    const GLubyte* n=glGetString(GL_VERSION);
    const GLubyte* extensions=glGetString(GL_EXTENSIONS);
    report.vendor=safeGLName(v);
    report.renderer=safeGLName(r);
    report.version=safeGLName(n);
    PROC blend=wglGetProcAddress("glBlendColor");
    if (!validProc(blend))
      blend=wglGetProcAddress("glBlendColorEXT");
    const bool blendValid=validProc(blend) && !testFault("blend");
    const char* classifiedVersion=testFault("legacy")?"1.1.0":(const char*)n;
    const char* classifiedExts=testFault("legacy")?"":(const char*)extensions;
    report.result=classify((const char*)v,(const char*)r,
      classifiedVersion,classifiedExts,blendValid,0,true);
    if (report.result.state==SUPPORTED) {
      // Work only in this disposable context, never in a guest framebuffer.
      for (int i=0;i<8 && glGetError()!=GL_NO_ERROR;++i){}
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
      const GLenum uploadError=glGetError();
      glViewport(0,0,16,16);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0,1,0,1,-1,1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glClearColor(0,0,0,1);
      glClear(GL_COLOR_BUFFER_BIT);
      glEnable(GL_TEXTURE_2D);
      glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
      glBegin(GL_QUADS);
      glTexCoord2f(0,0); glVertex2f(0,0);
      glTexCoord2f(1,0); glVertex2f(1,0);
      glTexCoord2f(1,1); glVertex2f(1,1);
      glTexCoord2f(0,1); glVertex2f(0,1);
      glEnd();
      glDisable(GL_TEXTURE_2D);
      ((BlendColorProc)blend)(1.0f,0.0f,0.0f,1.0f);
      glEnable(GL_BLEND);
      glBlendFunc(GL_CONSTANT_COLOR,GL_ONE_MINUS_CONSTANT_ALPHA);
      glDisable(GL_BLEND);
      glReadBuffer(doubleBuffered?GL_BACK:GL_FRONT);
      GLubyte pixel[3]={0,0,0};
      glPixelStorei(GL_PACK_ALIGNMENT,1);
      glReadPixels(8,8,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel);
      const GLenum renderError=glGetError();
      report.glError=unsigned(uploadError?uploadError:renderError);
      const bool redPixel=(pixel[0]>160 && pixel[1]<100 && pixel[2]<100);
      report.result=classify((const char*)v,(const char*)r,
         classifiedVersion,classifiedExts,blendValid,
         report.glError,redPixel && !testFault("readback"));
      glDeleteTextures(1,&texture);
    }
  }
  // Restore the previous context, then destroy all temporary resources.
  wglMakeCurrent(NULL,NULL);
  if (context)wglDeleteContext(context);
  if (previousContext && previousDC)wglMakeCurrent(previousDC,previousContext);
  if (dc)ReleaseDC(hwnd,dc);
  if (hwnd)DestroyWindow(hwnd);
  UnregisterClassW(name,app);
  return report.result.state==SUPPORTED;
}
inline void optInDiagnostic(const Report& report, const char* backend) {
  const char* enabled=std::getenv("EP128EMU_GL_DIAG");
  if (!enabled || std::strcmp(enabled,"1"))return;
  wchar_t directory[MAX_PATH+1]={0};
  DWORD n=GetTempPathW(MAX_PATH,directory);
  if (!n || n>=MAX_PATH-20)return;
  std::wstring file(directory);
  file+=L"ep128emu-gl.log";
  std::FILE* f=_wfopen(file.c_str(),L"a+b");
  if (!f)return;
  std::fseek(f,0,SEEK_END);
  long bytes=std::ftell(f);
  // Bound file size, without writing profile paths or guest data.
  if (bytes<0 || bytes>60000){std::fclose(f);f=_wfopen(file.c_str(),L"wb");}
  if (f) {
    std::fprintf(f,"selected_backend=%s GL_VENDOR=%s GL_RENDERER=%s "
      "GL_VERSION=%s pixel_format=%u double_buffer=%d packed565=%d "
      "blend_color=%d probe=%s gl_error=0x%X fallback_reason=%s\n",
      backend,report.vendor.c_str(),report.renderer.c_str(),
      report.version.c_str(),report.pixelFormat,int(report.doubleBuffered),
      int(report.result.packed565),int(report.result.blendColor),
      report.result.state==SUPPORTED?"PASS":"FAIL",
      report.glError,report.result.reason);
    std::fclose(f);
  }
}
} // namespace Ep128EmuGLCompat
#endif
#endif
