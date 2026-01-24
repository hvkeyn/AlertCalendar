#include "MainWindow.h"

#include "core/TimeUtils.h"
#include "model/NoteRepository.h"
#include "settings/AppSettings.h"
#include "win/WinUtil.h"
#include "win/NotificationWindow.h"
#include "win/CalendarView.h"
#include "win/RichEditUtil.h"
#include "win/ImageRtf.h"
#include "win/MarkupConvert.h"
#include "win/UiTheme.h"
#include "win/ScheduleImportDialog.h"
#include "win/AppIcon.h"
#include "app/AppPaths.h"

#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <algorithm>
#include <uxtheme.h>
#include <vssym32.h>
#include <shobjidl.h>
#include <filesystem>
#include <iterator>
#include <array>
#include <dwmapi.h>
#include <mmsystem.h>

#pragma comment(lib, "dwmapi.lib")

namespace {
constexpr int IDC_CALENDAR = 1001;
constexpr int IDC_LIST = 1002;
constexpr int IDC_BTN_ADD = 1003;
constexpr int IDC_BTN_REFRESH = 1004;
constexpr int IDC_LBL_ZOOM = 1005;
constexpr int IDC_SLIDER_ZOOM = 1006;
constexpr int IDC_BTN_IMPORT = 1007;
constexpr UINT_PTR TIMER_REMINDERS = 1;

// Editor controls
constexpr int IDC_EDIT_TITLE = 1101;
constexpr int IDC_TIME_PICKER = 1102;
constexpr int IDC_COMBO_IMPORTANCE = 1103;
constexpr int IDC_CHK_AUTOHIDE = 1104;
constexpr int IDC_EDIT_AUTOHIDE_SEC = 1105;
constexpr int IDC_SPIN_AUTOHIDE_SEC = 1106;
constexpr int IDC_BTN_SAVE = 1107;
constexpr int IDC_BTN_DELETE = 1108;
constexpr int IDC_CHK_PREVIEW = 1119;
constexpr int IDC_BTN_BOLD = 1110;
constexpr int IDC_BTN_ITALIC = 1111;
constexpr int IDC_BTN_UNDERLINE = 1112;
constexpr int IDC_BTN_BULLET = 1113;
constexpr int IDC_BTN_IMAGE = 1117;
constexpr int IDC_EDITOR_RICH = 1114;
constexpr int IDC_PREVIEW_RICH = 1120;
constexpr int IDC_CHK_SOUND = 1121;
constexpr int IDC_COMBO_SOUND_NORMAL = 1122;
constexpr int IDC_COMBO_SOUND_IMPORTANT = 1123;
constexpr int IDC_COMBO_SOUND_URGENT = 1124;
constexpr int IDC_BTN_TEST_SOUND = 1125;
constexpr int IDC_BTN_PREVIEW_POPUP = 1126;
constexpr int IDC_COMBO_CATEGORY = 1127;
constexpr int IDC_COMBO_REMINDER = 1128;
constexpr int IDC_COMBO_REPEAT = 1129;
constexpr int IDC_BTN_CLOSE_EDITOR = 1137;
constexpr int IDC_REPEAT_MON = 1130;
constexpr int IDC_REPEAT_TUE = 1131;
constexpr int IDC_REPEAT_WED = 1132;
constexpr int IDC_REPEAT_THU = 1133;
constexpr int IDC_REPEAT_FRI = 1134;
constexpr int IDC_REPEAT_SAT = 1135;
constexpr int IDC_REPEAT_SUN = 1136;

constexpr UINT WM_APP_TRAY = WM_APP + 1;

constexpr int ID_TRAY_OPEN = 40001;
constexpr int ID_TRAY_ADD_TEST = 40002;
constexpr int ID_TRAY_TOGGLE_AUTOSTART = 40003;
constexpr int ID_TRAY_TOGGLE_MINIMIZE_TO_TRAY = 40004;
constexpr int ID_TRAY_EXIT = 40005;
constexpr int ID_TRAY_THEME_PREMIUM = 40006;
constexpr int ID_TRAY_THEME_MINIMAL = 40007;

constexpr UINT_PTR TIMER_AUTOSAVE = 2;
constexpr int AUTOSAVE_DELAY_MS = 800;

constexpr int BTN_STYLE_PRIMARY = 1;
constexpr int BTN_STYLE_NEUTRAL = 2;
constexpr int BTN_STYLE_DANGER = 3;
constexpr int BTN_STYLE_GHOST = 4;

// Reminder combo index <-> minutes conversion
// 0=В начало(0), 1=5мин, 2=10мин, 3=15мин, 4=30мин, 5=1час(60), 6=2часа(120), 7=4часа(240), 8=1день(1440), 9=1неделя(10080)
int reminderComboIndexToMinutes(int idx) {
  switch (idx) {
    case 0: return 0;
    case 1: return 5;
    case 2: return 10;
    case 3: return 15;
    case 4: return 30;
    case 5: return 60;
    case 6: return 120;
    case 7: return 240;
    case 8: return 1440;
    case 9: return 10080;
    default: return 0;
  }
}

int reminderMinutesToComboIndex(int minutes) {
  if (minutes <= 0) return 0;
  if (minutes <= 5) return 1;
  if (minutes <= 10) return 2;
  if (minutes <= 15) return 3;
  if (minutes <= 30) return 4;
  if (minutes <= 60) return 5;
  if (minutes <= 120) return 6;
  if (minutes <= 240) return 7;
  if (minutes <= 1440) return 8;
  return 9;
}

COLORREF blendColor(COLORREF base, COLORREF mix, int alpha) {
  const int inv = 255 - alpha;
  const int r = (GetRValue(base) * inv + GetRValue(mix) * alpha) / 255;
  const int g = (GetGValue(base) * inv + GetGValue(mix) * alpha) / 255;
  const int b = (GetBValue(base) * inv + GetBValue(mix) * alpha) / 255;
  return RGB(r, g, b);
}

COLORREF importanceColor(int importance, const UiTheme& theme) {
  if (importance >= 2) return theme.badgeUrgent;
  if (importance == 1) return theme.badgeImportant;
  return theme.badgeNormal;
}

void drawImportanceIcon(HDC hdc, const RECT& rc, int importance, const UiTheme& theme, bool selected) {
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  const int radius = std::max(3, std::min(w, h) / 4);
  const int cx = rc.left + w / 2;
  const int cy = rc.top + h / 2;
  const COLORREF col = importanceColor(importance, theme);

  HBRUSH bg = CreateSolidBrush(selected ? GetSysColor(COLOR_HIGHLIGHT) : theme.panelBg);
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);

  HBRUSH b = CreateSolidBrush(col);
  HPEN p = CreatePen(PS_SOLID, 1, col);
  HGDIOBJ oldB = SelectObject(hdc, b);
  HGDIOBJ oldP = SelectObject(hdc, p);
  Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
  SelectObject(hdc, oldB);
  SelectObject(hdc, oldP);
  DeleteObject(b);
  DeleteObject(p);

  if (importance >= 1) {
    RECT t = rc;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, L"!", 1, &t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  }
}

void listViewInitColumns(HWND list) {
  ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.pszText = const_cast<wchar_t*>(L"Время");
  col.cx = 80;
  col.iSubItem = 0;
  ListView_InsertColumn(list, 0, &col);

  col.pszText = const_cast<wchar_t*>(L"Важн.");
  col.cx = 70;
  col.iSubItem = 1;
  ListView_InsertColumn(list, 1, &col);

  col.pszText = const_cast<wchar_t*>(L"Заметка");
  col.cx = 600;
  col.iSubItem = 2;
  ListView_InsertColumn(list, 2, &col);
}

std::wstring importanceToText(int importance) {
  switch (importance) {
    case 2: return L"Срочно";
    case 1: return L"Важно";
    default: return L"Обычн.";
  }
}

std::wstring getControlText(HWND hwnd) {
  if (!hwnd) return {};
  const int len = GetWindowTextLengthW(hwnd);
  if (len <= 0) return {};
  std::wstring s;
  s.resize(len);
  GetWindowTextW(hwnd, s.data(), len + 1);
  return s;
}

void setControlText(HWND hwnd, const std::wstring& s) {
  if (!hwnd) return;
  SetWindowTextW(hwnd, s.c_str());
}

