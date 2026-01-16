#pragma once

#include "model/CalendarDayMeta.h"
#include "model/Note.h"
#include "win/UiTheme.h"

#include <array>
#include <chrono>
#include <string>
#include <vector>
#include <windows.h>

// Custom-drawn month calendar (premium look) with event markers.
enum class CalendarViewMode : int {
  Month = 0,
  Day = 1
};

enum class CalendarNotify : int {
  Selection = 1,
  MonthChanged = 2,
  OpenDay = 3,
  BackToMonth = 4,
  DayAdd = 5,
  DayEdit = 6,
  DayDelete = 7
};

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

  void setMode(CalendarViewMode mode);
  CalendarViewMode mode() const { return m_mode; }

  // Displayed month (local calendar)
  int year() const { return m_year; }
  int month() const { return m_month; } // 1..12

  void setMonth(int year, int month);
  void nextMonth();
  void prevMonth();

  SYSTEMTIME selectedDateLocal() const;

  // Day metadata (1..31), used for drawing markers + preview.
  void setDayMeta(const std::array<CalendarDayMeta, 32>& meta);

  void setDayNotes(std::vector<Note> notes);
  int dayClickMinutes() const { return m_dayClickMinutes; }
  std::wstring dayClickNoteId() const { return m_dayClickNoteId; }

private:
  static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void onPaint();
  void onSize(int w, int h);
  void onLButtonDown(int x, int y);
  void onLButtonDblClick(int x, int y);
  void onRButtonDown(int x, int y);
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
  std::wstring dayTitle() const;
  void setSelectedDate(int year, int month, int day);
  void moveSelectedDay(int deltaDays);

  struct Layout {
    RECT header{};
    RECT btnPrev{};
    RECT btnBack{};
    RECT btnNext{};
    RECT btnDecorToggle{};
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
  CalendarViewMode m_mode = CalendarViewMode::Month;

  int m_year = 0;
  int m_month = 0;
  int m_selectedDay = 0;

  std::array<CalendarDayMeta, 32> m_dayMeta{};
  std::vector<Note> m_dayNotes{};
  int m_dayClickMinutes = 0;
  std::wstring m_dayClickNoteId{};
  struct DayEventRect {
    RECT rc{};
    std::wstring id;
  };
  std::vector<DayEventRect> m_dayEventRects{};

  bool m_dayDecorations = true;

  // fonts
  HFONT m_fontHeader{};
  HFONT m_fontDay{};
  HFONT m_fontSmall{};

  UiTheme m_theme = UiTheme::fromStyle(UiThemeStyle::Premium);
};


