#include "CalendarView.h"

#include <algorithm>
#include <windowsx.h>

namespace {
constexpr int kHeaderBaseH = 44;
constexpr int kDowBaseH = 24;
constexpr int kCellBaseH = 86;

constexpr int kSelCode = 1;
constexpr int kMonthCode = 2;

int scale(int px, int zoom) { return MulDiv(px, zoom, 100); }

bool ptInRect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

void fillRectColor(HDC hdc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(hdc, &r, b);
  DeleteObject(b);
}

void drawRoundRect(HDC hdc, const RECT& r, int radius, COLORREF fill, COLORREF border) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldB = SelectObject(hdc, b);
  HGDIOBJ oldP = SelectObject(hdc, p);
  RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
  SelectObject(hdc, oldB);
  SelectObject(hdc, oldP);
  DeleteObject(b);
  DeleteObject(p);
}

void drawText(HDC hdc, const std::wstring& s, RECT r, UINT format, COLORREF c) {
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, c);
  DrawTextW(hdc, s.c_str(), static_cast<int>(s.size()), &r, format);
}

std::wstring monthNameRu(int month) {
  static const wchar_t* names[] = {
    L"", L"Январь", L"Февраль", L"Март", L"Апрель", L"Май", L"Июнь",
    L"Июль", L"Август", L"Сентябрь", L"Октябрь", L"Ноябрь", L"Декабрь"
  };
  if (month < 1 || month > 12) return L"";
  return names[month];
}
} // namespace

CalendarView::CalendarView() {
  // init to current local date
  SYSTEMTIME st{};
  GetLocalTime(&st);
  m_year = st.wYear;
  m_month = st.wMonth;
  m_selectedDay = st.wDay;
  for (auto& d : m_dayMeta) d = CalendarDayMeta{};
}

CalendarView::~CalendarView() {
  if (m_fontHeader) DeleteObject(m_fontHeader);
  if (m_fontDay) DeleteObject(m_fontDay);
  if (m_fontSmall) DeleteObject(m_fontSmall);
}

bool CalendarView::create(HINSTANCE hInstance, HWND parent, int controlId) {
  m_hInstance = hInstance;
  m_parent = parent;
  m_controlId = controlId;

  const wchar_t* kClassName = L"AlertCalendar.CalendarView";

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &CalendarView::wndProcThunk;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = sizeof(void*);
  wc.hInstance = m_hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr; // we'll paint ourselves
  wc.lpszClassName = kClassName;

  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  m_hwnd = CreateWindowExW(
    0,
    kClassName,
    nullptr,
    WS_CHILD | WS_VISIBLE,
    0, 0, 100, 100,
    m_parent,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(m_controlId)),
    m_hInstance,
    this
  );

  return m_hwnd != nullptr;
}

void CalendarView::setZoomPercent(int percent) {
  percent = std::clamp(percent, 50, 250);
  if (m_zoomPercent == percent) return;
  m_zoomPercent = percent;
  ensureFonts();
  recalcLayout();
  invalidate();
}

void CalendarView::setThemeStyle(UiThemeStyle style) {
  if (m_theme.style == style) return;
  m_theme = UiTheme::fromStyle(style);
  invalidate();
}

void CalendarView::setMonth(int year, int month) {
  if (month < 1) { month = 12; --year; }
  if (month > 12) { month = 1; ++year; }

  if (m_year == year && m_month == month) return;
  m_year = year;
  m_month = month;

  const int dim = daysInMonth(m_year, m_month);
  m_selectedDay = std::clamp(m_selectedDay, 1, dim);

  sendMonthChanged();
  invalidate();
}

void CalendarView::nextMonth() { setMonth(m_year, m_month + 1); }
void CalendarView::prevMonth() { setMonth(m_year, m_month - 1); }

SYSTEMTIME CalendarView::selectedDateLocal() const {
  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(m_year);
  st.wMonth = static_cast<WORD>(m_month);
  st.wDay = static_cast<WORD>(m_selectedDay);
  return st;
}

void CalendarView::setDayMeta(const std::array<CalendarDayMeta, 32>& meta) {
  m_dayMeta = meta;
  invalidate();
}

LRESULT CALLBACK CalendarView::wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<CalendarView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<CalendarView*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) self->m_hwnd = hwnd;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
  return self->wndProc(hwnd, msg, wParam, lParam);
}