int toIntOr(const std::wstring& s, int def) {
  try {
    if (s.empty()) return def;
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

SYSTEMTIME normalizeLocalDate(SYSTEMTIME d) {
  d.wHour = 0;
  d.wMinute = 0;
  d.wSecond = 0;
  d.wMilliseconds = 0;
  FILETIME ft{};
  if (SystemTimeToFileTime(&d, &ft)) {
    SYSTEMTIME out{};
    if (FileTimeToSystemTime(&ft, &out)) {
      return out;
    }
  }
  return d;
}

int weekdayMaskFromWDayOfWeek(WORD dow) {
  switch (dow) {
    case 1: return kRepeatDayMon;
    case 2: return kRepeatDayTue;
    case 3: return kRepeatDayWed;
    case 4: return kRepeatDayThu;
    case 5: return kRepeatDayFri;
    case 6: return kRepeatDaySat;
    case 0: return kRepeatDaySun;
    default: return 0;
  }
}

std::wstring htmlEscape(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() * 2);
  for (wchar_t ch : s) {
    switch (ch) {
      case L'&': out += L"&amp;"; break;
      case L'<': out += L"&lt;"; break;
      case L'>': out += L"&gt;"; break;
      case L'"': out += L"&quot;"; break;
      case L'\'': out += L"&#39;"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::wstring plainToHtml(const std::wstring& plain) {
  std::wstring esc = htmlEscape(plain);
  // normalize line breaks to <br/>
  std::wstring out = L"<p>";
  out.reserve(esc.size() + 16);
  for (size_t i = 0; i < esc.size(); ++i) {
    const wchar_t ch = esc[i];
    if (ch == L'\r') continue;
    if (ch == L'\n') {
      out += L"<br/>\n";
      continue;
    }
    out.push_back(ch);
  }
  out += L"</p>";
  return out;
}

std::wstring stripHtmlToText(const std::wstring& html) {
  std::wstring out;
  out.reserve(html.size());
  bool inTag = false;
  std::wstring tag;
  for (size_t i = 0; i < html.size(); ++i) {
    const wchar_t ch = html[i];
    if (!inTag) {
      if (ch == L'<') {
        inTag = true;
        tag.clear();
      } else {
        out.push_back(ch);
      }
    } else {
      if (ch == L'>') {
        inTag = false;
        // minimal handling of some tags
        auto lower = tag;
        for (auto& c : lower) {
          if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        if (lower.rfind(L"br", 0) == 0 || lower.rfind(L"/p", 0) == 0 || lower.rfind(L"p", 0) == 0 ||
            lower.rfind(L"/li", 0) == 0) {
          out.push_back(L'\n');
        }
      } else {
        if (tag.size() < 32) tag.push_back(ch);
      }
    }
  }
  return out;
}
} // namespace

MainWindow::MainWindow(HINSTANCE hInstance) : m_hInstance(hInstance) {}

MainWindow::~MainWindow() = default;

bool MainWindow::create() {
  const wchar_t* kClassName = L"AlertCalendarMainWindow";

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &MainWindow::wndProcThunk;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = sizeof(void*);
  wc.hInstance = m_hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = wc.hIcon;

  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    MessageBoxW(nullptr, WinUtil::lastErrorMessage().c_str(), L"Ошибка RegisterClassExW", MB_ICONERROR);
    return false;
  }

  m_hwnd = CreateWindowExW(
    0,
    kClassName,
    L"AlertCalendar",
    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
    CW_USEDEFAULT, CW_USEDEFAULT, 1200, 750,
    nullptr,
    nullptr,
    m_hInstance,
    this
  );

  return m_hwnd != nullptr;
}

void MainWindow::show(int nCmdShow) {
  ShowWindow(m_hwnd, nCmdShow);
  UpdateWindow(m_hwnd);
}

LRESULT CALLBACK MainWindow::wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
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

LRESULT MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (!m_hwnd) {
    m_hwnd = hwnd;
  }

  switch (msg) {
    case WM_CREATE:
      m_hwnd = hwnd;
      onCreate();
      return 0;
    case WM_CLOSE:
      if (!m_isQuitting) {
        hideToTray();
        return 0;
      }
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      onDestroy();
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      onSize(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_GETMINMAXINFO: {
      MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize.x = 900;
      mmi->ptMinTrackSize.y = 600;
      return 0;
    }
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      FillRect(hdc, &rc, m_bgBrush ? m_bgBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      return 1;
    }
    case WM_CTLCOLORSTATIC:
      return onCtlColorStatic(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
    case WM_CTLCOLOREDIT:
      return onCtlColorEdit(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
    case WM_CTLCOLORBTN:
      return onCtlColorBtn(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
    case WM_COMMAND:
      // CalendarView sends WM_COMMAND with IDC_CALENDAR on selection/month change
      if (LOWORD(wParam) == IDC_CALENDAR) {
        const int code = HIWORD(wParam);
        if (code == static_cast<int>(CalendarNotify::OpenDay)) {
          if (m_calendarView) {
            m_calendarView->setMode(CalendarViewMode::Day);
          }
          setEditorVisible(false);
          refreshNotesForSelectedDate();
          return 0;
        }
        if (code == static_cast<int>(CalendarNotify::BackToMonth)) {
          if (m_calendarView) {
            m_calendarView->setMode(CalendarViewMode::Month);
          }
          setEditorVisible(false);
          refreshNotesForSelectedDate();
          return 0;
        }
        if (code == static_cast<int>(CalendarNotify::DayAdd)) {
          if (m_calendarView) {
            const int minutes = std::clamp(m_calendarView->dayClickMinutes(), 0, 1439);
            SYSTEMTIME day = selectedDateLocal();
            day.wHour = static_cast<WORD>(minutes / 60);
            day.wMinute = static_cast<WORD>(minutes % 60);
            day.wSecond = 0;
            day.wMilliseconds = 0;

            Note n;
            n.id = WinUtil::guidString();
            n.title = L"Новая встреча";
            n.importance = 0;
            n.category = 0;
            n.reminderMinutesBefore = 0;
            n.repeatType = RepeatType::None;
            n.repeatWeekdaysMask = 0;
            n.contentMode = NoteContentMode::VisualRtf;
            n.contentRtf = "{\\rtf1\\ansi\\deff0\\fs24 }";
            n.autoHideEnabled = false;
            n.autoHideSeconds = 5;
            n.scheduledAtUtcMs = TimeUtils::localSystemTimeToUnixMsUtc(day);

            std::wstring err;
            if (!NoteRepository::upsert(n, &err)) {
              MessageBoxW(m_hwnd, err.c_str(), L"Ошибка сохранения", MB_ICONERROR);
              return 0;
            }
            refreshNotesForSelectedDate();
            loadNoteToEditor(n);
            setEditorVisible(true);
          }
          return 0;
        }
        if (code == static_cast<int>(CalendarNotify::DayEdit)) {
          if (m_calendarView) {
            const std::wstring id = m_calendarView->dayClickNoteId();
            if (!id.empty()) {
              std::wstring err;
              const auto opt = NoteRepository::getById(id, &err);
              if (opt) {
                loadNoteToEditor(*opt);
                setEditorVisible(true);
              } else if (!err.empty()) {
                MessageBoxW(m_hwnd, err.c_str(), L"Ошибка чтения", MB_ICONERROR);
              }
            }
          }
          return 0;
        }
        if (code == static_cast<int>(CalendarNotify::DayDelete)) {
          if (m_calendarView) {
            const std::wstring id = m_calendarView->dayClickNoteId();
            if (!id.empty()) {
              if (MessageBoxW(m_hwnd, L"Удалить выбранную заметку?", L"Подтверждение", MB_ICONWARNING | MB_YESNO) != IDYES) {
                return 0;
              }
              std::wstring err;
              if (!NoteRepository::removeById(id, &err)) {
                if (!err.empty()) {
                  MessageBoxW(m_hwnd, err.c_str(), L"Ошибка удаления", MB_ICONERROR);
                }
                return 0;
              }
              if (m_currentNote && m_currentNote->id == id) {
                m_currentNote.reset();
                clearEditor();
                setEditorVisible(false);
              }
              refreshNotesForSelectedDate();
            }
          }
          return 0;
        }

        flushAutosave();
        refreshNotesForSelectedDate();
        return 0;
      }

      // Change notifications -> autosave
      switch (LOWORD(wParam)) {
        case IDC_EDIT_TITLE:
        case IDC_EDIT_AUTOHIDE_SEC:
        case IDC_EDITOR_RICH:
          if (HIWORD(wParam) == EN_CHANGE) {
            markEditorDirty();
            return 0;
          }
          break;
        case IDC_COMBO_IMPORTANCE:
          if (HIWORD(wParam) == CBN_SELCHANGE) {
            markEditorDirty();
            updateNotificationPreview();
            return 0;
          }
          break;
        case IDC_COMBO_CATEGORY:
          if (HIWORD(wParam) == CBN_SELCHANGE) {
            markEditorDirty();
            return 0;
          }
          break;
        case IDC_COMBO_REMINDER:
          if (HIWORD(wParam) == CBN_SELCHANGE) {
            markEditorDirty();
            return 0;
          }
          break;
        case IDC_COMBO_REPEAT:
          if (HIWORD(wParam) == CBN_SELCHANGE) {
            markEditorDirty();
            updateRepeatUi();
            return 0;
          }
          break;
        case IDC_REPEAT_MON:
        case IDC_REPEAT_TUE:
        case IDC_REPEAT_WED:
        case IDC_REPEAT_THU:
        case IDC_REPEAT_FRI:
        case IDC_REPEAT_SAT:
        case IDC_REPEAT_SUN:
          if (HIWORD(wParam) == BN_CLICKED) {
            markEditorDirty();
            return 0;
          }
          break;
        case IDC_CHK_SOUND:
          if (HIWORD(wParam) == BN_CLICKED) {
            const bool enabled = (SendMessageW(m_chkSound, BM_GETCHECK, 0, 0) == BST_CHECKED);
            AppSettings::setSoundEnabled(enabled);
            refreshSoundUi();
            return 0;
          }
          break;
        case IDC_BTN_TEST_SOUND:
          if (HIWORD(wParam) == BN_CLICKED) {
            const int imp = std::clamp(static_cast<int>(SendMessageW(m_comboImportance, CB_GETCURSEL, 0, 0)), 0, 2);
            playSoundForImportance(imp, true);
            return 0;
          }
          break;
        case IDC_COMBO_SOUND_NORMAL:
        case IDC_COMBO_SOUND_IMPORTANT:
        case IDC_COMBO_SOUND_URGENT:
          if (HIWORD(wParam) == CBN_SELCHANGE) {
            onSoundComboChanged(LOWORD(wParam));
            return 0;
          }
          break;
        default:
          break;
      }

      onCommand(LOWORD(wParam));
      return 0;
    case WM_NOTIFY:
      return onNotify(reinterpret_cast<NMHDR*>(lParam));
    case WM_DRAWITEM:
      if (onDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) {
        return TRUE;
      }
      return FALSE;
    case WM_HSCROLL:
      onHScroll(reinterpret_cast<HWND>(lParam));
      return 0;
    case WM_TIMER:
      if (wParam == TIMER_REMINDERS) {
        checkReminders();
        return 0;
      }
      if (wParam == TIMER_AUTOSAVE) {
        // one-shot debounce
        if (m_autosaveTimerId) {
          KillTimer(m_hwnd, TIMER_AUTOSAVE);
          m_autosaveTimerId = 0;
        }
        // Manual save only: ignore autosave timer.
      }
      return 0;
    case WM_APP_TRAY:
      // callback from tray icon
      if (lParam == WM_LBUTTONDBLCLK) {
        showMainWindowFromTray();
        return 0;
      }
      if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        showTrayMenu();
        return 0;
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

LRESULT MainWindow::onCtlColorStatic(HDC hdc, HWND hwndCtl) {
  if (hwndCtl == m_previewStripe && m_previewStripeBrush) {
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, m_previewStripeColor);
    return reinterpret_cast<LRESULT>(m_previewStripeBrush);
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, m_theme.text);
  SetBkColor(hdc, m_theme.windowBg);
  return reinterpret_cast<LRESULT>(m_bgBrush);
}

LRESULT MainWindow::onCtlColorEdit(HDC hdc, HWND /*hwndCtl*/) {
  SetBkMode(hdc, OPAQUE);
  SetTextColor(hdc, m_theme.editorText);
  SetBkColor(hdc, m_theme.editorBg);
  return reinterpret_cast<LRESULT>(m_editorBrush);
}

LRESULT MainWindow::onCtlColorBtn(HDC hdc, HWND /*hwndCtl*/) {
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, m_theme.text);
  return reinterpret_cast<LRESULT>(m_bgBrush);
}

void MainWindow::onCreate() {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_DATE_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&icc);

  // Initialize theme
  const int s = AppSettings::uiThemeStyle();
  m_theme = UiTheme::fromStyle((s == 1) ? UiThemeStyle::Minimal : UiThemeStyle::Premium);
  recreateBrushes();

  // App icon (generated)
  if (m_appIconLarge) DestroyIcon(m_appIconLarge);
  if (m_appIconSmall) DestroyIcon(m_appIconSmall);
  m_appIconLarge = AppIcon::createIcon(64, m_theme.style);
  m_appIconSmall = AppIcon::createIcon(32, m_theme.style);
  if (m_appIconLarge) {
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_appIconLarge));
  }
  if (m_appIconSmall) {
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_appIconSmall));
  }

  m_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

  // Create bold font for formatting buttons
  LOGFONTW lf{};
  GetObjectW(m_font, sizeof(lf), &lf);
  lf.lfWeight = FW_BOLD;
  m_fontBold = CreateFontIndirectW(&lf);

  // Top toolbar buttons with modern style
  m_btnAdd = CreateWindowExW(
    0, L"BUTTON", L"+ Новая заметка",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    10, 10, 180, 36,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnAdd, BTN_STYLE_PRIMARY);

  m_btnRefresh = CreateWindowExW(
    0, L"BUTTON", L"Обновить",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    200, 10, 110, 36,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REFRESH)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnRefresh, BTN_STYLE_NEUTRAL);

  m_btnImport = CreateWindowExW(
    0, L"BUTTON", L"Импорт + ИИ",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    320, 10, 150, 36,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_IMPORT)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnImport, BTN_STYLE_NEUTRAL);

  // Premium calendar view (custom drawn)
  m_calendarView = std::make_unique<CalendarView>();
  m_calendarView->create(m_hInstance, m_hwnd, IDC_CALENDAR);

  m_list = CreateWindowExW(
    WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
    520, 50, 560, 180,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), m_hInstance, nullptr
  );
  SetWindowTheme(m_list, L"Explorer", nullptr);

  // Editor controls (right panel) with labels
  RichEditUtil::ensureLoaded();

  m_lblTitle = CreateWindowExW(
    0, L"STATIC", L"Заголовок:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    520, 240, 100, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_editTitle = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    520, 260, 560, 28,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_TITLE)),
    m_hInstance,
    nullptr
  );

  m_lblTime = CreateWindowExW(
    0, L"STATIC", L"Время начала:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    520, 294, 60, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_timePicker = CreateWindowExW(
    0,
    DATETIMEPICK_CLASSW,
    nullptr,
    WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT | DTS_UPDOWN,
    580, 292, 100, 28,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TIME_PICKER)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_timePicker, L"Explorer", nullptr);
  SendMessageW(m_timePicker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));

  m_lblImportance = CreateWindowExW(
    0, L"STATIC", L"Важность:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    690, 294, 80, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_comboImportance = CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    770, 292, 130, 200,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_IMPORTANCE)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_comboImportance, L"Explorer", nullptr);
  SendMessageW(m_comboImportance, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Обычная"));
  SendMessageW(m_comboImportance, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Важно"));
  SendMessageW(m_comboImportance, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Срочно"));
  SendMessageW(m_comboImportance, CB_SETCURSEL, 0, 0);

  m_lblCategory = CreateWindowExW(
    0, L"STATIC", L"Категория:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    910, 294, 80, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_comboCategory = CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    995, 292, 130, 200,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_CATEGORY)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_comboCategory, L"Explorer", nullptr);
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Без категории"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Жёлтая"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Зелёная"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Красная"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Лиловая"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Оранжевая"));
  SendMessageW(m_comboCategory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Синяя"));
  SendMessageW(m_comboCategory, CB_SETCURSEL, 0, 0);

  m_lblReminder = CreateWindowExW(
    0, L"STATIC", L"Напомнить за:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    1130, 294, 80, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_comboReminder = CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    1215, 292, 120, 300,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_REMINDER)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_comboReminder, L"Explorer", nullptr);
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Время начала"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"5 мин"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"10 мин"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"15 мин"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 мин"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 час"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2 часа"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4 часа"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 день"));
  SendMessageW(m_comboReminder, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 неделя"));
  SendMessageW(m_comboReminder, CB_SETCURSEL, 0, 0);

  m_lblRepeat = CreateWindowExW(
    0, L"STATIC", L"Повторение:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    520, 294, 90, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_comboRepeat = CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    620, 292, 140, 200,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_REPEAT)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_comboRepeat, L"Explorer", nullptr);
  SendMessageW(m_comboRepeat, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Нет"));
  SendMessageW(m_comboRepeat, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Каждый день"));
  SendMessageW(m_comboRepeat, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Каждую неделю"));
  SendMessageW(m_comboRepeat, CB_SETCURSEL, 0, 0);

  m_lblRepeatDays = CreateWindowExW(
    0, L"STATIC", L"Дни:",
    WS_CHILD | SS_LEFT,
    520, 294, 40, 20,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_chkRepeatMon = CreateWindowExW(
    0, L"BUTTON", L"Пн",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_MON)), m_hInstance, nullptr
  );
  m_chkRepeatTue = CreateWindowExW(
    0, L"BUTTON", L"Вт",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_TUE)), m_hInstance, nullptr
  );
  m_chkRepeatWed = CreateWindowExW(
    0, L"BUTTON", L"Ср",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_WED)), m_hInstance, nullptr
  );
  m_chkRepeatThu = CreateWindowExW(
    0, L"BUTTON", L"Чт",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_THU)), m_hInstance, nullptr
  );
  m_chkRepeatFri = CreateWindowExW(
    0, L"BUTTON", L"Пт",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_FRI)), m_hInstance, nullptr
  );
  m_chkRepeatSat = CreateWindowExW(
    0, L"BUTTON", L"Сб",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_SAT)), m_hInstance, nullptr
  );
  m_chkRepeatSun = CreateWindowExW(
    0, L"BUTTON", L"Вс",
    WS_CHILD | BS_AUTOCHECKBOX,
    520, 294, 34, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REPEAT_SUN)), m_hInstance, nullptr
  );

  m_chkAutoHide = CreateWindowExW(
    0,
    L"BUTTON",
    L"Автоскрытие:",
    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
    910, 294, 130, 26,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_AUTOHIDE)),
    m_hInstance,
    nullptr
  );

  m_editAutoHideSeconds = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"5",
    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
    1045, 292, 50, 28,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_AUTOHIDE_SEC)),
    m_hInstance,
    nullptr
  );

  m_spinAutoHideSeconds = CreateWindowExW(
    0,
    UPDOWN_CLASSW,
    nullptr,
    WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS,
    0, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPIN_AUTOHIDE_SEC)),
    m_hInstance,
    nullptr
  );
  SendMessageW(m_spinAutoHideSeconds, UDM_SETBUDDY, reinterpret_cast<WPARAM>(m_editAutoHideSeconds), 0);
  SendMessageW(m_spinAutoHideSeconds, UDM_SETRANGE32, 1, 3600);

  m_btnSave = CreateWindowExW(
    0, L"BUTTON", L"Сохранить",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    520, 328, 120, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnSave, BTN_STYLE_PRIMARY);

  m_btnDelete = CreateWindowExW(
    0, L"BUTTON", L"Удалить",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    650, 328, 100, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_DELETE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnDelete, BTN_STYLE_DANGER);

  m_btnCloseEditor = CreateWindowExW(
    0, L"BUTTON", L"Закрыть",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    760, 328, 100, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CLOSE_EDITOR)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnCloseEditor, BTN_STYLE_GHOST);

  // Preview checkbox removed - preview is always visible now
  m_chkPreview = nullptr;

  m_btnPreviewPopup = CreateWindowExW(
    0,
    L"BUTTON",
    L"Превью",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    520, 366, 120, 24,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_PREVIEW_POPUP)),
    m_hInstance,
    nullptr
  );
  registerButtonStyle(m_btnPreviewPopup, BTN_STYLE_NEUTRAL);

  m_previewLabel = CreateWindowExW(
    0, L"STATIC", L"Предпросмотр (как в окне уведомления):",
    WS_CHILD,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_previewStripe = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_previewTitle = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_previewClose = CreateWindowExW(
    0, L"BUTTON", L"Закрыть",
    WS_CHILD | WS_DISABLED | BS_PUSHBUTTON,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_previewSnooze = CreateWindowExW(
    0, L"BUTTON", L"Отложить",
    WS_CHILD | WS_DISABLED | BS_PUSHBUTTON,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_previewRich = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    MSFTEDIT_CLASS,
    L"",
    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PREVIEW_RICH)),
    m_hInstance,
    nullptr
  );

  m_previewProgress = CreateWindowExW(
    0,
    PROGRESS_CLASSW,
    nullptr,
    WS_CHILD,
    520, 0, 0, 0,
    m_hwnd,
    nullptr,
    m_hInstance,
    nullptr
  );

  m_previewCountdown = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  ShowWindow(m_previewLabel, SW_HIDE);
  ShowWindow(m_previewStripe, SW_HIDE);
  ShowWindow(m_previewTitle, SW_HIDE);
  ShowWindow(m_previewClose, SW_HIDE);
  ShowWindow(m_previewSnooze, SW_HIDE);
  ShowWindow(m_previewRich, SW_HIDE);
  ShowWindow(m_previewProgress, SW_HIDE);
  ShowWindow(m_previewCountdown, SW_HIDE);

  // Sound settings (global)
  m_chkSound = CreateWindowExW(
    0,
    L"BUTTON",
    L"Звук",
    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_SOUND)),
    m_hInstance,
    nullptr
  );

  m_lblSound = CreateWindowExW(
    0, L"STATIC", L"Сигналы:",
    WS_CHILD | WS_VISIBLE,
    520, 0, 0, 0,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_lblSoundNormal = CreateWindowExW(0, L"STATIC", L"Обычн.", WS_CHILD | WS_VISIBLE, 520, 0, 0, 0, m_hwnd, nullptr, m_hInstance, nullptr);
  m_lblSoundImportant = CreateWindowExW(0, L"STATIC", L"Важно", WS_CHILD | WS_VISIBLE, 520, 0, 0, 0, m_hwnd, nullptr, m_hInstance, nullptr);
  m_lblSoundUrgent = CreateWindowExW(0, L"STATIC", L"Срочно", WS_CHILD | WS_VISIBLE, 520, 0, 0, 0, m_hwnd, nullptr, m_hInstance, nullptr);

  m_comboSoundNormal = CreateWindowExW(
    0, WC_COMBOBOXW, nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_SOUND_NORMAL)),
    m_hInstance,
    nullptr
  );
  m_comboSoundImportant = CreateWindowExW(
    0, WC_COMBOBOXW, nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_SOUND_IMPORTANT)),
    m_hInstance,
    nullptr
  );
  m_comboSoundUrgent = CreateWindowExW(
    0, WC_COMBOBOXW, nullptr,
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_SOUND_URGENT)),
    m_hInstance,
    nullptr
  );

  SetWindowTheme(m_comboSoundNormal, L"Explorer", nullptr);
  SetWindowTheme(m_comboSoundImportant, L"Explorer", nullptr);
  SetWindowTheme(m_comboSoundUrgent, L"Explorer", nullptr);

  m_btnTestSound = CreateWindowExW(
    0,
    L"BUTTON",
    L"Проверить звук",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    520, 0, 0, 0,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_TEST_SOUND)),
    m_hInstance,
    nullptr
  );
  SetWindowTheme(m_btnTestSound, L"Explorer", nullptr);
  registerButtonStyle(m_btnTestSound, BTN_STYLE_NEUTRAL);

  // No mode tabs: single WYSIWYG editor

  // Formatting toolbar with bold font
  m_btnBold = CreateWindowExW(
    0, L"BUTTON", L"B",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    520, 410, 36, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BOLD)), m_hInstance, nullptr
  );
  m_btnItalic = CreateWindowExW(
    0, L"BUTTON", L"I",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    560, 410, 36, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ITALIC)), m_hInstance, nullptr
  );
  m_btnUnderline = CreateWindowExW(
    0, L"BUTTON", L"U",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    600, 410, 36, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_UNDERLINE)), m_hInstance, nullptr
  );
  m_btnBullet = CreateWindowExW(
    0, L"BUTTON", L"•",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    640, 410, 36, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BULLET)), m_hInstance, nullptr
  );
  m_btnImage = CreateWindowExW(
    0, L"BUTTON", L"IMG",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    680, 410, 44, 32,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_IMAGE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnBold, BTN_STYLE_GHOST);
  registerButtonStyle(m_btnItalic, BTN_STYLE_GHOST);
  registerButtonStyle(m_btnUnderline, BTN_STYLE_GHOST);
  registerButtonStyle(m_btnBullet, BTN_STYLE_GHOST);
  registerButtonStyle(m_btnImage, BTN_STYLE_GHOST);

  m_editorRich = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    MSFTEDIT_CLASS,
    L"",
    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
    520, 450, 560, 220,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDITOR_RICH)),
    m_hInstance,
    nullptr
  );
  // Keep editor in rich-text mode
  SendMessageW(m_editorRich, EM_SETTEXTMODE, TM_RICHTEXT, 0);

  m_lblZoom = CreateWindowExW(
    0, L"STATIC", L"Масштаб: 100%",
    WS_CHILD | WS_VISIBLE | SS_RIGHT,
    420, 16, 160, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_ZOOM)), m_hInstance, nullptr
  );

  m_sliderZoom = CreateWindowExW(
    0, TRACKBAR_CLASSW, L"",
    WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
    600, 12, 180, 30,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SLIDER_ZOOM)), m_hInstance, nullptr
  );
  SetWindowTheme(m_sliderZoom, L"Explorer", nullptr);

  // Apply fonts
  SendMessageW(m_btnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnRefresh, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnImport, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblZoom, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_list, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblTime, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_timePicker, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblImportance, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboImportance, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblCategory, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboCategory, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblReminder, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboReminder, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_chkAutoHide, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editAutoHideSeconds, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnSave, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnDelete, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnCloseEditor, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnPreviewPopup, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_previewLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_previewTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontBold), TRUE);
  SendMessageW(m_previewClose, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_previewSnooze, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnBold, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontBold), TRUE);
  SendMessageW(m_btnItalic, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnUnderline, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnBullet, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnImage, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editorRich, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_previewRich, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_previewCountdown, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

  SetWindowTheme(m_previewClose, L"Explorer", nullptr);
  SetWindowTheme(m_previewSnooze, L"Explorer", nullptr);
  SetWindowTheme(m_previewProgress, L"Explorer", nullptr);

  SendMessageW(m_chkSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblSoundNormal, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblSoundImportant, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblSoundUrgent, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboSoundNormal, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboSoundImportant, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_comboSoundUrgent, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnTestSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

  // Single editor only

  // Zoom slider range and value
  SendMessageW(m_sliderZoom, TBM_SETRANGE, TRUE, MAKELONG(50, 200));
  SendMessageW(m_sliderZoom, TBM_SETTICFREQ, 10, 0);
  SendMessageW(m_sliderZoom, TBM_SETPAGESIZE, 0, 10);

  listViewInitColumns(m_list);

  initTray();

  m_timerId = SetTimer(m_hwnd, TIMER_REMINDERS, 1000, nullptr);

  // Auto-scale UI to current DPI on first run (keeps manual zoom if user changed it).
  {
    const int savedZoom = AppSettings::uiZoomPercent();
    if (savedZoom == 100) {
      const UINT dpi = GetDpiForWindow(m_hwnd);
      int autoZoom = static_cast<int>((dpi * 100) / 96);
      autoZoom = std::clamp(autoZoom, 50, 250);
      if (autoZoom != savedZoom) {
        AppSettings::setUiZoomPercent(autoZoom);
      }
    }
  }

  applyUiZoom();
  applyUiTheme();
  refreshSoundUi();
  clearEditor();
  updateAutoHideEnabled();
  refreshNotesForSelectedDate();
  setEditorVisible(false);
}

void MainWindow::onDestroy() {
  if (m_timerId) {
    KillTimer(m_hwnd, m_timerId);
    m_timerId = 0;
  }
  if (m_autosaveTimerId) {
    KillTimer(m_hwnd, TIMER_AUTOSAVE);
    m_autosaveTimerId = 0;
  }

  removeTray();
  if (m_trayMenu) {
    DestroyMenu(m_trayMenu);
    m_trayMenu = nullptr;
  }

  if (m_fontOwned) {
    DeleteObject(m_fontOwned);
    m_fontOwned = nullptr;
  }
  if (m_fontBold) {
    DeleteObject(m_fontBold);
    m_fontBold = nullptr;
  }
  if (m_appIconLarge) {
    DestroyIcon(m_appIconLarge);
    m_appIconLarge = nullptr;
  }
  if (m_appIconSmall) {
    DestroyIcon(m_appIconSmall);
    m_appIconSmall = nullptr;
  }
  if (m_bgBrush) {
    DeleteObject(m_bgBrush);
    m_bgBrush = nullptr;
  }
  if (m_panelBrush) {
    DeleteObject(m_panelBrush);
    m_panelBrush = nullptr;
  }
  if (m_editorBrush) {
    DeleteObject(m_editorBrush);
    m_editorBrush = nullptr;
  }
  if (m_previewStripeBrush) {
    DeleteObject(m_previewStripeBrush);
    m_previewStripeBrush = nullptr;
  }
}

void MainWindow::recreateBrushes() {
  if (m_bgBrush) DeleteObject(m_bgBrush);
  if (m_panelBrush) DeleteObject(m_panelBrush);
  if (m_editorBrush) DeleteObject(m_editorBrush);
  
  m_bgBrush = CreateSolidBrush(m_theme.windowBg);
  m_panelBrush = CreateSolidBrush(m_theme.panelBg);
  m_editorBrush = CreateSolidBrush(m_theme.editorBg);
}

void MainWindow::onSize(int width, int height) {
  const int zoom = AppSettings::uiZoomPercent();
  auto sx = [&](int px) { return MulDiv(px, zoom, 100); };

  const int margin = sx(12);
  const int top = sx(56);
  const int btnH = sx(36);
  const int btnY = sx(10);
  const int gap = sx(10);

  // Top toolbar
  int x = margin;
  MoveWindow(m_btnAdd, x, btnY, sx(170), btnH, TRUE);
  x += sx(170) + gap;
  MoveWindow(m_btnRefresh, x, btnY, sx(110), btnH, TRUE);
  x += sx(110) + gap;
  MoveWindow(m_btnImport, x, btnY, sx(150), btnH, TRUE);

  // Zoom controls on the right
  const int sliderW = sx(160);
  const int labelW = sx(150);
  const int sliderX = std::max(margin, width - margin - sliderW);
  const int labelX = std::max(margin, sliderX - gap - labelW);
  MoveWindow(m_lblZoom, labelX, btnY + sx(6), labelW, sx(24), TRUE);
  MoveWindow(m_sliderZoom, sliderX, btnY, sliderW, btnH, TRUE);

  const int usableH = height - top - margin;
  const int usableW = width - margin * 2;
 
  if (!m_editorVisible) {
    if (m_calendarView && m_calendarView->hwnd()) {
      MoveWindow(m_calendarView->hwnd(), margin, top, usableW, usableH, TRUE);
    }
    return;
  }

  // Left side: calendar (45% width)
  const int leftW = std::max(sx(320), (usableW * 45) / 100);
  const int rightW = usableW - leftW - margin;
  const int rightX = margin + leftW + margin;

  if (m_calendarView && m_calendarView->hwnd()) {
    MoveWindow(m_calendarView->hwnd(), margin, top, leftW, usableH, TRUE);
  }

  // Editor panel layout (list hidden in calendar-focused UX)
  MoveWindow(m_list, 0, 0, 0, 0, TRUE);

  int y = top;
  const int fieldH = sx(28);
  const int labelH = sx(20);

  // Title row
  MoveWindow(m_lblTitle, rightX, y, sx(100), labelH, TRUE);
  y += labelH + sx(2);
  MoveWindow(m_editTitle, rightX, y, rightW, fieldH, TRUE);
  y += fieldH + gap;

  // Row 1: Начало + Важность + Категория
  const int timeW = sx(90);
  const int impW = sx(95);
  const int catW = sx(95);
  const int remW = sx(120);
  const int repW = sx(150);
  const int secW = sx(45);
  const int autoHideW = sx(100);
  const int lblW = sx(90);

  int rx = rightX;
  MoveWindow(m_lblTime, rx, y + sx(4), lblW, labelH, TRUE);
  rx += lblW;
  MoveWindow(m_timePicker, rx, y, timeW, fieldH, TRUE);
  rx += timeW + gap;

  MoveWindow(m_lblImportance, rx, y + sx(4), lblW, labelH, TRUE);
  rx += lblW;
  MoveWindow(m_comboImportance, rx, y, impW, fieldH * 6, TRUE);
  rx += impW + gap;

  MoveWindow(m_lblCategory, rx, y + sx(4), lblW, labelH, TRUE);
  rx += lblW;
  MoveWindow(m_comboCategory, rx, y, catW, fieldH * 6, TRUE);
  y += fieldH + gap;

  // Row 2: Напомнить + Повторение
  rx = rightX;
  MoveWindow(m_lblReminder, rx, y + sx(4), lblW, labelH, TRUE);
  rx += lblW;
  MoveWindow(m_comboReminder, rx, y, remW, fieldH * 10, TRUE);
  rx += remW + gap;

  MoveWindow(m_lblRepeat, rx, y + sx(4), lblW, labelH, TRUE);
  rx += lblW;
  MoveWindow(m_comboRepeat, rx, y, repW, fieldH * 6, TRUE);
  y += fieldH + gap;

  // Row 3: Дни недели (только для еженедельных)
  const int repeatSel = static_cast<int>(SendMessageW(m_comboRepeat, CB_GETCURSEL, 0, 0));
  const bool showWeekdays = (repeatSel == static_cast<int>(RepeatType::Weekly));
  const int dayW = sx(32);
  const int dayGap = sx(2);

  if (showWeekdays) {
    ShowWindow(m_lblRepeatDays, SW_SHOW);
    ShowWindow(m_chkRepeatMon, SW_SHOW);
    ShowWindow(m_chkRepeatTue, SW_SHOW);
    ShowWindow(m_chkRepeatWed, SW_SHOW);
    ShowWindow(m_chkRepeatThu, SW_SHOW);
    ShowWindow(m_chkRepeatFri, SW_SHOW);
    ShowWindow(m_chkRepeatSat, SW_SHOW);
    ShowWindow(m_chkRepeatSun, SW_SHOW);

    rx = rightX;
    MoveWindow(m_lblRepeatDays, rx, y + sx(4), sx(40), labelH, TRUE);
    rx += sx(40);
    MoveWindow(m_chkRepeatMon, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatTue, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatWed, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatThu, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatFri, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatSat, rx, y, dayW, fieldH, TRUE); rx += dayW + dayGap;
    MoveWindow(m_chkRepeatSun, rx, y, dayW, fieldH, TRUE);
    y += fieldH + gap;
  } else {
    ShowWindow(m_lblRepeatDays, SW_HIDE);
    ShowWindow(m_chkRepeatMon, SW_HIDE);
    ShowWindow(m_chkRepeatTue, SW_HIDE);
    ShowWindow(m_chkRepeatWed, SW_HIDE);
    ShowWindow(m_chkRepeatThu, SW_HIDE);
    ShowWindow(m_chkRepeatFri, SW_HIDE);
    ShowWindow(m_chkRepeatSat, SW_HIDE);
    ShowWindow(m_chkRepeatSun, SW_HIDE);
  }

  // Row 4: Автоскрытие
  rx = rightX;
  MoveWindow(m_chkAutoHide, rx, y + sx(2), autoHideW, fieldH, TRUE);
  rx += autoHideW + sx(4);
  MoveWindow(m_editAutoHideSeconds, rx, y, secW, fieldH, TRUE);
  if (m_spinAutoHideSeconds) {
    const int spinW = std::max(sx(14), GetSystemMetrics(SM_CXVSCROLL));
    const int spinX = rx + secW - spinW;
    MoveWindow(m_spinAutoHideSeconds, spinX, y, spinW, fieldH, TRUE);
  }
  y += fieldH + gap;

  // Save/Delete buttons
  const int btnW = sx(120);
  const int delW = sx(100);
  const int closeW = sx(100);
  MoveWindow(m_btnSave, rightX, y, btnW, sx(32), TRUE);
  MoveWindow(m_btnDelete, rightX + btnW + gap, y, delW, sx(32), TRUE);
  MoveWindow(m_btnCloseEditor, rightX + btnW + gap + delW + gap, y, closeW, sx(32), TRUE);
  y += sx(32) + sx(6);

  // Preview popup button (checkbox removed - preview always visible)
  const int previewBtnW = sx(130);
  MoveWindow(m_btnPreviewPopup, rightX, y, previewBtnW, sx(24), TRUE);
  y += sx(24) + sx(2);

  // Sound settings
  const int soundLabelH = sx(18);
  const int soundLeft = rightX;
  const int soundBlockX = rightX + sx(80);
  const int soundBlockW = std::max(sx(180), rightW - sx(80));

  MoveWindow(m_chkSound, soundLeft, y, sx(70), sx(24), TRUE);
  const int testW = sx(140);
  MoveWindow(m_lblSound, soundBlockX, y + sx(4), std::max(sx(120), soundBlockW - testW - gap), soundLabelH, TRUE);
  MoveWindow(m_btnTestSound, rightX + rightW - testW, y, testW, sx(24), TRUE);
  y += sx(24);

  const int comboGap = gap;
  const int comboW = std::max(sx(90), (soundBlockW - comboGap * 2) / 3);
  const int dropH = fieldH * 7;

  MoveWindow(m_lblSoundNormal, soundBlockX, y, comboW, soundLabelH, TRUE);
  MoveWindow(m_lblSoundImportant, soundBlockX + comboW + comboGap, y, comboW, soundLabelH, TRUE);
  MoveWindow(m_lblSoundUrgent, soundBlockX + (comboW + comboGap) * 2, y, comboW, soundLabelH, TRUE);
  y += soundLabelH;

  MoveWindow(m_comboSoundNormal, soundBlockX, y, comboW, dropH, TRUE);
  MoveWindow(m_comboSoundImportant, soundBlockX + comboW + comboGap, y, comboW, dropH, TRUE);
  MoveWindow(m_comboSoundUrgent, soundBlockX + (comboW + comboGap) * 2, y, comboW, dropH, TRUE);
  y += fieldH + gap;

  // Formatting toolbar
  const int toolH = sx(32);
  const int toolBtnW = sx(36);
  MoveWindow(m_btnBold, rightX, y, toolBtnW, toolH, TRUE);
  MoveWindow(m_btnItalic, rightX + toolBtnW + sx(4), y, toolBtnW, toolH, TRUE);
  MoveWindow(m_btnUnderline, rightX + (toolBtnW + sx(4)) * 2, y, toolBtnW, toolH, TRUE);
  MoveWindow(m_btnBullet, rightX + (toolBtnW + sx(4)) * 3, y, toolBtnW, toolH, TRUE);
  MoveWindow(m_btnImage, rightX + (toolBtnW + sx(4)) * 4, y, sx(44), toolH, TRUE);
  y += toolH + sx(6);

  // Editor takes full remaining height (inline preview removed)
  const int totalH = std::max(sx(120), height - y - margin);
  MoveWindow(m_editorRich, rightX, y, rightW, totalH, TRUE);

  // Hide inline preview elements (removed from UI)
  ShowWindow(m_previewLabel, SW_HIDE);
  ShowWindow(m_previewStripe, SW_HIDE);
  ShowWindow(m_previewTitle, SW_HIDE);
  ShowWindow(m_previewClose, SW_HIDE);
  ShowWindow(m_previewSnooze, SW_HIDE);
  ShowWindow(m_previewRich, SW_HIDE);
  ShowWindow(m_previewProgress, SW_HIDE);
  ShowWindow(m_previewCountdown, SW_HIDE);

  // Comfortable inner padding (more iPad-like, easier to read)
  const int pad = sx(10);
  const LPARAM lr = MAKELONG(pad, pad);
  SendMessageW(m_editTitle, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, lr);
  SendMessageW(m_editAutoHideSeconds, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, lr);
  SendMessageW(m_editorRich, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, lr);
  SendMessageW(m_previewRich, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, lr);
}

void MainWindow::onCommand(int id) {
  switch (id) {
    case IDC_BTN_ADD:
      addNewNote();
      return;
    case IDC_BTN_REFRESH:
      flushAutosave();
      refreshNotesForSelectedDate();
      return;
    case IDC_BTN_IMPORT: {
      flushAutosave();
      ScheduleImportDialog dlg(m_hInstance, m_hwnd);
      if (dlg.showModal()) {
        refreshNotesForSelectedDate();
        if (m_currentNote) {
          if (!NoteRepository::getById(m_currentNote->id, nullptr)) {
            m_currentNote.reset();
            clearEditor();
          }
        }
      }
      return;
    }
    case IDC_BTN_SAVE:
      flushAutosave();
      saveEditorToNote(true);
      return;
    case IDC_BTN_DELETE:
      flushAutosave();
      deleteCurrentNote();
      return;
    case IDC_BTN_CLOSE_EDITOR:
      flushAutosave();
      setEditorVisible(false);
      return;
    case IDC_BTN_BOLD:
      RichEditUtil::toggleBold(m_editorRich);
      markEditorDirty();
      return;
    case IDC_BTN_ITALIC:
      RichEditUtil::toggleItalic(m_editorRich);
      markEditorDirty();
      return;
    case IDC_BTN_UNDERLINE:
      RichEditUtil::toggleUnderline(m_editorRich);
      markEditorDirty();
      return;
    case IDC_BTN_BULLET:
      RichEditUtil::toggleBullet(m_editorRich);
      markEditorDirty();
      return;
    case IDC_BTN_IMAGE:
      insertImageIntoRich();
      return;
    case IDC_CHK_AUTOHIDE: {
      updateAutoHideEnabled();
      markEditorDirty();
      updateNotificationPreview();
      // Force layout update for preview
      RECT rc{};
      GetClientRect(m_hwnd, &rc);
      onSize(rc.right - rc.left, rc.bottom - rc.top);
      return;
    }
    case IDC_BTN_PREVIEW_POPUP:
      showNotificationPreviewPopup();
      return;
    case ID_TRAY_OPEN:
      showMainWindowFromTray();
      return;
    case ID_TRAY_ADD_TEST:
      addTestNote();
      return;
    case ID_TRAY_TOGGLE_AUTOSTART: {
      const bool enabled = AppSettings::autostartEnabled();
      AppSettings::setAutostartEnabled(!enabled);
      return;
    }
    case ID_TRAY_TOGGLE_MINIMIZE_TO_TRAY: {
      const bool enabled = AppSettings::minimizeToTray();
      AppSettings::setMinimizeToTray(!enabled);
      return;
    }
    case ID_TRAY_THEME_PREMIUM:
      AppSettings::setUiThemeStyle(0);
      applyUiTheme();
      return;
    case ID_TRAY_THEME_MINIMAL:
      AppSettings::setUiThemeStyle(1);
      applyUiTheme();
      return;
    case ID_TRAY_EXIT:
      m_isQuitting = true;
      removeTray();
      DestroyWindow(m_hwnd);
      return;
    default:
      return;
  }
}

LRESULT MainWindow::onNotify(NMHDR* hdr) {
  if (!hdr) return 0;
  if (m_refreshingList) return 0;

  if (hdr->idFrom == IDC_LIST && hdr->code == NM_CUSTOMDRAW) {
    auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
    switch (cd->nmcd.dwDrawStage) {
      case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
      case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
      case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        const int item = static_cast<int>(cd->nmcd.dwItemSpec);
        const int sub = cd->iSubItem;
        const bool selected = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;

        if (sub == 1) {
          RECT rc{};
          if (ListView_GetSubItemRect(m_list, item, sub, LVIR_BOUNDS, &rc)) {
            const int importance = (item >= 0 && item < static_cast<int>(m_listNoteMeta.size()))
                                     ? m_listNoteMeta[static_cast<size_t>(item)].importance
                                     : 0;
            drawImportanceIcon(cd->nmcd.hdc, rc, importance, m_theme, selected);
          }
          return CDRF_SKIPDEFAULT;
        }

        if (sub == 0 && !selected && m_listIsToday) {
          if (item >= 0 && item < static_cast<int>(m_listNoteMeta.size())) {
            const int64_t nowUtc = TimeUtils::unixMsNowUtc();
            const int64_t ts = m_listNoteMeta[static_cast<size_t>(item)].scheduledAtUtcMs;
            const int64_t diff = ts - nowUtc;
            if (diff < 0) {
              cd->clrText = m_theme.mutedText;
            } else if (diff <= 30 * 60 * 1000LL) {
              cd->clrText = m_theme.badgeUrgent;
            } else if (diff <= 2 * 60 * 60 * 1000LL) {
              cd->clrText = m_theme.badgeImportant;
            }
          }
        }
        return CDRF_NEWFONT;
      }
      default:
        break;
    }
  }

  if (hdr->idFrom == IDC_LIST && hdr->code == LVN_ITEMCHANGED) {
    if (!m_editorVisible) return 0;
    auto* nmlv = reinterpret_cast<NMLISTVIEW*>(hdr);
    if ((nmlv->uChanged & LVIF_STATE) != 0) {
      const bool nowSelected = (nmlv->uNewState & LVIS_SELECTED) != 0;
      const bool wasSelected = (nmlv->uOldState & LVIS_SELECTED) != 0;
      if (nowSelected && !wasSelected) {
        flushAutosave();
        const int idx = nmlv->iItem;
        if (idx >= 0 && idx < static_cast<int>(m_listNoteIds.size())) {
          const auto opt = NoteRepository::getById(m_listNoteIds[static_cast<size_t>(idx)], nullptr);
          if (opt) {
            loadNoteToEditor(*opt);
          }
        }
      }
    }
    return 0;
  }

  if (hdr->idFrom == IDC_TIME_PICKER && hdr->code == DTN_DATETIMECHANGE) {
    markEditorDirty();
    return 0;
  }
  return 0;
}

