#pragma once

#include "model/Note.h"
#include "win/UiTheme.h"

#include <windows.h>

// Forward declare (defined in <richole.h>)
struct IRichEditOleCallback;

class NotificationWindow {
public:
  // If previewOnly==true and sourceRichForPreview is provided, content is copied directly
  // from the source RichEdit to preserve embedded images/objects.
  NotificationWindow(HINSTANCE hInstance, Note note, bool previewOnly = false, HWND sourceRichForPreview = nullptr);
  void show();

private:
  static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void onCreate();
  void onDestroy();
  void onTimer();
  void closeSelf();
  void showSnoozeMenu();
  void snoozeMinutes(int minutes);
  void layout(int width, int height);
  int desiredHeightForWidth(int width);

  void updateCountdownUi();
  void positionBottomRight();

  HINSTANCE m_hInstance{};
  Note m_note;
  HWND m_sourceRichForPreview{};

  HWND m_hwnd{};
  HWND m_lblTitle{};
  HWND m_btnClose{};
  HWND m_btnSnooze{};
  HWND m_rich{};
  HWND m_progress{};
  HWND m_lblCountdown{};

  HFONT m_font{};
  HFONT m_fontTitle{};

  IRichEditOleCallback* m_oleCb{}; // owned, released in onDestroy
  HICON m_appIcon{};

  UINT_PTR m_timerId{};
  int m_totalMs = 0;
  int m_elapsedMs = 0;

  UiTheme m_theme = UiTheme::fromStyle(UiThemeStyle::Premium);
  HBRUSH m_bgBrush{};
  COLORREF m_importanceColor{};
  bool m_previewOnly = false;
};