LRESULT CalendarView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      ensureFonts();
      recalcLayout();
      return 0;
    case WM_SIZE:
      onSize(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_PAINT:
      onPaint();
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      onLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      return 0;
    case WM_MOUSEWHEEL:
      onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
      return 0;
    case WM_ERASEBKGND:
      return 1; // no flicker
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

void CalendarView::onSize(int, int) {
  recalcLayout();
  invalidate();
}

void CalendarView::onMouseWheel(short delta) {
  if (delta > 0) prevMonth();
  else if (delta < 0) nextMonth();
}

void CalendarView::onLButtonDown(int x, int y) {
  if (ptInRect(m_layout.btnPrev, x, y)) {
    prevMonth();
    return;
  }
  if (ptInRect(m_layout.btnNext, x, y)) {
    nextMonth();
    return;
  }

  if (!ptInRect(m_layout.grid, x, y)) return;

  const int gx = x - m_layout.grid.left;
  const int gy = y - m_layout.grid.top;

  const int col = (m_layout.cellW > 0) ? (gx / m_layout.cellW) : 0;
  const int row = (m_layout.cellH > 0) ? (gy / m_layout.cellH) : 0;
  if (col < 0 || col > 6 || row < 0 || row > 5) return;

  const int first = firstWeekdayMonday0(m_year, m_month);
  const int idx = row * 7 + col;
  const int day = idx - first + 1;
  const int dim = daysInMonth(m_year, m_month);
  if (day < 1 || day > dim) return;

  if (m_selectedDay != day) {
    m_selectedDay = day;
    sendSelectionChanged();
    invalidate();
  }
}

void CalendarView::ensureFonts() {
  // Derive from Segoe UI for premium look
  const int z = m_zoomPercent;

  if (m_fontHeader) DeleteObject(m_fontHeader);
  if (m_fontDay) DeleteObject(m_fontDay);
  if (m_fontSmall) DeleteObject(m_fontSmall);

  LOGFONTW lf{};
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(lf.lfFaceName, L"Segoe UI");

  lf.lfWeight = FW_SEMIBOLD;
  lf.lfHeight = -scale(20, z);
  m_fontHeader = CreateFontIndirectW(&lf);

  lf.lfWeight = FW_MEDIUM;
  lf.lfHeight = -scale(18, z);
  m_fontDay = CreateFontIndirectW(&lf);

  lf.lfWeight = FW_NORMAL;
  lf.lfHeight = -scale(11, z);
  m_fontSmall = CreateFontIndirectW(&lf);
}

void CalendarView::recalcLayout() {
  RECT rc{};
  GetClientRect(m_hwnd, &rc);

  const int z = m_zoomPercent;
  const int pad = scale(12, z);
  const int headerH = scale(kHeaderBaseH, z);
  const int dowH = scale(kDowBaseH, z);

  m_layout.header = rc;
  m_layout.header.bottom = rc.top + headerH;

  const int btnSize = scale(28, z);
  m_layout.btnPrev = { rc.left + pad, rc.top + (headerH - btnSize) / 2, rc.left + pad + btnSize, rc.top + (headerH + btnSize) / 2 };
  m_layout.btnNext = { rc.right - pad - btnSize, rc.top + (headerH - btnSize) / 2, rc.right - pad, rc.top + (headerH + btnSize) / 2 };

  m_layout.title = { m_layout.btnPrev.right + pad, rc.top, m_layout.btnNext.left - pad, rc.top + headerH };

  m_layout.dowRow = { rc.left, m_layout.header.bottom, rc.right, m_layout.header.bottom + dowH };
  m_layout.grid = { rc.left, m_layout.dowRow.bottom, rc.right, rc.bottom };

  const int gridW = std::max(1, static_cast<int>(m_layout.grid.right - m_layout.grid.left));
  const int gridH = std::max(1, static_cast<int>(m_layout.grid.bottom - m_layout.grid.top));
  m_layout.cellW = gridW / 7;
  m_layout.cellH = gridH / 6;
}

void CalendarView::invalidate() {
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

int CalendarView::daysInMonth(int y, int m) const {
  static const int days[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 30;
  int d = days[m];
  const bool leap = (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
  if (m == 2 && leap) d = 29;
  return d;
}

int CalendarView::firstWeekdayMonday0(int y, int m) const {
  // Sakamoto algorithm: returns 0=Sunday..6=Saturday for Gregorian dates
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  int yy = y;
  if (m < 3) yy -= 1;
  const int sun0 = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + 1) % 7;
  // make Monday=0: Mon(1)->0, Tue(2)->1, ..., Sun(0)->6
  return (sun0 + 6) % 7;
}

std::wstring CalendarView::monthTitle() const {
  return monthNameRu(m_month) + L" " + std::to_wstring(m_year);
}

void CalendarView::sendSelectionChanged() {
  if (!m_parent) return;
  SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(m_controlId, kSelCode), reinterpret_cast<LPARAM>(m_hwnd));
}

void CalendarView::sendMonthChanged() {
  if (!m_parent) return;
  SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(m_controlId, kMonthCode), reinterpret_cast<LPARAM>(m_hwnd));
}

void CalendarView::onPaint() {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(m_hwnd, &ps);

  RECT rc{};
  GetClientRect(m_hwnd, &rc);

  // Double buffer
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
  HGDIOBJ oldBmp = SelectObject(mem, bmp);

  // Background with subtle border
  fillRectColor(mem, rc, m_theme.windowBg);
  
  // Header with gradient-like effect
  RECT headerBg = m_layout.header;
  fillRectColor(mem, headerBg, m_theme.headerBg);

  // Header navigation buttons with hover-like appearance
  const int btnRadius = scale(10, m_zoomPercent);
  drawRoundRect(mem, m_layout.btnPrev, btnRadius, m_theme.panelBg, m_theme.gridLine);
  drawRoundRect(mem, m_layout.btnNext, btnRadius, m_theme.panelBg, m_theme.gridLine);

  SelectObject(mem, m_fontHeader);
  drawText(mem, L"◀", m_layout.btnPrev, DT_SINGLELINE | DT_CENTER | DT_VCENTER, m_theme.accent);
  drawText(mem, L"▶", m_layout.btnNext, DT_SINGLELINE | DT_CENTER | DT_VCENTER, m_theme.accent);

  // Month title with larger font
  SelectObject(mem, m_fontHeader);
  drawText(mem, monthTitle(), m_layout.title, DT_SINGLELINE | DT_CENTER | DT_VCENTER, m_theme.text);

  // Weekday row with subtle background
  RECT dowBg = m_layout.dowRow;
  fillRectColor(mem, dowBg, m_theme.headerBg);
  
  static const wchar_t* dows[] = { L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб", L"Вс" };
  SelectObject(mem, m_fontSmall);
  for (int c = 0; c < 7; ++c) {
    RECT r = m_layout.dowRow;
    r.left = m_layout.dowRow.left + c * m_layout.cellW;
    r.right = (c == 6) ? m_layout.dowRow.right : (r.left + m_layout.cellW);
    const COLORREF col = (c >= 5) ? m_theme.weekend : m_theme.mutedText;
    drawText(mem, dows[c], r, DT_SINGLELINE | DT_CENTER | DT_VCENTER, col);
  }

  // Grid cells
  const int first = firstWeekdayMonday0(m_year, m_month);
  const int dim = daysInMonth(m_year, m_month);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  const bool isThisMonthNow = (now.wYear == m_year && now.wMonth == m_month);

  // Grid background
  fillRectColor(mem, m_layout.grid, m_theme.panelBg);

  // Grid lines
  HPEN pen = CreatePen(PS_SOLID, 1, m_theme.gridLine);
  HGDIOBJ oldPen = SelectObject(mem, pen);

  // Horizontal lines
  for (int r = 0; r <= 6; ++r) {
    const int y = m_layout.grid.top + r * m_layout.cellH;
    MoveToEx(mem, m_layout.grid.left, y, nullptr);
    LineTo(mem, m_layout.grid.right, y);
  }
  // Vertical lines
  for (int c = 0; c <= 7; ++c) {
    const int x = m_layout.grid.left + c * m_layout.cellW;
    MoveToEx(mem, x, m_layout.grid.top, nullptr);
    LineTo(mem, x, m_layout.grid.bottom);
  }

  SelectObject(mem, oldPen);
  DeleteObject(pen);

  SelectObject(mem, m_fontDay);
  for (int idx = 0; idx < 42; ++idx) {
    const int row = idx / 7;
    const int col = idx % 7;
    const int day = idx - first + 1;
    if (day < 1 || day > dim) continue;

    RECT cell{};
    cell.left = m_layout.grid.left + col * m_layout.cellW;
    cell.top = m_layout.grid.top + row * m_layout.cellH;
    cell.right = (col == 6) ? m_layout.grid.right : (cell.left + m_layout.cellW);
    cell.bottom = (row == 5) ? m_layout.grid.bottom : (cell.top + m_layout.cellH);

    const bool selected = (day == m_selectedDay);
    const bool today = isThisMonthNow && (day == now.wDay);
    const bool weekend = (col >= 5);

    // Selection background with rounded corners
    if (selected) {
      RECT inner = cell;
      InflateRect(&inner, -scale(4, m_zoomPercent), -scale(4, m_zoomPercent));
      drawRoundRect(mem, inner, scale(12, m_zoomPercent), m_theme.accentSoft, m_theme.accent);
    }

    // Day number - centered at top
    RECT num = cell;
    num.left += scale(8, m_zoomPercent);
    num.top += scale(6, m_zoomPercent);
    num.right -= scale(8, m_zoomPercent);
    num.bottom = num.top + scale(26, m_zoomPercent);

    COLORREF dayColor = weekend ? m_theme.weekend : m_theme.text;
    if (selected) dayColor = m_theme.accent;

    drawText(mem, std::to_wstring(day), num, DT_SINGLELINE | DT_LEFT | DT_VCENTER, dayColor);

    // Today indicator - filled circle behind number
    if (today && !selected) {
      const int circleSize = scale(30, m_zoomPercent);
      RECT ring{};
      ring.left = num.left - scale(3, m_zoomPercent);
      ring.top = num.top - scale(1, m_zoomPercent);
      ring.right = ring.left + circleSize;
      ring.bottom = ring.top + circleSize;
      
      HBRUSH todayBrush = CreateSolidBrush(m_theme.accent);
      HGDIOBJ oldB = SelectObject(mem, todayBrush);
      HPEN todayPen = CreatePen(PS_SOLID, 1, m_theme.accent);
      HGDIOBJ oldP = SelectObject(mem, todayPen);
      Ellipse(mem, ring.left, ring.top, ring.right, ring.bottom);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(todayBrush);
      DeleteObject(todayPen);
      
      // Redraw day number in white on top of circle
      drawText(mem, std::to_wstring(day), num, DT_SINGLELINE | DT_LEFT | DT_VCENTER, RGB(255, 255, 255));
    }

    // Event marker (badge + preview)
    const CalendarDayMeta meta = (day >= 1 && day < static_cast<int>(m_dayMeta.size())) ? m_dayMeta[day] : CalendarDayMeta{};
    const int count = meta.count;
    if (count > 0) {
      COLORREF badgeColor = m_theme.badgeNormal;
      if (meta.maxImportance >= 2) badgeColor = m_theme.badgeUrgent;
      else if (meta.maxImportance == 1) badgeColor = m_theme.badgeImportant;

      // Badge in top-right corner
      RECT badge{};
      badge.right = cell.right - scale(6, m_zoomPercent);
      badge.top = cell.top + scale(6, m_zoomPercent);
      badge.left = badge.right - scale(22, m_zoomPercent);
      badge.bottom = badge.top + scale(18, m_zoomPercent);

      HBRUSH b = CreateSolidBrush(badgeColor);
      HGDIOBJ oldB = SelectObject(mem, b);
      HPEN p = CreatePen(PS_SOLID, 1, badgeColor);
      HGDIOBJ oldP = SelectObject(mem, p);
      RoundRect(mem, badge.left, badge.top, badge.right, badge.bottom, scale(9, m_zoomPercent), scale(9, m_zoomPercent));
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(b);
      DeleteObject(p);

      SelectObject(mem, m_fontSmall);
      drawText(mem, std::to_wstring(count), badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER, RGB(255, 255, 255));
      SelectObject(mem, m_fontDay);

      // Preview text below the day number
      if (!meta.preview.empty()) {
        RECT pr = cell;
        pr.left += scale(6, m_zoomPercent);
        pr.right -= scale(6, m_zoomPercent);
        pr.top = num.bottom + scale(2, m_zoomPercent);
        pr.bottom = cell.bottom - scale(4, m_zoomPercent);
        SelectObject(mem, m_fontSmall);
        drawText(mem, meta.preview, pr, DT_WORDBREAK | DT_END_ELLIPSIS | DT_LEFT, m_theme.mutedText);
        SelectObject(mem, m_fontDay);
      }
    }
  }

  BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);

  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);

  EndPaint(m_hwnd, &ps);
}