void MainWindow::onHScroll(HWND src) {
  if (src != m_sliderZoom) return;
  const int pos = static_cast<int>(SendMessageW(m_sliderZoom, TBM_GETPOS, 0, 0));
  AppSettings::setUiZoomPercent(pos);
  applyUiZoom();
  RECT rc{};
  GetClientRect(m_hwnd, &rc);
  onSize(rc.right - rc.left, rc.bottom - rc.top);
}

SYSTEMTIME MainWindow::selectedDateLocal() const {
  if (m_calendarView) {
    return m_calendarView->selectedDateLocal();
  }
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return st;
}

void MainWindow::refreshNotesForSelectedDate() {
  m_refreshingList = true;
  const SYSTEMTIME day = selectedDateLocal();
  SYSTEMTIME now{};
  GetLocalTime(&now);
  m_listIsToday = (day.wYear == now.wYear && day.wMonth == now.wMonth && day.wDay == now.wDay);

  std::wstring err;
  const auto notes = NoteRepository::listForDate(day, &err);
  if (!err.empty()) {
    MessageBoxW(m_hwnd, err.c_str(), L"Ошибка чтения заметок", MB_ICONERROR);
  }

  if (m_calendarView) {
    m_calendarView->setDayNotes(notes);
  }

  ListView_DeleteAllItems(m_list);
  m_listNoteIds.clear();
  m_listNoteMeta.clear();

  const std::wstring keepId = (m_currentNote ? m_currentNote->id : L"");

  int i = 0;
  for (const auto& n : notes) {
    m_listNoteIds.push_back(n.id);
    m_listNoteMeta.push_back({ n.scheduledAtUtcMs, n.importance });

    SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
    const std::wstring t = WinUtil::formatHHMM(stLocal);
    const std::wstring imp = importanceToText(n.importance);

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(t.c_str());
    ListView_InsertItem(m_list, &item);

    ListView_SetItemText(m_list, i, 1, const_cast<wchar_t*>(imp.c_str()));
    const std::wstring title = n.title.empty() ? L"(без названия)" : n.title;
    ListView_SetItemText(m_list, i, 2, const_cast<wchar_t*>(title.c_str()));

    ++i;
  }

  // Update calendar markers for currently displayed month
  if (m_calendarView) {
    std::wstring err2;
    const auto meta = NoteRepository::monthMeta(m_calendarView->year(), m_calendarView->month(), &err2);
    (void)err2;
    m_calendarView->setDayMeta(meta);
  }

  // Restore selection if possible
  int selectIdx = -1;
  if (!keepId.empty()) {
    for (size_t k = 0; k < m_listNoteIds.size(); ++k) {
      if (m_listNoteIds[k] == keepId) {
        selectIdx = static_cast<int>(k);
        break;
      }
    }
  }
  if (selectIdx < 0 && !m_listNoteIds.empty()) {
    selectIdx = 0;
  }

  if (selectIdx >= 0) {
    ListView_SetItemState(m_list, selectIdx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(m_list, selectIdx, FALSE);
  }

  m_refreshingList = false;

  if (!m_editorVisible) {
    m_currentNote.reset();
    clearEditor();
  }
}

// (tabs removed) single WYSIWYG editor is always visible

void MainWindow::clearEditor() {
  m_loadingEditor = true;
  setControlText(m_editTitle, L"");
  SendMessageW(m_comboImportance, CB_SETCURSEL, 0, 0);
  SendMessageW(m_comboCategory, CB_SETCURSEL, 0, 0);
  SendMessageW(m_comboReminder, CB_SETCURSEL, 0, 0);
  SendMessageW(m_comboRepeat, CB_SETCURSEL, 0, 0);
  setRepeatMaskUi(0);
  SendMessageW(m_chkAutoHide, BM_SETCHECK, BST_UNCHECKED, 0);
  setControlText(m_editAutoHideSeconds, L"5");
  updateAutoHideEnabled();

  // reset time picker to today 09:00 on selected date
  SYSTEMTIME day = selectedDateLocal();
  day.wHour = 9;
  day.wMinute = 0;
  day.wSecond = 0;
  day.wMilliseconds = 0;
  SendMessageW(m_timePicker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&day));

  // Clear content (single editor)
  RichEditUtil::setRtfW(m_editorRich, L"{\\rtf1\\ansi\\deff0\\fs24 }");

  m_editorDirty = false;
  updateSaveButtonState();
  m_loadingEditor = false;
  updateRepeatUi();
}

