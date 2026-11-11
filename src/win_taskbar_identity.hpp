// EP128Emu Windows 7+ taskbar identity; no guest/emulator dependencies.
// Windows taskbar group icons may come from Explorer/shortcut metadata rather
// than WM_SETICON on the HWND. Bind the real window to its active machine.
#ifndef EP128EMU_WIN_TASKBAR_IDENTITY_HPP
#define EP128EMU_WIN_TASKBAR_IDENTITY_HPP

#ifdef WIN32
#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <string>
#include <vector>

namespace Ep128EmuWinTaskbar {

inline bool setMachineIdentity(HWND window, int machineIcon)
{
  if (!window || !IsWindow(window) || machineIcon < 0 || machineIcon > 3)
    return false;

  // SHGetPropertyStoreForWindow is available on Windows 7 and later.
  HRESULT initResult = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  const bool initialized = SUCCEEDED(initResult);
  if (!initialized && initResult != RPC_E_CHANGED_MODE)
    return false;

  IPropertyStore *store = 0;
  HRESULT result = SHGetPropertyStoreForWindow(
      window, IID_IPropertyStore, (void **) &store);
  if (SUCCEEDED(result) && store) {
    std::vector<wchar_t> exePath(32768, 0);
    DWORD count = GetModuleFileNameW(0, &exePath[0], DWORD(exePath.size()));
    if (!count || size_t(count) >= exePath.size())
      result = E_FAIL;
    if (SUCCEEDED(result)) {
      static const wchar_t *const ids[4] = {
        L"EP128Emu.Machine.Enterprise", L"EP128Emu.Machine.Spectrum",
        L"EP128Emu.Machine.CPC", L"EP128Emu.Machine.TVC"
      };
      static const wchar_t *const args[4] = {
        L"", L" -zx", L" -cpc", L" -tvc"
      };
      const std::wstring path(&exePath[0]);
      const std::wstring iconResource =
          path + L",-" + std::to_wstring(machineIcon + 101);
      const std::wstring displayName =
          L"@" + path + L",-" + std::to_wstring(machineIcon + 201);
      const std::wstring relaunch = L"\"" + path + L"\"" + args[machineIcon];

      // Taskbar shell properties must precede the AppUserModelID. Setting
      // the ID last notifies Explorer to reevaluate the taskbar button.
      const PROPERTYKEY * const keys[3] = {
        &PKEY_AppUserModel_RelaunchIconResource,
        &PKEY_AppUserModel_RelaunchCommand,
        &PKEY_AppUserModel_RelaunchDisplayNameResource
      };
      const std::wstring * const values[3] = {
        &iconResource, &relaunch, &displayName
      };
      for (int i = 0; i < 3 && SUCCEEDED(result); i++) {
        PROPVARIANT pv = {};
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<LPWSTR>(values[i]->c_str());
        result = store->SetValue(*keys[i], pv);
      }
      if (SUCCEEDED(result))
        result = store->Commit();
      if (SUCCEEDED(result)) {
        PROPVARIANT pv = {};
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<LPWSTR>(ids[machineIcon]);
        result = store->SetValue(PKEY_AppUserModel_ID, pv);
      }
      if (SUCCEEDED(result))
        result = store->Commit();
    }
    store->Release();
  }
  if (initialized)
    CoUninitialize();
  return SUCCEEDED(result);
}

} // namespace Ep128EmuWinTaskbar
#endif // WIN32
#endif // EP128EMU_WIN_TASKBAR_IDENTITY_HPP
