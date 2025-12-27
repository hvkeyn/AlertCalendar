#pragma once

#include "model/CalendarDayMeta.h"
#include "win/UiTheme.h"

#include <array>
#include <chrono>
#include <string>
#include <windows.h>

// Custom-drawn month calendar (premium look) with event markers.
class CalendarView {
public:
  CalendarView();
  ~CalendarView();

  bool create(HINSTANCE hInstance, HWND parent, int controlId);
  HWND hwnd() const { return m_hwnd; }

  void setZoomPercent(int percent);
  int zoomPercent() const { return m_zoomPercent; }

  void setThemeStyle(UiThemeStyle style);
  UiThemeStyle themeStyle() const { return m_theme.style; }

  // Displayed month (local calendar)
  int year() const { return m_year; }
  int month() const { return m_month; } // 1..12

  void setMonth(int year, int month);
  void nextMonth();
  void prevMonth();

  SYSTEMTIME selectedDateLocal() const;

  // Day metadata (1..31), used for drawing markers + preview.
  void setDayMeta(const std::array<CalendarDayMeta, 32>& meta);

private:
  static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void onPaint();
  void onSize(int w, int h);
  void onLButtonDown(int x, int y);
  void onMouseWheel(short delta);

  void recalcLayout();
  void ensureFonts();
  void invalidate();

  void sendSelectionChanged();
  void sendMonthChanged();

  // Helpers
  int daysInMonth(int y, int m) const;
  int firstWeekdayMonday0(int y, int m) const; // 0..6 where 0=Mon
  std::wstring monthTitle() const;

  struct Layout {
    RECT header{};
    RECT btnPrev{};
    RECT btnNext{};
    RECT title{};
    RECT dowRow{};
    RECT grid{};
    int cellW = 0;
    int cellH = 0;
  };

  Layout m_layout{};

  HINSTANCE m_hInstance{};
  HWND m_parent{};
  int m_controlId = 0;
  HWND m_hwnd{};

  int m_zoomPercent = 100;

  int m_year = 0;
  int m_month = 0;
  int m_selectedDay = 0;

  std::array<CalendarDayMeta, 32> m_dayMeta{};

  // fonts
  HFONT m_fontHeader{};
  HFONT m_fontDay{};
  HFONT m_fontSmall{};

  UiTheme m_theme = UiTheme::fromStyle(UiThemeStyle::Premium);
};