void MainWindow::loadNoteToEditor(const Note& note) {
  m_loadingEditor = true;
  m_currentNote = note;

  setControlText(m_editTitle, note.title);
  SendMessageW(m_comboImportance, CB_SETCURSEL, note.importance, 0);
  SendMessageW(m_comboCategory, CB_SETCURSEL, std::clamp(note.category, 0, 6), 0);
  SendMessageW(m_comboReminder, CB_SETCURSEL, reminderMinutesToComboIndex(note.reminderMinutesBefore), 0);
  SendMessageW(m_comboRepeat, CB_SETCURSEL, static_cast<int>(note.repeatType), 0);
  setRepeatMaskUi(note.repeatWeekdaysMask);
  SendMessageW(m_chkAutoHide, BM_SETCHECK, note.autoHideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
  setControlText(m_editAutoHideSeconds, std::to_wstring(std::max(1, note.autoHideSeconds)));
  updateAutoHideEnabled();

  // set time picker to note's local date/time
  SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(note.scheduledAtUtcMs);
  SendMessageW(m_timePicker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&stLocal));

  // Load content into WYSIWYG editor.
  if (!note.contentRtf.empty()) {
    RichEditUtil::setRtfBytes(m_editorRich, note.contentRtf);
  } else if (!note.contentMarkdown.empty()) {
    RichEditUtil::setRtfW(m_editorRich, MarkupConvert::markdownToRtf(note.contentMarkdown));
  } else if (!note.contentHtml.empty()) {
    RichEditUtil::setRtfW(m_editorRich, MarkupConvert::htmlToRtf(note.contentHtml));
  } else {
    RichEditUtil::setRtfW(m_editorRich, L"{\\rtf1\\ansi\\deff0\\fs24 }");
  }

  m_loadingEditor = false;
  m_editorDirty = false;
  updateSaveButtonState();
  updateRepeatUi();
  updateNotificationPreview();
}

