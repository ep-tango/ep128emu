#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commctrl.h>
#include "win_native_drop.hpp"
#include <string>
#include <vector>

namespace {
struct TargetState {
  HWND hwnd;
  bool oleRegistered;
  bool subclassed;
  TargetState():hwnd(NULL),oleRegistered(false),subclassed(false){}
} targets[2];
Ep128EmuWinDrop::FileDropCallback callback_=0;
void *user_=0;
bool oleStarted=false;

std::string utf8(const std::wstring& s) {
  if(s.empty())return std::string();
  int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),
                            NULL,0,NULL,NULL);
  if(n<=0)return std::string();
  std::string out(static_cast<size_t>(n),'\0');
  if(!WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),
                          &out[0],n,NULL,NULL))return std::string();
  return out;
}
void deliver(HDROP drop) {
  if(!callback_ || !drop)return;
  UINT n=DragQueryFileW(drop,0xFFFFFFFF,NULL,0);
  if(n==0 || n>64)return;
  std::string paths;
  for(UINT i=0;i<n;++i) {
    UINT len=DragQueryFileW(drop,i,NULL,0);
    if(len<1 || len>32760)return;
    std::vector<wchar_t> buffer(static_cast<size_t>(len)+1U,L'\0');
    if(!DragQueryFileW(drop,i,&buffer[0],len+1U))return;
    if(i)paths+='\n';
    paths+=utf8(std::wstring(&buffer[0]));
  }
  if(!paths.empty())callback_(user_,paths);
}
LRESULT CALLBACK fileDropSubclass(HWND hwnd,UINT msg,WPARAM w,LPARAM l,
                                  UINT_PTR id,DWORD_PTR) {
  if(msg==WM_DROPFILES) {
    HDROP drop=reinterpret_cast<HDROP>(w);
    deliver(drop);
    DragFinish(drop);
    return 0;
  }
  if(msg==WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd,fileDropSubclass,id);
    for(int i=0;i<2;++i)
      if(targets[i].hwnd==hwnd) {
        targets[i].hwnd=NULL;
        targets[i].subclassed=false;
        targets[i].oleRegistered=false;
      }
  }
  return DefSubclassProc(hwnd,msg,w,l);
}
class FileDropTarget : public IDropTarget {
  LONG refs_;
  bool allowed_;
 public:
  FileDropTarget():refs_(1),allowed_(false){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) {
    if(!out)return E_POINTER;
    *out=NULL;
    if(IsEqualIID(id,IID_IUnknown)||IsEqualIID(id,IID_IDropTarget)) {
      *out=static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef(){return ULONG(InterlockedIncrement(&refs_));}
  ULONG STDMETHODCALLTYPE Release() {
    ULONG n=ULONG(InterlockedDecrement(&refs_));
    if(!n)delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data,DWORD,
                  POINTL,DWORD *effect) {
    FORMATETC format={CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    allowed_=(data && data->QueryGetData(&format)==S_OK);
    if(effect)*effect=(allowed_?DROPEFFECT_COPY:DROPEFFECT_NONE);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD *effect) {
    if(effect)*effect=(allowed_?DROPEFFECT_COPY:DROPEFFECT_NONE);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() {allowed_=false;return S_OK;}
  HRESULT STDMETHODCALLTYPE Drop(IDataObject *data,DWORD,
                   POINTL,DWORD *effect) {
    if(effect)*effect=DROPEFFECT_NONE;
    if(!allowed_ || !data)return S_OK;
    FORMATETC format={CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    STGMEDIUM medium={};
    if(data->GetData(&format,&medium)==S_OK) {
      deliver(reinterpret_cast<HDROP>(medium.hGlobal));
      ReleaseStgMedium(&medium);
      if(effect)*effect=DROPEFFECT_COPY;
    }
    allowed_=false;
    return S_OK;
  }
};
bool install(HWND hwnd,int i) {
  if(!hwnd || !IsWindow(hwnd))return false;
  targets[i].hwnd=hwnd;
  DragAcceptFiles(hwnd,TRUE);
  targets[i].subclassed=(SetWindowSubclass(hwnd,fileDropSubclass,
                              static_cast<UINT_PTR>(0xE112+i),0)!=FALSE);
  // FLTK may have registered its own target. Replace only the file-drop
  // handling on the emulator's two dedicated windows.
  if(oleStarted) {
    RevokeDragDrop(hwnd);
    FileDropTarget *target=new FileDropTarget();
    HRESULT hr=RegisterDragDrop(hwnd,target);
    targets[i].oleRegistered=(hr==S_OK);
    target->Release();
  }
  return targets[i].subclassed || targets[i].oleRegistered;
}
}
namespace Ep128EmuWinDrop {
void detach() {
  for(int i=1;i>=0;--i) {
    TargetState& t=targets[i];
    if(t.hwnd&&IsWindow(t.hwnd)) {
      if(t.oleRegistered)RevokeDragDrop(t.hwnd);
      DragAcceptFiles(t.hwnd,FALSE);
      if(t.subclassed)
        RemoveWindowSubclass(t.hwnd,fileDropSubclass,
                             static_cast<UINT_PTR>(0xE112+i));
    }
    t=TargetState();
  }
  if(oleStarted){OleUninitialize();oleStarted=false;}
  callback_=NULL;user_=NULL;
}
bool attach(void *main,void *video,FileDropCallback callback,void *user) {
  detach();
  if(!callback)return false;
  callback_=callback;user_=user;
  HRESULT hr=OleInitialize(NULL);
  oleStarted=SUCCEEDED(hr);
  bool ok=install(reinterpret_cast<HWND>(main),0);
  HWND child=reinterpret_cast<HWND>(video);
  if(child && child!=reinterpret_cast<HWND>(main))
    ok=install(child,1)||ok;
  return ok;
}
}
#endif
