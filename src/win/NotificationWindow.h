#pragma once

#include "model/Note.h"
#include "win/UiTheme.h"

#include <windows.h>

class NotificationWindow {
public:
  NotificationWindow(HINSTANCE hInstance, Note note, bool previewOnly = false);
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

  void updateCountdownUi();
  void positionBottomRight();

  HINSTANCE m_hInstance{};
  Note m_note;

  HWND m_hwnd{};
  HWND m_lblTitle{};
  HWND m_btnClose{};
  HWND m_btnSnooze{};
  HWND m_rich{};
  HWND m_progress{};
  HWND m_lblCountdown{};

  HFONT m_font{};
  HFONT m_fontTitle{};

  UINT_PTR m_timerId{};
  int m_totalMs = 0;
  int m_elapsedMs = 0;

  UiTheme m_theme = UiTheme::fromStyle(UiThemeStyle::Premium);
  HBRUSH m_bgBrush{};
  COLORREF m_importanceColor{};
  bool m_previewOnly = false;
};