void MainWindow::addNewNote() {
  Note n;
  n.id = WinUtil::guidString();
  n.title = L"";
  n.importance = 0;
  n.contentMode = NoteContentMode::VisualRtf;
  n.contentRtf = "{\\rtf1\\ansi\\deff0\\fs24 }";

  // Default time: +1 hour from now (so reminder doesn't fire immediately)
  const int64_t nowMs = TimeUtils::unixMsNowUtc();
  n.scheduledAtUtcMs = nowMs + 60 * 60 * 1000; // +1 hour

  n.autoHideEnabled = false;
  n.autoHideSeconds = 5;

  std::wstring err;
  if (!NoteRepository::upsert(n, &err)) {
    MessageBoxW(m_hwnd, err.c_str(), L"Ошибка сохранения", MB_ICONERROR);
    return;
  }

  refreshNotesForSelectedDate();
  loadNoteToEditor(n);
  setEditorVisible(true);
}

void MainWindow::saveEditorToNote(bool refreshAfter) {
  if (m_loadingEditor) return;

  const int64_t prevScheduled = m_currentNote ? m_currentNote->scheduledAtUtcMs : 0;

  Note n = m_currentNote.value_or(Note{});
  if (n.id.empty()) {
    n.id = WinUtil::guidString();
  }

  n.title = getControlText(m_editTitle);

  const int imp = static_cast<int>(SendMessageW(m_comboImportance, CB_GETCURSEL, 0, 0));
  n.importance = std::clamp(imp, 0, 2);

  const int cat = static_cast<int>(SendMessageW(m_comboCategory, CB_GETCURSEL, 0, 0));
  n.category = std::clamp(cat, 0, 6);

  const int reminderIdx = static_cast<int>(SendMessageW(m_comboReminder, CB_GETCURSEL, 0, 0));
  n.reminderMinutesBefore = reminderComboIndexToMinutes(reminderIdx);

  const int repeatIdx = static_cast<int>(SendMessageW(m_comboRepeat, CB_GETCURSEL, 0, 0));
  n.repeatType = static_cast<RepeatType>(std::clamp(repeatIdx, 0, 2));
  int repeatMask = repeatMaskFromUi();
  if (n.repeatType == RepeatType::Weekly) {
    if (repeatMask == 0) {
      repeatMask = defaultRepeatMaskForSelectedDate();
      setRepeatMaskUi(repeatMask);
    }
  } else {
    repeatMask = 0;
  }
  n.repeatWeekdaysMask = repeatMask;

  n.autoHideEnabled = (SendMessageW(m_chkAutoHide, BM_GETCHECK, 0, 0) == BST_CHECKED);
  n.autoHideSeconds = std::clamp(toIntOr(getControlText(m_editAutoHideSeconds), 5), 1, 3600);

  // schedule time (take selected date + picker time)
  SYSTEMTIME t{};
  const LRESULT gdt = SendMessageW(m_timePicker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&t));
  SYSTEMTIME day = selectedDateLocal();
  if (gdt != GDT_VALID) {
    day.wHour = 9;
    day.wMinute = 0;
    day.wSecond = 0;
    day.wMilliseconds = 0;
    n.scheduledAtUtcMs = TimeUtils::localSystemTimeToUnixMsUtc(day);
  } else {
    t.wYear = day.wYear;
    t.wMonth = day.wMonth;
    t.wDay = day.wDay;
    n.scheduledAtUtcMs = TimeUtils::localSystemTimeToUnixMsUtc(t);
  }

  // Preserve fire/dismiss state unless user moved the event to a new future time.
  const bool scheduledChanged = (m_currentNote && prevScheduled != 0 && prevScheduled != n.scheduledAtUtcMs);
  const int64_t nowMs = TimeUtils::unixMsNowUtc();
  if (scheduledChanged && n.scheduledAtUtcMs > nowMs) {
    n.hasFired = false;
    n.firedAtUtcMs = 0;
    n.dismissed = false;
    n.dismissedAtUtcMs = 0;
  } else if (m_currentNote && !n.id.empty()) {
    if (const auto stored = NoteRepository::getById(n.id, nullptr)) {
      if (stored->hasFired) {
        n.hasFired = stored->hasFired;
        n.firedAtUtcMs = stored->firedAtUtcMs;
      }
      if (stored->dismissed) {
        n.dismissed = stored->dismissed;
        n.dismissedAtUtcMs = stored->dismissedAtUtcMs;
      }
    }
  }

  // Single WYSIWYG editor: always store RTF.
  n.contentMode = NoteContentMode::VisualRtf;
  n.contentRtf = RichEditUtil::getRtfBytes(m_editorRich);

  // If scheduled time changed, we should refresh list to keep ordering correct.
  if (m_currentNote && prevScheduled != 0 && prevScheduled != n.scheduledAtUtcMs) {
    refreshAfter = true;
  }

  std::wstring err;
  if (!NoteRepository::upsert(n, &err)) {
    MessageBoxW(m_hwnd, err.c_str(), L"Ошибка сохранения", MB_ICONERROR);
    return;
  }

  m_currentNote = n;
  m_editorDirty = false;
  updateSaveButtonState();
  updateNotificationPreview();

  // Update calendar meta (badges/preview)
  if (m_calendarView) {
    std::wstring err2;
    const auto meta = NoteRepository::monthMeta(m_calendarView->year(), m_calendarView->month(), &err2);
    (void)err2;
    m_calendarView->setDayMeta(meta);
  }

  if (refreshAfter) {
    refreshNotesForSelectedDate();
    return;
  }

  // Update list row in-place if present
  for (size_t i = 0; i < m_listNoteIds.size(); ++i) {
    if (m_listNoteIds[i] == n.id) {
      SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
      const std::wstring timeText = WinUtil::formatHHMM(stLocal);
      const std::wstring impText = importanceToText(n.importance);
      const std::wstring title = n.title.empty() ? L"(без названия)" : n.title;
      ListView_SetItemText(m_list, static_cast<int>(i), 0, const_cast<wchar_t*>(timeText.c_str()));
      ListView_SetItemText(m_list, static_cast<int>(i), 1, const_cast<wchar_t*>(impText.c_str()));
      ListView_SetItemText(m_list, static_cast<int>(i), 2, const_cast<wchar_t*>(title.c_str()));
      if (i < m_listNoteMeta.size()) {
        m_listNoteMeta[i] = { n.scheduledAtUtcMs, n.importance };
      }
      return;
    }
  }

  // New note: refresh list to show it
  refreshNotesForSelectedDate();
}

