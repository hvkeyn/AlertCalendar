#include "NotificationWindow.h"

#include "core/TimeUtils.h"
#include "model/NoteRepository.h"
#include "settings/AppSettings.h"
#include "win/RichEditUtil.h"
#include "win/MarkupConvert.h"
#include "win/WinUtil.h"

#include <commctrl.h>
#include <richedit.h>

namespace {
constexpr UINT_PTR TIMER_TICK = 1;
constexpr int TICK_MS = 100;

constexpr int IDC_BTN_CLOSE = 50001;
constexpr int IDC_BTN_SNOOZE = 50002;

constexpr int IDM_SNOOZE_5 = 50105;
constexpr int IDM_SNOOZE_10 = 50110;
constexpr int IDM_SNOOZE_30 = 50130;
constexpr int IDM_SNOOZE_60 = 50160;

void setDarkTitleBar(HWND hwnd) {
  // Optional: don't fail if not supported
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return;
  using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
  if (!fn) {
    FreeLibrary(dwm);
    return;
  }
  // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004+), 19 for older
  BOOL on = FALSE;
  fn(hwnd, 20, &on, sizeof(on));
  FreeLibrary(dwm);
}
} // namespace

NotificationWindow::NotificationWindow(HINSTANCE hInstance, Note note, bool previewOnly)
  : m_hInstance(hInstance), m_note(std::move(note)), m_previewOnly(previewOnly) {}

void NotificationWindow::show() {
  const wchar_t* kClassName = L"AlertCalendarNotificationWindow";

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = &NotificationWindow::wndProcThunk;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = sizeof(void*);
  wc.hInstance = m_hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;
  wc.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
  wc.hIconSm = wc.hIcon;

  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return;
  }

  // Construct title based on importance (avoid emoji to prevent missing glyph artifacts)
  std::wstring title = L"Напоминание";
  if (m_note.importance >= 2) title = L"Срочное напоминание!";
  else if (m_note.importance == 1) title = L"Важное напоминание";

  // Modeless: object lifetime controlled via WM_NCDESTROY (delete this)
  m_hwnd = CreateWindowExW(
    WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
    kClassName,
    title.c_str(),
    WS_POPUP | WS_CAPTION | WS_SYSMENU,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    480,
    320,
    nullptr,
    nullptr,
    m_hInstance,
    this
  );

  if (!m_hwnd) {
    delete this;
    return;
  }

  positionBottomRight();
  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(m_hwnd);
}

LRESULT CALLBACK NotificationWindow::wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<NotificationWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<NotificationWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) {
      self->m_hwnd = hwnd;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  if (!self) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  return self->wndProc(hwnd, msg, wParam, lParam);
}

