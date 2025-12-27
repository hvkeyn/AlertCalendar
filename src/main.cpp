#include "app/SingleInstance.h"
#include "win/MainWindow.h"
#include "win/WinUtil.h"

#include <windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  WinUtil::enableDpiAwareness();

  // Required by WebView2 / COM-based UI components.
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool comInit = SUCCEEDED(hr);

  SingleInstance instance(L"AlertCalendar.Singleton");
  if (!instance.tryLock()) {
    MessageBoxW(nullptr, L"AlertCalendar уже запущен.", L"AlertCalendar", MB_ICONINFORMATION);
    if (comInit) CoUninitialize();
    return 0;
  }

  MainWindow w(hInstance);
  if (!w.create()) {
    return 1;
  }
  w.show(nCmdShow);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (comInit) CoUninitialize();
  return 0;
}