void MainWindow::deleteCurrentNote() {
  if (!m_currentNote) return;
  const std::wstring id = m_currentNote->id;

  if (MessageBoxW(m_hwnd, L"Удалить выбранную заметку?", L"Подтверждение", MB_ICONWARNING | MB_YESNO) != IDYES) {
    return;
  }

  std::wstring err;
  if (!NoteRepository::removeById(id, &err)) {
    MessageBoxW(m_hwnd, err.c_str(), L"Ошибка удаления", MB_ICONERROR);
    return;
  }

  m_currentNote.reset();
  clearEditor();
  refreshNotesForSelectedDate();
}

void MainWindow::updateAutoHideEnabled() {
  const bool enabled = (SendMessageW(m_chkAutoHide, BM_GETCHECK, 0, 0) == BST_CHECKED);
  EnableWindow(m_editAutoHideSeconds, enabled);
  EnableWindow(m_spinAutoHideSeconds, enabled);
}

int MainWindow::repeatMaskFromUi() const {
  int mask = 0;
  if (SendMessageW(m_chkRepeatMon, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDayMon;
  if (SendMessageW(m_chkRepeatTue, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDayTue;
  if (SendMessageW(m_chkRepeatWed, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDayWed;
  if (SendMessageW(m_chkRepeatThu, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDayThu;
  if (SendMessageW(m_chkRepeatFri, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDayFri;
  if (SendMessageW(m_chkRepeatSat, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDaySat;
  if (SendMessageW(m_chkRepeatSun, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= kRepeatDaySun;
  return mask;
}

void MainWindow::setRepeatMaskUi(int mask) {
  SendMessageW(m_chkRepeatMon, BM_SETCHECK, (mask & kRepeatDayMon) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatTue, BM_SETCHECK, (mask & kRepeatDayTue) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatWed, BM_SETCHECK, (mask & kRepeatDayWed) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatThu, BM_SETCHECK, (mask & kRepeatDayThu) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatFri, BM_SETCHECK, (mask & kRepeatDayFri) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatSat, BM_SETCHECK, (mask & kRepeatDaySat) ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(m_chkRepeatSun, BM_SETCHECK, (mask & kRepeatDaySun) ? BST_CHECKED : BST_UNCHECKED, 0);
}

int MainWindow::defaultRepeatMaskForSelectedDate() const {
  SYSTEMTIME day = normalizeLocalDate(selectedDateLocal());
  return weekdayMaskFromWDayOfWeek(day.wDayOfWeek);
}

void MainWindow::updateRepeatUi() {
  if (!m_comboRepeat) return;
  const int sel = static_cast<int>(SendMessageW(m_comboRepeat, CB_GETCURSEL, 0, 0));
  if (sel == static_cast<int>(RepeatType::Weekly)) {
    if (repeatMaskFromUi() == 0) {
      setRepeatMaskUi(defaultRepeatMaskForSelectedDate());
    }
  }
  RECT rc{};
  GetClientRect(m_hwnd, &rc);
  onSize(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::setEditorVisible(bool visible) {
  m_editorVisible = visible;
  auto show = [&](HWND h, bool v) {
    if (!h) return;
    ShowWindow(h, v ? SW_SHOW : SW_HIDE);
  };

  // List is hidden in calendar-focused UX.
  show(m_list, false);

  show(m_lblTitle, visible);
  show(m_editTitle, visible);
  show(m_lblTime, visible);
  show(m_timePicker, visible);
  show(m_lblImportance, visible);
  show(m_comboImportance, visible);
  show(m_lblCategory, visible);
  show(m_comboCategory, visible);
  show(m_lblReminder, visible);
  show(m_comboReminder, visible);
  show(m_lblRepeat, visible);
  show(m_comboRepeat, visible);
  show(m_lblRepeatDays, visible);
  show(m_chkRepeatMon, visible);
  show(m_chkRepeatTue, visible);
  show(m_chkRepeatWed, visible);
  show(m_chkRepeatThu, visible);
  show(m_chkRepeatFri, visible);
  show(m_chkRepeatSat, visible);
  show(m_chkRepeatSun, visible);
  show(m_chkAutoHide, visible);
  show(m_editAutoHideSeconds, visible);
  show(m_spinAutoHideSeconds, visible);
  show(m_btnSave, visible);
  show(m_btnDelete, visible);
  show(m_btnCloseEditor, visible);
  show(m_btnPreviewPopup, visible);
  show(m_chkSound, visible);
  show(m_lblSound, visible);
  show(m_lblSoundNormal, visible);
  show(m_lblSoundImportant, visible);
  show(m_lblSoundUrgent, visible);
  show(m_comboSoundNormal, visible);
  show(m_comboSoundImportant, visible);
  show(m_comboSoundUrgent, visible);
  show(m_btnTestSound, visible);
  show(m_btnBold, visible);
  show(m_btnItalic, visible);
  show(m_btnUnderline, visible);
  show(m_btnBullet, visible);
  show(m_btnImage, visible);
  show(m_editorRich, visible);

  // Inline preview always hidden (legacy)
  show(m_previewLabel, false);
  show(m_previewStripe, false);
  show(m_previewTitle, false);
  show(m_previewClose, false);
  show(m_previewSnooze, false);
  show(m_previewRich, false);
  show(m_previewProgress, false);
  show(m_previewCountdown, false);

  RECT rc{};
  GetClientRect(m_hwnd, &rc);
  onSize(rc.right - rc.left, rc.bottom - rc.top);
  updateSaveButtonState();
}

void MainWindow::updateSaveButtonState() {
  if (!m_btnSave) return;
  const BOOL enable = m_editorVisible && m_editorDirty;
  EnableWindow(m_btnSave, enable);
}

void MainWindow::registerButtonStyle(HWND hwnd, int style) {
  if (!hwnd) return;
  m_buttonStyles[hwnd] = style;
  LONG_PTR s = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if ((s & BS_OWNERDRAW) == 0) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, s | BS_OWNERDRAW);
  }
}

LRESULT MainWindow::onDrawItem(DRAWITEMSTRUCT* dis) {
  if (!dis) return FALSE;
  const auto it = m_buttonStyles.find(dis->hwndItem);
  if (it == m_buttonStyles.end()) return FALSE;

  const int style = it->second;
  const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
  const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

  COLORREF bg = m_theme.panelBg;
  COLORREF border = m_theme.gridLine;
  COLORREF text = m_theme.text;

  if (style == BTN_STYLE_PRIMARY) {
    bg = m_theme.accent;
    border = m_theme.accent;
    text = RGB(255, 255, 255);
  } else if (style == BTN_STYLE_DANGER) {
    bg = m_theme.badgeUrgent;
    border = m_theme.badgeUrgent;
    text = RGB(255, 255, 255);
  } else if (style == BTN_STYLE_GHOST) {
    bg = m_theme.windowBg;
    border = m_theme.gridLine;
    text = m_theme.text;
  }

  if (pressed) {
    bg = blendColor(bg, RGB(0, 0, 0), 25);
  }
  if (disabled) {
    bg = blendColor(bg, RGB(255, 255, 255), 120);
    text = m_theme.mutedText;
    border = blendColor(border, RGB(255, 255, 255), 120);
  }

  HDC hdc = dis->hDC;
  RECT rc = dis->rcItem;
  const int radius = MulDiv(8, AppSettings::uiZoomPercent(), 100);

  HBRUSH b = CreateSolidBrush(bg);
  HPEN p = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldB = SelectObject(hdc, b);
  HGDIOBJ oldP = SelectObject(hdc, p);
  RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
  SelectObject(hdc, oldB);
  SelectObject(hdc, oldP);
  DeleteObject(b);
  DeleteObject(p);

  std::wstring textBuf = getControlText(dis->hwndItem);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, text);
  HFONT useFont = m_fontOwned ? m_fontOwned : m_font;
  if (dis->hwndItem == m_btnBold) {
    useFont = m_fontBold ? m_fontBold : useFont;
  }
  HGDIOBJ oldF = SelectObject(hdc, useFont);
  DrawTextW(hdc, textBuf.c_str(), static_cast<int>(textBuf.size()), &rc,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  SelectObject(hdc, oldF);

  if ((dis->itemState & ODS_FOCUS) != 0) {
    RECT focus = rc;
    InflateRect(&focus, -2, -2);
    DrawFocusRect(hdc, &focus);
  }
  return TRUE;
}

std::wstring MainWindow::openImageFileDialog() {
  // Modern Windows file picker (premium UX)
  std::wstring result;

  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool doUninit = SUCCEEDED(hr);

  IFileOpenDialog* dlg = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    if (doUninit) CoUninitialize();
    return {};
  }

  std::unique_ptr<IFileOpenDialog, void (*)(IFileOpenDialog*)> dlgGuard(dlg, [](IFileOpenDialog* d) { d->Release(); });

  const COMDLG_FILTERSPEC filters[] = {
    { L"Изображения", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif" },
    { L"PNG", L"*.png" },
    { L"JPEG", L"*.jpg;*.jpeg" },
    { L"Все файлы", L"*.*" }
  };
  dlg->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
  dlg->SetTitle(L"Вставить изображение");

  hr = dlg->Show(m_hwnd);
  if (SUCCEEDED(hr)) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && item) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }

  if (doUninit) CoUninitialize();
  return result;
}

std::wstring MainWindow::openSoundFileDialog() {
  std::wstring result;

  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool doUninit = SUCCEEDED(hr);

  IFileOpenDialog* dlg = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    if (doUninit) CoUninitialize();
    return {};
  }

  std::unique_ptr<IFileOpenDialog, void (*)(IFileOpenDialog*)> dlgGuard(dlg, [](IFileOpenDialog* d) { d->Release(); });

  const COMDLG_FILTERSPEC filters[] = {
    { L"Звуки WAV", L"*.wav" },
    { L"Все файлы", L"*.*" }
  };
  dlg->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
  dlg->SetTitle(L"Выбрать звук (WAV)");

  hr = dlg->Show(m_hwnd);
  if (SUCCEEDED(hr)) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && item) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }

  if (doUninit) CoUninitialize();
  return result;
}

void MainWindow::refreshSoundUi() {
  if (!m_chkSound) return;

  const bool enabled = AppSettings::soundEnabled();
  SendMessageW(m_chkSound, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);

  auto isFileValue = [](const std::wstring& v) -> bool {
    if (v.empty()) return false;
    if (v.find(L"\\") != std::wstring::npos || v.find(L"/") != std::wstring::npos) return true;
    if (v.size() >= 4) {
      const std::wstring ext = v.substr(v.size() - 4);
      if (ext == L".wav" || ext == L".WAV") return true;
    }
    return false;
  };

  auto fileLabel = [&](const std::wstring& path) -> std::wstring {
    if (!isFileValue(path)) return L"WAV...";
    try {
      const std::filesystem::path p(path);
      return L"WAV: " + p.filename().wstring();
    } catch (...) {
      return L"WAV: файл";
    }
  };

  auto fillCombo = [&](HWND combo, const std::wstring& value) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Сигнал"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Предупр."));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ошибка"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"По умолч."));
    const std::wstring wavItem = fileLabel(value);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wavItem.c_str()));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Выкл"));

    int sel = 0;
    if (value.empty()) sel = 5;
    else if (value == L"SystemAsterisk") sel = 0;
    else if (value == L"SystemExclamation") sel = 1;
    else if (value == L"SystemHand") sel = 2;
    else if (value == L"SystemDefault") sel = 3;
    else sel = 4; // file
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
  };

  fillCombo(m_comboSoundNormal, AppSettings::soundNormal());
  fillCombo(m_comboSoundImportant, AppSettings::soundImportant());
  fillCombo(m_comboSoundUrgent, AppSettings::soundUrgent());

  EnableWindow(m_comboSoundNormal, enabled);
  EnableWindow(m_comboSoundImportant, enabled);
  EnableWindow(m_comboSoundUrgent, enabled);
  EnableWindow(m_lblSoundNormal, enabled);
  EnableWindow(m_lblSoundImportant, enabled);
  EnableWindow(m_lblSoundUrgent, enabled);
  EnableWindow(m_lblSound, enabled);
  EnableWindow(m_btnTestSound, enabled);
}

void MainWindow::onSoundComboChanged(int controlId) {
  if (!AppSettings::soundEnabled()) {
    refreshSoundUi();
    return;
  }

  HWND combo = nullptr;
  std::wstring current;
  void (*setter)(const std::wstring&) = nullptr;

  if (controlId == IDC_COMBO_SOUND_NORMAL) {
    combo = m_comboSoundNormal;
    current = AppSettings::soundNormal();
    setter = &AppSettings::setSoundNormal;
  } else if (controlId == IDC_COMBO_SOUND_IMPORTANT) {
    combo = m_comboSoundImportant;
    current = AppSettings::soundImportant();
    setter = &AppSettings::setSoundImportant;
  } else if (controlId == IDC_COMBO_SOUND_URGENT) {
    combo = m_comboSoundUrgent;
    current = AppSettings::soundUrgent();
    setter = &AppSettings::setSoundUrgent;
  }

  if (!combo || !setter) return;

  const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  std::wstring newVal = current;
  if (sel == 0) newVal = L"SystemAsterisk";
  else if (sel == 1) newVal = L"SystemExclamation";
  else if (sel == 2) newVal = L"SystemHand";
  else if (sel == 3) newVal = L"SystemDefault";
  else if (sel == 5) newVal.clear();
  else if (sel == 4) {
    const std::wstring path = openSoundFileDialog();
    if (!path.empty()) {
      newVal = path;
    }
  }

  setter(newVal);
  refreshSoundUi();
}

void MainWindow::playSoundForImportance(int importance, bool showErrors) {
  if (!AppSettings::soundEnabled()) {
    if (showErrors) {
      MessageBoxW(m_hwnd, L"Звук выключен.", L"AlertCalendar", MB_ICONINFORMATION);
    }
    return;
  }

  std::wstring sound;
  if (importance >= 2) sound = AppSettings::soundUrgent();
  else if (importance == 1) sound = AppSettings::soundImportant();
  else sound = AppSettings::soundNormal();

  if (sound.empty()) {
    if (showErrors) {
      MessageBoxW(m_hwnd, L"Звук для этого уровня важности выключен.", L"AlertCalendar", MB_ICONINFORMATION);
    }
    return;
  }

  auto isFileValue = [](const std::wstring& v) -> bool {
    if (v.empty()) return false;
    if (v.find(L"\\") != std::wstring::npos || v.find(L"/") != std::wstring::npos) return true;
    if (v.size() >= 4) {
      const std::wstring ext = v.substr(v.size() - 4);
      if (ext == L".wav" || ext == L".WAV") return true;
    }
    return false;
  };

  DWORD flags = SND_ASYNC | SND_NODEFAULT;
  flags |= isFileValue(sound) ? SND_FILENAME : SND_ALIAS;
  if (!PlaySoundW(sound.c_str(), nullptr, flags)) {
    if (showErrors) {
      MessageBoxW(m_hwnd, L"Не удалось воспроизвести выбранный звук.", L"AlertCalendar", MB_ICONWARNING);
    } else {
      const UINT beep = (importance >= 2) ? MB_ICONHAND : ((importance == 1) ? MB_ICONEXCLAMATION : MB_OK);
      MessageBeep(beep);
    }
  }
}

void MainWindow::showNotificationPreviewPopup() {
  // Build a preview Note from current editor state (no persistence side-effects).
  Note n;
  n.id = (m_currentNote && !m_currentNote->id.empty()) ? m_currentNote->id : L"__preview__";
  n.title = getControlText(m_editTitle);
  n.importance = std::clamp(static_cast<int>(SendMessageW(m_comboImportance, CB_GETCURSEL, 0, 0)), 0, 2);
  n.contentMode = NoteContentMode::VisualRtf;
  n.contentRtf.clear();
  n.autoHideEnabled = (SendMessageW(m_chkAutoHide, BM_GETCHECK, 0, 0) == BST_CHECKED);
  n.autoHideSeconds = std::clamp(toIntOr(getControlText(m_editAutoHideSeconds), 5), 1, 3600);
  n.scheduledAtUtcMs = TimeUtils::unixMsNowUtc();

  HWND sourceRich = (m_editorRich && IsWindow(m_editorRich)) ? m_editorRich : nullptr;
  if (!sourceRich && m_currentNote) {
    n.contentRtf = m_currentNote->contentRtf;
  }

  auto* w = new NotificationWindow(m_hInstance, n, /*previewOnly*/ true, sourceRich);
  w->show();
}

void MainWindow::insertImageIntoRich() {
  // Ensure we have a note id to place media under
  if (!m_currentNote || m_currentNote->id.empty()) {
    addNewNote();
  }
  if (!m_currentNote) return;

  const std::wstring src = openImageFileDialog();
  if (src.empty()) return;

  // Copy to note media folder (storage)
  try {
    namespace fs = std::filesystem;
    const fs::path srcPath(src);
    std::wstring ext = srcPath.extension().wstring();
    if (ext.empty()) ext = L".png";
    const fs::path dstDir = AppPaths::noteMediaDir(m_currentNote->id);
    const fs::path dst = dstDir / (WinUtil::guidString() + ext);
    fs::copy_file(srcPath, dst, fs::copy_options::overwrite_existing);
  } catch (...) {
    // non-fatal (RTF will still embed bytes)
  }

  // Compute max width based on current editor client width
  RECT rc{};
  GetClientRect(m_editorRich, &rc);
  const int editorW = static_cast<int>(rc.right - rc.left);
  const int maxW = std::max(200, editorW - 40);

  std::wstring err;
  const std::wstring rtf = ImageRtf::makePngPictRtfFromFile(src, maxW, &err);
  if (rtf.empty()) {
    if (!err.empty()) {
      MessageBoxW(m_hwnd, err.c_str(), L"Не удалось вставить изображение", MB_ICONERROR);
    }
    return;
  }

  SetFocus(m_editorRich);
  // Convert generated RTF (wstring) to bytes (ASCII-safe; ImageRtf produces ASCII/hex)
  std::string rtfBytes;
  rtfBytes.reserve(rtf.size());
  for (wchar_t wc : rtf) rtfBytes.push_back(static_cast<char>(wc & 0xFF));
  RichEditUtil::insertRtfAtSelectionBytes(m_editorRich, rtfBytes);
  markEditorDirty();
}

// Preview removed: WYSIWYG editor is the single source of truth.

void MainWindow::markEditorDirty() {
  if (m_loadingEditor) return;
  if (!m_editorDirty) {
    m_editorDirty = true;
    updateSaveButtonState();
  }
}

void MainWindow::scheduleAutosave() {
  if (m_autosaveTimerId) {
    KillTimer(m_hwnd, TIMER_AUTOSAVE);
    m_autosaveTimerId = 0;
  }
  // Manual save only: autosave disabled.
}

void MainWindow::flushAutosave() {
  if (m_autosaveTimerId) {
    KillTimer(m_hwnd, TIMER_AUTOSAVE);
    m_autosaveTimerId = 0;
  }
}

void MainWindow::addTestNote() {
  Note n;
  n.id = WinUtil::guidString();
  n.title = L"Тестовое напоминание";
  n.importance = 1;
  n.contentMode = NoteContentMode::Markdown;
  n.contentMarkdown = L"**AlertCalendar**: тестовая заметка (пока без визуального редактора).";
  n.autoHideEnabled = false;
  n.autoHideSeconds = 0;

  const int64_t now = TimeUtils::unixMsNowUtc();
  n.scheduledAtUtcMs = now + 10'000; // через 10 секунд
  n.createdAtUtcMs = now;
  n.updatedAtUtcMs = now;

  std::wstring err;
  if (!NoteRepository::upsert(n, &err)) {
    MessageBoxW(m_hwnd, err.c_str(), L"Ошибка сохранения", MB_ICONERROR);
    return;
  }

  refreshNotesForSelectedDate();
}

void MainWindow::checkReminders() {
  const int64_t now = TimeUtils::unixMsNowUtc();
  std::wstring err;
  const auto due = NoteRepository::listDue(now, 20, &err);
  if (!err.empty()) {
    return;
  }

  if (!due.empty() && AppSettings::soundEnabled()) {
    int maxImp = 0;
    for (const auto& n : due) {
      maxImp = std::max(maxImp, n.importance);
    }

    std::wstring sound;
    if (maxImp >= 2) sound = AppSettings::soundUrgent();
    else if (maxImp == 1) sound = AppSettings::soundImportant();
    else sound = AppSettings::soundNormal();

    auto isFileValue = [](const std::wstring& v) -> bool {
      if (v.empty()) return false;
      if (v.find(L"\\") != std::wstring::npos || v.find(L"/") != std::wstring::npos) return true;
      if (v.size() >= 4) {
        const std::wstring ext = v.substr(v.size() - 4);
        if (ext == L".wav" || ext == L".WAV") return true;
      }
      return false;
    };

    if (!sound.empty()) {
      DWORD flags = SND_ASYNC | SND_NODEFAULT;
      flags |= isFileValue(sound) ? SND_FILENAME : SND_ALIAS;
      if (!PlaySoundW(sound.c_str(), nullptr, flags)) {
        const UINT beep = (maxImp >= 2) ? MB_ICONHAND : ((maxImp == 1) ? MB_ICONEXCLAMATION : MB_OK);
        MessageBeep(beep);
      }
    }
  }

  for (const auto& n : due) {
    // Mark as fired first to avoid repeated popups if user keeps it open.
    const int64_t firedAt = (n.repeatType == RepeatType::None) ? now : n.scheduledAtUtcMs;
    NoteRepository::markFired(n.id, firedAt, nullptr);
    if (m_currentNote && m_currentNote->id == n.id) {
      m_currentNote->hasFired = true;
      m_currentNote->firedAtUtcMs = firedAt;
    }

    auto* w = new NotificationWindow(m_hInstance, n);
    w->show();
  }
}

void MainWindow::applyUiZoom() {
  const int zoom = AppSettings::uiZoomPercent();

  SendMessageW(m_sliderZoom, TBM_SETPOS, TRUE, zoom);
  updateZoomLabel();

  // Create scaled font from DEFAULT_GUI_FONT
  LOGFONTW lf{};
  if (GetObjectW(m_font, sizeof(lf), &lf) <= 0) {
    return;
  }

  // lfHeight can be negative (character height). Scale magnitude.
  const int h = lf.lfHeight;
  const int scaled = MulDiv(h, zoom, 100);
  lf.lfHeight = (h < 0) ? -std::abs(scaled) : std::abs(scaled);

  const LOGFONTW baseLf = lf;

  HFONT newFont = CreateFontIndirectW(&baseLf);
  if (!newFont) return;

  if (m_fontOwned) {
    DeleteObject(m_fontOwned);
  }
  m_fontOwned = newFont;

  // Bold font for formatting buttons
  if (m_fontBold) {
    DeleteObject(m_fontBold);
  }
  LOGFONTW boldLf = baseLf;
  boldLf.lfWeight = FW_BOLD;
  m_fontBold = CreateFontIndirectW(&boldLf);

  // Apply fonts to all controls
  SendMessageW(m_btnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnRefresh, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnImport, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblZoom, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_sliderZoom, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  if (m_calendarView) {
    m_calendarView->setZoomPercent(zoom);
  }
  SendMessageW(m_list, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_editTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblTime, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_timePicker, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblImportance, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboImportance, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblCategory, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboCategory, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblReminder, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboReminder, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblRepeat, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboRepeat, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblRepeatDays, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatMon, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatTue, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatWed, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatThu, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatFri, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatSat, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkRepeatSun, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkAutoHide, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_editAutoHideSeconds, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnSave, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnDelete, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnCloseEditor, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnPreviewPopup, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_previewLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_previewTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontBold), TRUE);
  SendMessageW(m_previewClose, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_previewSnooze, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_previewCountdown, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_chkSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblSoundNormal, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblSoundImportant, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_lblSoundUrgent, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboSoundNormal, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboSoundImportant, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_comboSoundUrgent, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnTestSound, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnBold, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontBold), TRUE);
  SendMessageW(m_btnItalic, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnUnderline, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnBullet, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_btnImage, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_editorRich, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
  SendMessageW(m_previewRich, WM_SETFONT, reinterpret_cast<WPARAM>(m_fontOwned), TRUE);
}

void MainWindow::applyUiTheme() {
  const int s = AppSettings::uiThemeStyle();
  const UiThemeStyle style = (s == 1) ? UiThemeStyle::Minimal : UiThemeStyle::Premium;
  m_theme = UiTheme::fromStyle(style);
  recreateBrushes();

  if (m_appIconLarge) DestroyIcon(m_appIconLarge);
  if (m_appIconSmall) DestroyIcon(m_appIconSmall);
  m_appIconLarge = AppIcon::createIcon(64, m_theme.style);
  m_appIconSmall = AppIcon::createIcon(32, m_theme.style);
  if (m_appIconLarge) {
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_appIconLarge));
  }
  if (m_appIconSmall) {
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_appIconSmall));
    if (m_trayAdded) {
      m_nid.hIcon = m_appIconSmall;
      Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
  }

  if (m_calendarView) {
    m_calendarView->setThemeStyle(style);
  }

  // List colors
  ListView_SetBkColor(m_list, m_theme.panelBg);
  ListView_SetTextBkColor(m_list, m_theme.panelBg);
  ListView_SetTextColor(m_list, m_theme.text);

  // RichEdit backgrounds
  SendMessageW(m_editorRich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(m_theme.editorBg));
  SendMessageW(m_previewRich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(m_theme.editorBg));

  // Default text color for RichEdits
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_COLOR;
  cf.crTextColor = m_theme.editorText;
  SendMessageW(m_editorRich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
  SendMessageW(m_previewRich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));

  updateNotificationPreview();

  // Force repaint of all child windows
  InvalidateRect(m_hwnd, nullptr, TRUE);
  
  // Redraw all child controls
  EnumChildWindows(m_hwnd, [](HWND hwnd, LPARAM) -> BOOL {
    InvalidateRect(hwnd, nullptr, TRUE);
    return TRUE;
  }, 0);
}

void MainWindow::updateNotificationPreview() {
  if (!m_previewRich) return;
  // Preview is always visible now (checkbox removed)

  // Importance stripe color
  int imp = static_cast<int>(SendMessageW(m_comboImportance, CB_GETCURSEL, 0, 0));
  imp = std::clamp(imp, 0, 2);
  COLORREF stripe = m_theme.badgeNormal;
  if (imp >= 2) stripe = m_theme.badgeUrgent;
  else if (imp == 1) stripe = m_theme.badgeImportant;

  if (!m_previewStripeBrush || m_previewStripeColor != stripe) {
    if (m_previewStripeBrush) {
      DeleteObject(m_previewStripeBrush);
      m_previewStripeBrush = nullptr;
    }
    m_previewStripeBrush = CreateSolidBrush(stripe);
    m_previewStripeColor = stripe;
    if (m_previewStripe) {
      InvalidateRect(m_previewStripe, nullptr, TRUE);
    }
  }

  std::wstring title = getControlText(m_editTitle);
  if (title.empty()) title = L"(без названия)";

  // Add event start date/time to title (like in NotificationWindow)
  SYSTEMTIME t{};
  const LRESULT gdt = SendMessageW(m_timePicker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&t));
  if (gdt == GDT_VALID) {
    SYSTEMTIME day = selectedDateLocal();
    day.wHour = t.wHour;
    day.wMinute = t.wMinute;
    day.wSecond = 0;
    day.wMilliseconds = 0;
    wchar_t dtBuf[64]{};
    swprintf_s(dtBuf, L"  %02d.%02d.%04d в %02d:%02d",
               day.wDay, day.wMonth, day.wYear, day.wHour, day.wMinute);
    title += dtBuf;
  }
  SetWindowTextW(m_previewTitle, title.c_str());

  // Show exactly what will be used in the notification window: current RTF from the editor.
  const std::string rtf = RichEditUtil::getRtfBytes(m_editorRich);
  if (!rtf.empty()) {
    RichEditUtil::setRtfBytes(m_previewRich, rtf);
  } else {
    SetWindowTextW(m_previewRich, L"");
  }

  // Auto-hide mock (progress + label)
  const bool autoHide = (SendMessageW(m_chkAutoHide, BM_GETCHECK, 0, 0) == BST_CHECKED);
  if (autoHide) {
    const int sec = std::clamp(toIntOr(getControlText(m_editAutoHideSeconds), 5), 1, 3600);
    SendMessageW(m_previewProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
    SendMessageW(m_previewProgress, PBM_SETPOS, 0, 0);
    wchar_t buf[128]{};
    swprintf_s(buf, L"Окно закроется через %d сек", sec);
    SetWindowTextW(m_previewCountdown, buf);
  } else {
    SetWindowTextW(m_previewCountdown, L"");
  }
}

void MainWindow::updateZoomLabel() {
  const int zoom = AppSettings::uiZoomPercent();
  wchar_t buf[64]{};
  swprintf_s(buf, L"Масштаб: %d%%", zoom);
  SetWindowTextW(m_lblZoom, buf);
}

void MainWindow::initTray() {
  if (m_trayAdded) return;

  if (!m_trayMenu) {
    m_trayMenu = CreatePopupMenu();
  }

  ZeroMemory(&m_nid, sizeof(m_nid));
  m_nid.cbSize = sizeof(m_nid);
  m_nid.hWnd = m_hwnd;
  m_nid.uID = 1;
  m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  m_nid.uCallbackMessage = WM_APP_TRAY;
  m_nid.hIcon = m_appIconSmall ? m_appIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"AlertCalendar");

  if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
    m_trayAdded = true;
  }
}