LRESULT NotificationWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      onCreate();
      return 0;
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HBRUSH b = m_bgBrush ? m_bgBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
      FillRect(hdc, &rc, b);
      // Importance stripe (left)
      const int stripe = 6;
      RECT bar = rc;
      bar.right = std::min(rc.right, rc.left + stripe);
      HBRUSH imp = CreateSolidBrush(m_importanceColor);
      FillRect(hdc, &bar, imp);
      DeleteObject(imp);
      return 1;
    }
    case WM_CTLCOLORSTATIC: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, m_theme.text);
      SetBkColor(hdc, m_theme.windowBg);
      return reinterpret_cast<LRESULT>(m_bgBrush);
    }
    case WM_SIZE:
      layout(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_TIMER:
      if (wParam == TIMER_TICK) {
        onTimer();
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_BTN_CLOSE) {
        closeSelf();
        return 0;
      }
      if (LOWORD(wParam) == IDC_BTN_SNOOZE) {
        showSnoozeMenu();
        return 0;
      }
      switch (LOWORD(wParam)) {
        case IDM_SNOOZE_5:
          snoozeMinutes(5);
          return 0;
        case IDM_SNOOZE_10:
          snoozeMinutes(10);
          return 0;
        case IDM_SNOOZE_30:
          snoozeMinutes(30);
          return 0;
        case IDM_SNOOZE_60:
          snoozeMinutes(60);
          return 0;
        default:
          break;
      }
      return 0;
    case WM_CLOSE:
      closeSelf();
      return 0;
    case WM_NCDESTROY:
      delete this;
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

void NotificationWindow::onCreate() {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&icc);

  setDarkTitleBar(m_hwnd);

  RichEditUtil::ensureLoaded();

  const int themeStyle = AppSettings::uiThemeStyle();
  m_theme = UiTheme::fromStyle(themeStyle == 1 ? UiThemeStyle::Minimal : UiThemeStyle::Premium);
  m_importanceColor = m_theme.badgeNormal;
  if (m_note.importance >= 2) m_importanceColor = m_theme.badgeUrgent;
  else if (m_note.importance == 1) m_importanceColor = m_theme.badgeImportant;
  if (m_bgBrush) {
    DeleteObject(m_bgBrush);
    m_bgBrush = nullptr;
  }
  m_bgBrush = CreateSolidBrush(m_theme.windowBg);

  // Create fonts
  m_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  LOGFONTW lf{};
  GetObjectW(m_font, sizeof(lf), &lf);
  lf.lfWeight = FW_SEMIBOLD;
  lf.lfHeight = MulDiv(lf.lfHeight, 120, 100); // 20% larger
  m_fontTitle = CreateFontIndirectW(&lf);

  // Title label (keep simple and readable)
  std::wstring titleText = m_note.title.empty() ? L"(без названия)" : m_note.title;

  m_lblTitle = CreateWindowExW(
    0, L"STATIC", titleText.c_str(),
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 10, 380, 28,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_btnClose = CreateWindowExW(
    0, L"BUTTON", L"Закрыть",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    0, 0, 0, 0,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CLOSE)), m_hInstance, nullptr
  );

  m_btnSnooze = CreateWindowExW(
    0, L"BUTTON", L"Отложить",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    0, 0, 0, 0,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SNOOZE)), m_hInstance, nullptr
  );

  m_rich = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    MSFTEDIT_CLASS,
    L"",
    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
    12, 48, 396, 180,
    m_hwnd,
    nullptr,
    m_hInstance,
    nullptr
  );
  SendMessageW(m_rich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(m_theme.editorBg));

  // Set default text color for RichEdit
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_COLOR;
  cf.crTextColor = m_theme.editorText;
  SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));

  // Show content
  if (m_note.contentMode == NoteContentMode::VisualRtf && !m_note.contentRtf.empty()) {
    RichEditUtil::setRtf(m_rich, m_note.contentRtf);
  } else if (m_note.contentMode == NoteContentMode::Markdown && !m_note.contentMarkdown.empty()) {
    RichEditUtil::setRtf(m_rich, MarkupConvert::markdownToRtf(m_note.contentMarkdown));
  } else if (m_note.contentMode == NoteContentMode::Html && !m_note.contentHtml.empty()) {
    RichEditUtil::setRtf(m_rich, MarkupConvert::htmlToRtf(m_note.contentHtml));
  } else {
    RichEditUtil::setRtf(m_rich, L"{\\rtf1\\ansi\\deff0\\fs22 }");
  }

  m_progress = CreateWindowExW(
    0,
    PROGRESS_CLASSW,
    nullptr,
    WS_CHILD | (m_note.autoHideEnabled ? WS_VISIBLE : 0),
    12,
    240,
    396,
    12,
    m_hwnd,
    nullptr,
    m_hInstance,
    nullptr
  );

  m_lblCountdown = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD | (m_note.autoHideEnabled ? WS_VISIBLE : 0) | SS_CENTER,
    12, 256, 396, 22,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  SendMessageW(m_lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontTitle), TRUE);
  SendMessageW(m_btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnSnooze, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_rich, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblCountdown, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

  if (m_note.autoHideEnabled && m_note.autoHideSeconds > 0) {
    m_totalMs = m_note.autoHideSeconds * 1000;
    SendMessageW(m_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
    SendMessageW(m_progress, PBM_SETPOS, 0, 0);
    updateCountdownUi();
    m_timerId = SetTimer(m_hwnd, TIMER_TICK, TICK_MS, nullptr);
  }
}

void NotificationWindow::onDestroy() {
  if (m_timerId) {
    KillTimer(m_hwnd, m_timerId);
    m_timerId = 0;
  }
  if (m_bgBrush) {
    DeleteObject(m_bgBrush);
    m_bgBrush = nullptr;
  }
  if (m_fontTitle) {
    DeleteObject(m_fontTitle);
    m_fontTitle = nullptr;
  }
}

void NotificationWindow::onTimer() {
  if (m_totalMs <= 0) return;
  m_elapsedMs += TICK_MS;
  if (m_elapsedMs >= m_totalMs) {
    closeSelf();
    return;
  }
  updateCountdownUi();
}

void NotificationWindow::updateCountdownUi() {
  if (m_totalMs <= 0) return;
  const int remainingMs = std::max(0, m_totalMs - m_elapsedMs);
  const int remainingSec = (remainingMs + 999) / 1000;

  wchar_t buf[128]{};
  swprintf_s(buf, L"Окно закроется через %d сек", remainingSec);
  SetWindowTextW(m_lblCountdown, buf);

  const int pos = (m_elapsedMs * 1000) / m_totalMs;
  SendMessageW(m_progress, PBM_SETPOS, pos, 0);
}

void NotificationWindow::showSnoozeMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  AppendMenuW(menu, MF_STRING, IDM_SNOOZE_5, L"Отложить на 5 минут");
  AppendMenuW(menu, MF_STRING, IDM_SNOOZE_10, L"Отложить на 10 минут");
  AppendMenuW(menu, MF_STRING, IDM_SNOOZE_30, L"Отложить на 30 минут");
  AppendMenuW(menu, MF_STRING, IDM_SNOOZE_60, L"Отложить на 1 час");

  RECT rcBtn{};
  GetWindowRect(m_btnSnooze, &rcBtn);
  const int x = rcBtn.left;
  const int y = rcBtn.bottom;

  const int cmd = TrackPopupMenu(
    menu,
    TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
    x,
    y,
    0,
    m_hwnd,
    nullptr
  );

  DestroyMenu(menu);

  if (cmd != 0) {
    SendMessageW(m_hwnd, WM_COMMAND, static_cast<WPARAM>(cmd), 0);
  }
}