void MainWindow::removeTray() {
  if (!m_trayAdded) return;
  Shell_NotifyIconW(NIM_DELETE, &m_nid);
  m_trayAdded = false;
}

void MainWindow::showMainWindowFromTray() {
  initTray();
  ShowWindow(m_hwnd, SW_SHOW);
  ShowWindow(m_hwnd, SW_RESTORE);
  SetForegroundWindow(m_hwnd);
}

void MainWindow::hideToTray() {
  initTray();
  flushAutosave();
  if (AppSettings::minimizeToTray()) {
    ShowWindow(m_hwnd, SW_HIDE);
  } else {
    // если режим трея выключен — закрываем как обычно
    m_isQuitting = true;
    DestroyWindow(m_hwnd);
  }
}

void MainWindow::showTrayMenu() {
  initTray();

  // rebuild menu каждый раз, чтобы чекбоксы отражали текущие настройки
  while (GetMenuItemCount(m_trayMenu) > 0) {
    DeleteMenu(m_trayMenu, 0, MF_BYPOSITION);
  }

  AppendMenuW(m_trayMenu, MF_STRING, ID_TRAY_OPEN, L"Открыть");
  AppendMenuW(m_trayMenu, MF_STRING, ID_TRAY_ADD_TEST, L"+ Тест-заметка (10 сек)");
  AppendMenuW(m_trayMenu, MF_SEPARATOR, 0, nullptr);

  const bool autostart = AppSettings::autostartEnabled();
  AppendMenuW(m_trayMenu, MF_STRING | (autostart ? MF_CHECKED : 0), ID_TRAY_TOGGLE_AUTOSTART, L"Автозапуск");

  const bool minToTray = AppSettings::minimizeToTray();
  AppendMenuW(
    m_trayMenu,
    MF_STRING | (minToTray ? MF_CHECKED : 0),
    ID_TRAY_TOGGLE_MINIMIZE_TO_TRAY,
    L"Сворачивать в трей при закрытии"
  );

  const int theme = AppSettings::uiThemeStyle();
  AppendMenuW(
    m_trayMenu,
    MF_STRING | (theme == 0 ? MF_CHECKED : 0),
    ID_TRAY_THEME_PREMIUM,
    L"Стиль: Премиум"
  );
  AppendMenuW(
    m_trayMenu,
    MF_STRING | (theme == 1 ? MF_CHECKED : 0),
    ID_TRAY_THEME_MINIMAL,
    L"Стиль: Минимал"
  );

  AppendMenuW(m_trayMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m_trayMenu, MF_STRING, ID_TRAY_EXIT, L"Выход");

  POINT pt{};
  GetCursorPos(&pt);

  // Must call before TrackPopupMenu, иначе меню иногда "залипает"
  SetForegroundWindow(m_hwnd);
  TrackPopupMenu(m_trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
  PostMessageW(m_hwnd, WM_NULL, 0, 0);
}