void NotificationWindow::snoozeMinutes(int minutes) {
  if (minutes <= 0) return;

  if (m_previewOnly) {
    DestroyWindow(m_hwnd);
    return;
  }

  std::wstring err;
  auto opt = NoteRepository::getById(m_note.id, &err);
  Note n = opt ? *opt : m_note;

  const int64_t now = TimeUtils::unixMsNowUtc();
  n.scheduledAtUtcMs = now + static_cast<int64_t>(minutes) * 60'000;
  n.hasFired = false;
  n.firedAtUtcMs = 0;
  n.dismissed = false;
  n.dismissedAtUtcMs = 0;

  if (!NoteRepository::upsert(std::move(n), &err)) {
    // If snooze failed, don't close: user can try again / close normally.
    if (!err.empty()) {
      MessageBoxW(m_hwnd, err.c_str(), L"Не удалось отложить", MB_ICONERROR);
    }
    return;
  }

  DestroyWindow(m_hwnd);
}

void NotificationWindow::closeSelf() {
  // Mark dismissed (skip in preview mode)
  if (!m_previewOnly) {
    NoteRepository::markDismissed(m_note.id, TimeUtils::unixMsNowUtc(), nullptr);
  }
  DestroyWindow(m_hwnd);
}

void NotificationWindow::layout(int width, int height) {
  const int zoom = AppSettings::uiZoomPercent();
  auto sx = [&](int px) { return MulDiv(px, zoom, 100); };

  const int margin = sx(12);
  const int top = sx(10);
  const int titleH = sx(22);
  const int btnH = sx(28);
  const int btnWClose = sx(90);
  const int btnWSnooze = sx(110);
  const int btnGap = sx(8);

  const int closeX = width - margin - btnWClose;
  const int snoozeX = closeX - btnGap - btnWSnooze;

  MoveWindow(m_lblTitle, margin, top, std::max(sx(140), snoozeX - margin - sx(10)), titleH, TRUE);
  MoveWindow(m_btnSnooze, snoozeX, top - sx(2), btnWSnooze, btnH, TRUE);
  MoveWindow(m_btnClose, closeX, top - sx(2), btnWClose, btnH, TRUE);

  const int richTop = top + titleH + sx(10);
  const int bottomPad = margin;
  const int progressH = m_note.autoHideEnabled ? sx(10) : 0;
  const int labelH = m_note.autoHideEnabled ? sx(18) : 0;
  const int gap = m_note.autoHideEnabled ? sx(8) : 0;

  const int richH = height - richTop - bottomPad - progressH - labelH - gap;
  MoveWindow(m_rich, margin, richTop, width - margin * 2, std::max(50, richH), TRUE);

  if (m_note.autoHideEnabled) {
    const int progY = richTop + std::max(50, richH) + sx(8);
    MoveWindow(m_progress, margin, progY, width - margin * 2, progressH, TRUE);
    MoveWindow(m_lblCountdown, margin, progY + progressH + sx(4), width - margin * 2, labelH, TRUE);
  }
}

void NotificationWindow::positionBottomRight() {
  RECT rc{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);

  const int zoom = AppSettings::uiZoomPercent();
  auto sx = [&](int px) { return MulDiv(px, zoom, 100); };

  // Centered notification (as requested). Size scales with UI zoom.
  const int w = sx(560);
  const int h = sx(380);
  const int cx = rc.left + ((rc.right - rc.left) - w) / 2;
  const int cy = rc.top + ((rc.bottom - rc.top) - h) / 2;

  SetWindowPos(m_hwnd, HWND_TOPMOST, cx, cy, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}


