#include "ScheduleImportDialog.h"

#include "model/NoteRepository.h"
#include "app/AppPaths.h"
#include "core/TimeUtils.h"
#include "settings/AppSettings.h"
#include "net/HttpClient.h"
#include "win/WinUtil.h"

#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int BTN_STYLE_PRIMARY = 1;
constexpr int BTN_STYLE_NEUTRAL = 2;
constexpr int BTN_STYLE_DANGER = 3;
constexpr int BTN_STYLE_GHOST = 4;

constexpr int AI_STATUS_IDLE = 0;
constexpr int AI_STATUS_BUSY = 1;
constexpr int AI_STATUS_OK = 2;
constexpr int AI_STATUS_ERROR = 3;

constexpr UINT WM_APP_AI_DONE = WM_APP + 42;
constexpr UINT WM_APP_AI_TEST_DONE = WM_APP + 43;

constexpr int IDC_IMPORT_BTN_LOAD = 2101;
constexpr int IDC_IMPORT_BTN_LEGEND = 2102;
constexpr int IDC_IMPORT_BTN_VALIDATE = 2103;
constexpr int IDC_IMPORT_EDIT_JSON = 2104;
constexpr int IDC_IMPORT_LIST = 2105;
constexpr int IDC_IMPORT_BTN_GENERATE = 2106;
constexpr int IDC_IMPORT_EDIT_PROMPT = 2107;
constexpr int IDC_IMPORT_EDIT_BASEURL = 2108;
constexpr int IDC_IMPORT_EDIT_MODEL = 2109;
constexpr int IDC_IMPORT_EDIT_APIKEY = 2110;
constexpr int IDC_IMPORT_EDIT_TIMEOUT = 2111;
constexpr int IDC_IMPORT_BTN_APPLY = 2112;
constexpr int IDC_IMPORT_BTN_CLOSE = 2113;
constexpr int IDC_IMPORT_BTN_TEST = 2114;
constexpr int IDC_IMPORT_BTN_PREVIEW_LIST = 2115;
constexpr int IDC_IMPORT_BTN_PREVIEW_CAL = 2116;
constexpr int IDC_IMPORT_CALENDAR = 2117;

struct AiResponse {
  int status = 0;
  std::wstring body;
  std::wstring error;
};

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

COLORREF blendColor(COLORREF base, COLORREF mix, int alpha) {
  const int inv = 255 - alpha;
  const int r = (GetRValue(base) * inv + GetRValue(mix) * alpha) / 255;
  const int g = (GetGValue(base) * inv + GetGValue(mix) * alpha) / 255;
  const int b = (GetBValue(base) * inv + GetBValue(mix) * alpha) / 255;
  return RGB(r, g, b);
}

std::wstring joinMessages(const std::vector<std::wstring>& items) {
  std::wstring out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) out += L"; ";
    out += items[i];
  }
  return out;
}

std::wstring formatDate(const SYSTEMTIME& st) {
  wchar_t buf[16]{};
  swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::wstring formatTime(const SYSTEMTIME& st) {
  wchar_t buf[8]{};
  swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
  return buf;
}

std::wstring opLabelRu(ScheduleJson::ImportOp op) {
  switch (op) {
    case ScheduleJson::ImportOp::Update: return L"обновить";
    case ScheduleJson::ImportOp::Delete: return L"удалить";
    case ScheduleJson::ImportOp::Create:
    default: return L"создать";
  }
}

std::wstring stripCodeFences(std::wstring s) {
  const std::wstring fence = L"```";
  const size_t start = s.find(fence);
  if (start == std::wstring::npos) return s;
  const size_t end = s.rfind(fence);
  if (end == std::wstring::npos || end <= start) return s;
  std::wstring inner = s.substr(start + fence.size(), end - start - fence.size());
  // Remove optional "json" token on the first line
  if (!inner.empty() && inner[0] == L'\n') inner.erase(0, 1);
  if (inner.rfind(L"json", 0) == 0) {
    size_t nl = inner.find(L'\n');
    if (nl != std::wstring::npos) {
      inner = inner.substr(nl + 1);
    }
  }
  return inner;
}

std::wstring escapeJsonString(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 16);
  for (wchar_t ch : s) {
    switch (ch) {
      case L'\\': out += L"\\\\"; break;
      case L'"': out += L"\\\""; break;
      case L'\n': out += L"\\n"; break;
      case L'\r': out += L"\\r"; break;
      case L'\t': out += L"\\t"; break;
      case L'\b': out += L"\\b"; break;
      case L'\f': out += L"\\f"; break;
      default:
        if (ch < 0x20) {
          wchar_t buf[7]{};
          swprintf_s(buf, L"\\u%04x", static_cast<unsigned int>(ch));
          out += buf;
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::wstring trimCopy(const std::wstring& s) {
  size_t start = 0;
  while (start < s.size() && iswspace(s[start])) ++start;
  size_t end = s.size();
  while (end > start && iswspace(s[end - 1])) --end;
  return s.substr(start, end - start);
}

std::wstring buildChatRequest(const std::wstring& model, const std::wstring& prompt) {
  const std::wstring system =
    L"Верни только JSON без пояснений. "
    L"Формат: {\"mode\":\"merge|replace\",\"notes\":["
    L"{\"op\":\"create|update|delete\",\"id\":\"optional\","
    L"\"date\":\"YYYY-MM-DD\",\"time\":\"HH:MM\",\"title\":\"...\","
    L"\"reminderMinutesBefore\":0,\"importance\":\"normal|important|urgent\","
    L"\"category\":0,"
    L"\"repeat\":{\"type\":\"none|daily|weekly\",\"weekdays\":[\"mon\"]},"
    L"\"content\":{\"mode\":\"markdown|html|rtf\",\"text\":\"...\"},"
    L"\"autoHide\":{\"enabled\":true,\"seconds\":5}}]}";

  std::wstring json;
  json.reserve(prompt.size() + system.size() + model.size() + 256);
  json += L"{\"model\":\"";
  json += escapeJsonString(model);
  json += L"\",\"messages\":[";
  json += L"{\"role\":\"system\",\"content\":\"";
  json += escapeJsonString(system);
  json += L"\"},";
  json += L"{\"role\":\"user\",\"content\":\"";
  json += escapeJsonString(prompt);
  json += L"\"}]}";
  return json;
}

void initPreviewColumns(HWND list) {
  ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.pszText = const_cast<wchar_t*>(L"Действие");
  col.cx = 90;
  col.iSubItem = 0;
  ListView_InsertColumn(list, 0, &col);

  col.pszText = const_cast<wchar_t*>(L"Дата");
  col.cx = 100;
  col.iSubItem = 1;
  ListView_InsertColumn(list, 1, &col);

  col.pszText = const_cast<wchar_t*>(L"Время");
  col.cx = 70;
  col.iSubItem = 2;
  ListView_InsertColumn(list, 2, &col);

  col.pszText = const_cast<wchar_t*>(L"Заголовок");
  col.cx = 220;
  col.iSubItem = 3;
  ListView_InsertColumn(list, 3, &col);

  col.pszText = const_cast<wchar_t*>(L"Ошибки/предупр.");
  col.cx = 300;
  col.iSubItem = 4;
  ListView_InsertColumn(list, 4, &col);
}

void adjustPreviewColumns(HWND list) {
  RECT rc{};
  GetClientRect(list, &rc);
  const int totalW = std::max<int>(1, static_cast<int>(rc.right - rc.left));
  const int w0 = 90;
  const int w1 = 100;
  const int w2 = 70;
  const int w3 = 220;
  const int used = w0 + w1 + w2 + w3;
  const int w4 = std::max(120, totalW - used - 8);
  ListView_SetColumnWidth(list, 0, w0);
  ListView_SetColumnWidth(list, 1, w1);
  ListView_SetColumnWidth(list, 2, w2);
  ListView_SetColumnWidth(list, 3, w3);
  ListView_SetColumnWidth(list, 4, w4);
}

} // namespace

ScheduleImportDialog::ScheduleImportDialog(HINSTANCE hInstance, HWND parent)
  : m_hInstance(hInstance)
  , m_parent(parent) {}

bool ScheduleImportDialog::showModal() {
  const wchar_t* kClassName = L"AlertCalendar.ScheduleImportDialog";

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &ScheduleImportDialog::wndProcThunk;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = sizeof(void*);
  wc.hInstance = m_hInstance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;

  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  const int width = 980;
  const int height = 640;

  m_hwnd = CreateWindowExW(
    WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
    kClassName,
    L"Импорт расписания + ИИ",
    WS_POPUP | WS_CAPTION | WS_SYSMENU,
    CW_USEDEFAULT, CW_USEDEFAULT, width, height,
    m_parent, nullptr, m_hInstance, this
  );

  if (!m_hwnd) return false;

  // Center on parent
  RECT pr{};
  if (m_parent && GetWindowRect(m_parent, &pr)) {
    const int px = pr.left + ((pr.right - pr.left) - width) / 2;
    const int py = pr.top + ((pr.bottom - pr.top) - height) / 2;
    SetWindowPos(m_hwnd, nullptr, px, py, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  }

  EnableWindow(m_parent, FALSE);
  ShowWindow(m_hwnd, SW_SHOW);
  UpdateWindow(m_hwnd);

  MSG msg{};
  while (!m_done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(m_hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  EnableWindow(m_parent, TRUE);
  SetForegroundWindow(m_parent);
  return m_applied;
}

LRESULT CALLBACK ScheduleImportDialog::wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<ScheduleImportDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<ScheduleImportDialog*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) self->m_hwnd = hwnd;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
  return self->wndProc(hwnd, msg, wParam, lParam);
}

LRESULT ScheduleImportDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      onCreate();
      return 0;
    case WM_SIZE:
      onSize(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_DRAWITEM:
      if (onDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) {
        return TRUE;
      }
      return FALSE;
    case WM_CTLCOLORSTATIC:
      return onCtlColorStatic(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
    case WM_CTLCOLOREDIT:
      return onCtlColorEdit(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      FillRect(hdc, &rc, m_bgBrush ? m_bgBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      return 1;
    }
    case WM_COMMAND:
      onCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
      return 0;
    case WM_APP_AI_DONE: {
      auto* res = reinterpret_cast<AiResponse*>(lParam);
      if (!res) return 0;
      m_aiBusy = false;

      if (!res->error.empty()) {
        setAiStatus(L"Ошибка AI: " + res->error, AI_STATUS_ERROR);
        delete res;
        return 0;
      }
      if (res->status < 200 || res->status >= 300) {
        std::wstring httpMsg = L"Ошибка AI: HTTP " + std::to_wstring(res->status);
        if (!res->body.empty()) {
          httpMsg += L" | " + res->body;
        }
        setAiStatus(httpMsg, AI_STATUS_ERROR);
        delete res;
        return 0;
      }

      std::wstring content;
      std::wstring err;
      if (!ScheduleJson::extractChatCompletionContent(res->body, &content, &err)) {
        setAiStatus(err.empty() ? L"Ошибка AI: не удалось извлечь контент." : (L"Ошибка AI: " + err), AI_STATUS_ERROR);
        delete res;
        return 0;
      }
      content = stripCodeFences(content);
      if (trimCopy(content).empty()) {
        setAiStatus(L"Ошибка AI: пустой ответ.", AI_STATUS_ERROR);
        delete res;
        return 0;
      }

      setJsonText(content);
      const bool ok = validateAndPreview(false);
      if (!ok) {
        const std::wstring errText = m_lastParseError.empty() ? L"AI: JSON не прошел проверку." : (L"AI: " + m_lastParseError);
        setAiStatus(errText, AI_STATUS_ERROR);
      } else {
        setAiStatus(L"AI: готово", AI_STATUS_OK);
      }

      delete res;
      return 0;
    }
    case WM_APP_AI_TEST_DONE: {
      auto* res = reinterpret_cast<AiResponse*>(lParam);
      if (!res) return 0;
      m_aiBusy = false;

      if (!res->error.empty()) {
        setAiStatus(L"Связь: ошибка - " + res->error, AI_STATUS_ERROR);
        delete res;
        return 0;
      }
      if (res->status < 200 || res->status >= 300) {
        std::wstring httpMsg = L"Связь: HTTP " + std::to_wstring(res->status);
        if (!res->body.empty()) {
          httpMsg += L" | " + res->body;
        }
        setAiStatus(httpMsg, AI_STATUS_ERROR);
        delete res;
        return 0;
      }

      setAiStatus(L"Связь: OK", AI_STATUS_OK);
      delete res;
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      onDestroy();
      m_done = true;
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

void ScheduleImportDialog::onCreate() {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&icc);

  const int s = AppSettings::uiThemeStyle();
  m_theme = UiTheme::fromStyle((s == 1) ? UiThemeStyle::Minimal : UiThemeStyle::Premium);
  recreateBrushes();

  m_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

  m_btnLoadFile = CreateWindowExW(
    0, L"BUTTON", L"Файл JSON…",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    12, 12, 110, 28,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_LOAD)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnLoadFile, BTN_STYLE_NEUTRAL);
  m_btnLegend = CreateWindowExW(
    0, L"BUTTON", L"Легенда",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    130, 12, 90, 28,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_LEGEND)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnLegend, BTN_STYLE_GHOST);
  m_btnValidate = CreateWindowExW(
    0, L"BUTTON", L"Проверить и показать",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    230, 12, 190, 28,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_VALIDATE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnValidate, BTN_STYLE_NEUTRAL);

  m_lblFile = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 46, 600, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_lblStatus = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD | WS_VISIBLE | SS_RIGHT,
    620, 12, 300, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );

  m_lblInput = CreateWindowExW(
    0, L"STATIC", L"Ввод JSON",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 70, 200, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_lblPreview = CreateWindowExW(
    0, L"STATIC", L"Предпросмотр",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    520, 70, 200, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_btnPreviewList = CreateWindowExW(
    0, L"BUTTON", L"Список",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    720, 66, 80, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_PREVIEW_LIST)), m_hInstance, nullptr
  );
  m_btnPreviewCalendar = CreateWindowExW(
    0, L"BUTTON", L"Календарь",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    810, 66, 90, 22,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_PREVIEW_CAL)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnPreviewList, BTN_STYLE_PRIMARY);
  registerButtonStyle(m_btnPreviewCalendar, BTN_STYLE_GHOST);

  m_editJson = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
    12, 92, 460, 420,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_JSON)),
    m_hInstance,
    nullptr
  );

  m_listPreview = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    WC_LISTVIEWW,
    nullptr,
    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
    480, 92, 480, 420,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_LIST)),
    m_hInstance,
    nullptr
  );
  initPreviewColumns(m_listPreview);

  m_calendarPreview = std::make_unique<CalendarView>();
  if (m_calendarPreview->create(m_hInstance, m_hwnd, IDC_IMPORT_CALENDAR)) {
    m_calendarPreview->setThemeStyle(m_theme.style);
    m_calendarPreview->setZoomPercent(AppSettings::uiZoomPercent());
    m_calendarPreview->setMode(CalendarViewMode::Month);
  }
  if (m_calendarPreview && m_calendarPreview->hwnd()) {
    ShowWindow(m_calendarPreview->hwnd(), SW_HIDE);
  }

  m_lblAi = CreateWindowExW(
    0, L"STATIC", L"AI запрос",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 520, 120, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_lblAiStatus = CreateWindowExW(
    0, L"STATIC", L"",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    140, 520, 480, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_btnGenerate = CreateWindowExW(
    0, L"BUTTON", L"Сгенерировать",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    760, 520, 120, 28,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_GENERATE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnGenerate, BTN_STYLE_NEUTRAL);
  m_aiProgress = CreateWindowExW(
    0,
    PROGRESS_CLASSW,
    nullptr,
    WS_CHILD | PBS_MARQUEE,
    640, 520, 100, 18,
    m_hwnd,
    nullptr,
    m_hInstance,
    nullptr
  );
  ShowWindow(m_aiProgress, SW_HIDE);
  m_editPrompt = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
    12, 540, 940, 60,
    m_hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_PROMPT)),
    m_hInstance,
    nullptr
  );

  m_lblBaseUrl = CreateWindowExW(
    0, L"STATIC", L"Endpoint:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 610, 80, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_editBaseUrl = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    100, 606, 360, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_BASEURL)), m_hInstance, nullptr
  );
  m_btnTestConnection = CreateWindowExW(
    0, L"BUTTON", L"Тест связи",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    470, 606, 90, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_TEST)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnTestConnection, BTN_STYLE_GHOST);
  m_lblModel = CreateWindowExW(
    0, L"STATIC", L"Модель:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    480, 610, 60, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_editModel = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    550, 606, 160, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_MODEL)), m_hInstance, nullptr
  );

  m_lblApiKey = CreateWindowExW(
    0, L"STATIC", L"API ключ:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    12, 638, 80, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_editApiKey = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
    100, 634, 360, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_APIKEY)), m_hInstance, nullptr
  );
  m_lblTimeout = CreateWindowExW(
    0, L"STATIC", L"Timeout (сек.):",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    480, 638, 100, 18,
    m_hwnd, nullptr, m_hInstance, nullptr
  );
  m_editTimeout = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"30",
    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
    590, 634, 60, 24,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_EDIT_TIMEOUT)), m_hInstance, nullptr
  );

  m_btnApply = CreateWindowExW(
    0, L"BUTTON", L"Применить",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    720, 530, 120, 30,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_APPLY)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnApply, BTN_STYLE_PRIMARY);
  m_btnClose = CreateWindowExW(
    0, L"BUTTON", L"Закрыть",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    850, 530, 90, 30,
    m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IMPORT_BTN_CLOSE)), m_hInstance, nullptr
  );
  registerButtonStyle(m_btnClose, BTN_STYLE_GHOST);

  SendMessageW(m_btnLoadFile, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnLegend, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnValidate, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblFile, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblStatus, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblInput, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblPreview, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnPreviewList, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnPreviewCalendar, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editJson, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_listPreview, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblAi, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblAiStatus, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnGenerate, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editPrompt, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblBaseUrl, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editBaseUrl, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnTestConnection, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblModel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editModel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblApiKey, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editApiKey, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_lblTimeout, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_editTimeout, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnApply, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
  SendMessageW(m_btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

  ListView_SetBkColor(m_listPreview, m_theme.editorBg);
  ListView_SetTextBkColor(m_listPreview, m_theme.editorBg);
  ListView_SetTextColor(m_listPreview, m_theme.text);

  setControlText(m_editBaseUrl, AppSettings::aiBaseUrl());
  setControlText(m_editModel, AppSettings::aiModel());
  setControlText(m_editApiKey, AppSettings::aiApiKey());
  setControlText(m_editTimeout, std::to_wstring(AppSettings::aiTimeoutSeconds()));

  updateStatusText();
  setAiStatus(L"Ожидание запроса", AI_STATUS_IDLE);
}

void ScheduleImportDialog::onDestroy() {
  m_hwnd = nullptr;
  if (m_bgBrush) {
    DeleteObject(m_bgBrush);
    m_bgBrush = nullptr;
  }
  if (m_editorBrush) {
    DeleteObject(m_editorBrush);
    m_editorBrush = nullptr;
  }
}

void ScheduleImportDialog::recreateBrushes() {
  if (m_bgBrush) DeleteObject(m_bgBrush);
  if (m_editorBrush) DeleteObject(m_editorBrush);
  m_bgBrush = CreateSolidBrush(m_theme.windowBg);
  m_editorBrush = CreateSolidBrush(m_theme.editorBg);
}

void ScheduleImportDialog::registerButtonStyle(HWND hwnd, int style) {
  if (!hwnd) return;
  m_buttonStyles[hwnd] = style;
  LONG_PTR s = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if ((s & BS_OWNERDRAW) == 0) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, s | BS_OWNERDRAW);
  }
}

LRESULT ScheduleImportDialog::onDrawItem(DRAWITEMSTRUCT* dis) {
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
  HGDIOBJ oldF = SelectObject(hdc, m_font);
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

LRESULT ScheduleImportDialog::onCtlColorStatic(HDC hdc, HWND hwnd) {
  SetBkMode(hdc, TRANSPARENT);
  COLORREF color = m_theme.text;
  if (hwnd == m_lblStatus) {
    color = m_theme.mutedText;
  } else if (hwnd == m_lblAiStatus) {
    if (m_aiStatusState == AI_STATUS_BUSY) color = m_theme.accent;
    else if (m_aiStatusState == AI_STATUS_OK) color = RGB(16, 185, 129);
    else if (m_aiStatusState == AI_STATUS_ERROR) color = m_theme.badgeUrgent;
    else color = m_theme.mutedText;
  }
  SetTextColor(hdc, color);
  return reinterpret_cast<LRESULT>(m_bgBrush);
}

LRESULT ScheduleImportDialog::onCtlColorEdit(HDC hdc, HWND /*hwnd*/) {
  SetBkMode(hdc, OPAQUE);
  SetTextColor(hdc, m_theme.editorText);
  SetBkColor(hdc, m_theme.editorBg);
  return reinterpret_cast<LRESULT>(m_editorBrush);
}

void ScheduleImportDialog::setAiStatus(const std::wstring& text, int state) {
  m_aiStatusState = state;
  setControlText(m_lblAiStatus, text);
  const bool busy = (state == AI_STATUS_BUSY);
  m_aiBusy = busy;
  if (m_aiProgress) {
    ShowWindow(m_aiProgress, busy ? SW_SHOW : SW_HIDE);
    SendMessageW(m_aiProgress, PBM_SETMARQUEE, busy ? TRUE : FALSE, 30);
  }
  if (m_btnGenerate) {
    EnableWindow(m_btnGenerate, !busy);
  }
  if (m_btnTestConnection) {
    EnableWindow(m_btnTestConnection, !busy);
  }
  if (m_lblAiStatus) {
    InvalidateRect(m_lblAiStatus, nullptr, TRUE);
  }
}

void ScheduleImportDialog::onSize(int width, int height) {
  const int margin = 12;
  const int gap = 8;
  const int btnH = 28;
  const int labelH = 18;
  const int fieldH = 24;
  const int aiSectionH = 150;

  int y = margin;
  const int btnLoadW = 110;
  const int btnLegendW = 90;
  const int btnValidateW = 190;

  MoveWindow(m_btnLoadFile, margin, y, btnLoadW, btnH, TRUE);
  MoveWindow(m_btnLegend, margin + btnLoadW + gap, y, btnLegendW, btnH, TRUE);
  MoveWindow(m_btnValidate, margin + btnLoadW + btnLegendW + gap * 2, y, btnValidateW, btnH, TRUE);

  const int statusW = std::max(200, width - (margin + btnLoadW + btnLegendW + btnValidateW + gap * 2) - margin);
  MoveWindow(m_lblStatus, width - margin - statusW, y + 4, statusW, labelH, TRUE);

  y += btnH + gap;
  MoveWindow(m_lblFile, margin, y, std::max(100, width - margin * 2), labelH, TRUE);
  y += labelH + gap;

  const int leftW = std::max(220, (width - margin * 2 - gap) * 45 / 100);
  const int rightW = std::max(220, width - margin * 2 - gap - leftW);

  MoveWindow(m_lblInput, margin, y, leftW, labelH, TRUE);
  const int rightX = margin + leftW + gap;
  const int toggleW = 86;
  const int toggleGap = 6;
  const int toggleH = 22;
  const int togglesW = toggleW * 2 + toggleGap;
  MoveWindow(m_lblPreview, rightX, y, std::max(120, rightW - togglesW - gap), labelH, TRUE);
  MoveWindow(m_btnPreviewList, rightX + rightW - togglesW, y - 2, toggleW, toggleH, TRUE);
  MoveWindow(m_btnPreviewCalendar, rightX + rightW - toggleW, y - 2, toggleW, toggleH, TRUE);
  y += labelH + gap;

  const int contentBottom = std::max(y + 120, height - margin - btnH - gap - aiSectionH - gap);
  const int contentH = std::max(120, contentBottom - y);

  MoveWindow(m_editJson, margin, y, leftW, contentH, TRUE);
  MoveWindow(m_listPreview, rightX, y, rightW, contentH, TRUE);
  if (m_calendarPreview && m_calendarPreview->hwnd()) {
    MoveWindow(m_calendarPreview->hwnd(), rightX, y, rightW, contentH, TRUE);
  }

  // AI section
  int aiY = contentBottom + gap;
  const int genW = 130;
  const int aiLabelW = 120;
  const int progressW = 90;
  MoveWindow(m_lblAi, margin, aiY, aiLabelW, labelH, TRUE);
  MoveWindow(m_btnGenerate, width - margin - genW, aiY - 2, genW, btnH, TRUE);
  const int progressX = std::max(margin + aiLabelW + gap, width - margin - genW - gap - progressW);
  MoveWindow(m_aiProgress, progressX, aiY + 1, progressW, labelH - 2, TRUE);
  const int statusLeft = margin + aiLabelW + gap;
  const int statusRight = progressX - gap;
  const int aiStatusW = std::max(80, statusRight - statusLeft);
  MoveWindow(m_lblAiStatus, statusLeft, aiY, aiStatusW, labelH, TRUE);
  aiY += labelH + gap;

  const int promptH = 60;
  MoveWindow(m_editPrompt, margin, aiY, std::max(200, width - margin * 2), promptH, TRUE);
  aiY += promptH + gap;

  const int halfW = std::max(200, (width - margin * 2 - gap) / 2);
  const int labelW1 = 90;
  const int labelW2 = 100;
  const int testW = 90;
  MoveWindow(m_lblBaseUrl, margin, aiY + 4, labelW1, labelH, TRUE);
  const int baseEditW = std::max(80, halfW - labelW1 - gap - testW - gap);
  MoveWindow(m_editBaseUrl, margin + labelW1 + gap, aiY, baseEditW, fieldH, TRUE);
  MoveWindow(m_btnTestConnection, margin + labelW1 + gap + baseEditW + gap, aiY, testW, fieldH, TRUE);
  MoveWindow(m_lblModel, margin + halfW + gap, aiY + 4, 60, labelH, TRUE);
  MoveWindow(m_editModel, margin + halfW + gap + 60 + gap, aiY, std::max(60, halfW - 60 - gap), fieldH, TRUE);
  aiY += fieldH + gap;

  MoveWindow(m_lblApiKey, margin, aiY + 4, labelW1, labelH, TRUE);
  MoveWindow(m_editApiKey, margin + labelW1 + gap, aiY, std::max(80, halfW - labelW1 - gap), fieldH, TRUE);
  MoveWindow(m_lblTimeout, margin + halfW + gap, aiY + 4, labelW2, labelH, TRUE);
  MoveWindow(m_editTimeout, margin + halfW + gap + labelW2 + gap, aiY, 60, fieldH, TRUE);

  const int btnApplyW = 120;
  const int btnCloseW = 90;
  const int btnY = height - margin - btnH;
  MoveWindow(m_btnApply, width - margin - btnCloseW - gap - btnApplyW, btnY, btnApplyW, btnH, TRUE);
  MoveWindow(m_btnClose, width - margin - btnCloseW, btnY, btnCloseW, btnH, TRUE);

  adjustPreviewColumns(m_listPreview);
}

void ScheduleImportDialog::onCommand(int id, int code, HWND hwndCtl) {
  (void)hwndCtl;
  switch (id) {
    case IDC_IMPORT_BTN_LOAD: {
      const std::wstring path = openJsonFileDialog();
      if (path.empty()) return;
      std::wstring text;
      std::wstring err;
      if (!loadJsonFromFile(path, &text, &err)) {
        if (!err.empty()) {
          MessageBoxW(m_hwnd, err.c_str(), L"Ошибка чтения", MB_ICONERROR);
        }
        return;
      }
      m_loadedFile = path;
      setControlText(m_lblFile, L"Файл: " + path);
      setJsonText(text);
      return;
    }
    case IDC_IMPORT_BTN_LEGEND: {
      setClipboardText(ScheduleJson::buildSchemaLegend());
      MessageBoxW(m_hwnd, L"Схема и пример скопированы в буфер.", L"Импорт JSON", MB_ICONINFORMATION);
      return;
    }
    case IDC_IMPORT_BTN_VALIDATE:
      validateAndPreview();
      return;
    case IDC_IMPORT_BTN_PREVIEW_LIST:
      setPreviewMode(false);
      return;
    case IDC_IMPORT_BTN_PREVIEW_CAL:
      setPreviewMode(true);
      return;
    case IDC_IMPORT_BTN_GENERATE:
      generateFromAi();
      return;
    case IDC_IMPORT_BTN_TEST:
      testAiConnection();
      return;
    case IDC_IMPORT_BTN_APPLY:
      applyImport();
      return;
    case IDC_IMPORT_BTN_CLOSE:
      DestroyWindow(m_hwnd);
      return;
    default:
      break;
  }

  if (id == IDC_IMPORT_CALENDAR && m_calendarPreview) {
    const int notify = code;
    if (notify == static_cast<int>(CalendarNotify::MonthChanged)) {
      updateCalendarPreview(false);
      return;
    }
    if (notify == static_cast<int>(CalendarNotify::Selection)) {
      if (m_calendarPreview->mode() == CalendarViewMode::Day) {
        updateCalendarPreview(false);
      }
      return;
    }
    if (notify == static_cast<int>(CalendarNotify::OpenDay)) {
      m_calendarPreview->setMode(CalendarViewMode::Day);
      updateCalendarPreview(false);
      return;
    }
    if (notify == static_cast<int>(CalendarNotify::BackToMonth)) {
      m_calendarPreview->setMode(CalendarViewMode::Month);
      updateCalendarPreview(false);
      return;
    }
  }
}

std::wstring ScheduleImportDialog::getJsonText() const {
  return getControlText(m_editJson);
}

void ScheduleImportDialog::setJsonText(const std::wstring& text) {
  setControlText(m_editJson, text);
}

bool ScheduleImportDialog::validateAndPreview(bool showMessage) {
  const std::wstring json = getJsonText();
  if (json.empty()) {
    m_lastParseError = L"Вставьте JSON перед проверкой.";
    if (showMessage) {
      MessageBoxW(m_hwnd, m_lastParseError.c_str(), L"Импорт JSON", MB_ICONINFORMATION);
    }
    return false;
  }

  std::wstring parseErr;
  ScheduleJson::ImportResult result;
  if (!ScheduleJson::parseSchedule(json, &result, &parseErr)) {
    m_lastParseError = parseErr;
    if (showMessage) {
      MessageBoxW(m_hwnd, parseErr.c_str(), L"Ошибка JSON", MB_ICONERROR);
    }
    m_result = {};
    m_hasPreview = false;
    updatePreviewList();
    updateStatusText();
    return false;
  }

  m_result = std::move(result);
  m_hasPreview = true;
  updatePreviewList();
  updateStatusText();
  updateCalendarPreview(true);
  m_lastParseError.clear();
  return true;
}

void ScheduleImportDialog::updatePreviewList() {
  ListView_DeleteAllItems(m_listPreview);
  if (!m_hasPreview) return;

  int row = 0;
  for (const auto& item : m_result.items) {
    const std::wstring action = opLabelRu(item.op);
    const std::wstring date = item.hasDateTime ? formatDate(item.localDateTime) : L"-";
    const std::wstring time = item.hasDateTime ? formatTime(item.localDateTime) : L"-";
    std::wstring title = item.note.title;
    if (title.empty()) {
      if (!item.note.id.empty()) title = L"id: " + item.note.id;
      else title = L"(без названия)";
    }

    std::wstring issues;
    if (!item.errors.empty()) {
      issues = joinMessages(item.errors);
    }
    if (!item.warnings.empty()) {
      if (!issues.empty()) issues += L" ";
      issues += L"Предупр.: " + joinMessages(item.warnings);
    }
    if (issues.empty()) issues = L"OK";

    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    it.pszText = const_cast<wchar_t*>(action.c_str());
    ListView_InsertItem(m_listPreview, &it);

    ListView_SetItemText(m_listPreview, row, 1, const_cast<wchar_t*>(date.c_str()));
    ListView_SetItemText(m_listPreview, row, 2, const_cast<wchar_t*>(time.c_str()));
    ListView_SetItemText(m_listPreview, row, 3, const_cast<wchar_t*>(title.c_str()));
    ListView_SetItemText(m_listPreview, row, 4, const_cast<wchar_t*>(issues.c_str()));

    ++row;
  }
}

void ScheduleImportDialog::updateCalendarPreview(bool resetMonth) {
  if (!m_calendarPreview) return;
  if (!m_hasPreview) {
    std::array<CalendarDayMeta, 32> empty{};
    m_calendarPreview->setDayMeta(empty);
    m_calendarPreview->setDayNotes({});
    return;
  }

  if (resetMonth) {
    bool hasDate = false;
    SYSTEMTIME first{};
    for (const auto& item : m_result.items) {
      if (!item.hasDateTime || !item.errors.empty()) continue;
      const SYSTEMTIME st = item.localDateTime;
      if (!hasDate ||
          st.wYear < first.wYear ||
          (st.wYear == first.wYear && st.wMonth < first.wMonth) ||
          (st.wYear == first.wYear && st.wMonth == first.wMonth && st.wDay < first.wDay)) {
        first = st;
        hasDate = true;
      }
    }
    if (hasDate) {
      m_calendarPreview->setMonth(first.wYear, first.wMonth);
    }
  }

  const int year = m_calendarPreview->year();
  const int month = m_calendarPreview->month();
  std::array<CalendarDayMeta, 32> meta{};

  for (const auto& item : m_result.items) {
    if (!item.hasDateTime || !item.errors.empty()) continue;
    const SYSTEMTIME st = item.localDateTime;
    if (st.wYear != year || st.wMonth != month) continue;
    const int day = static_cast<int>(st.wDay);
    if (day < 1 || day > 31) continue;
    auto& m = meta[day];
    m.count += 1;
    m.maxImportance = std::max(m.maxImportance, item.note.importance);
    if (item.note.importance >= 2) m.hasUrgent = true;
    else if (item.note.importance == 1) m.hasImportant = true;
    else m.hasNormal = true;
    if (m.preview.empty()) {
      std::wstring title = item.note.title.empty() ? L"(без названия)" : item.note.title;
      m.preview = formatTime(st) + L" " + title;
    }
  }

  m_calendarPreview->setDayMeta(meta);

  if (m_calendarPreview->mode() == CalendarViewMode::Day) {
    const SYSTEMTIME sel = m_calendarPreview->selectedDateLocal();
    std::vector<Note> dayNotes;
    for (const auto& item : m_result.items) {
      if (!item.hasDateTime || !item.errors.empty()) continue;
      const SYSTEMTIME st = item.localDateTime;
      if (st.wYear != sel.wYear || st.wMonth != sel.wMonth || st.wDay != sel.wDay) continue;
      dayNotes.push_back(item.note);
    }
    std::sort(dayNotes.begin(), dayNotes.end(), [](const Note& a, const Note& b) {
      return a.scheduledAtUtcMs < b.scheduledAtUtcMs;
    });
    m_calendarPreview->setDayNotes(std::move(dayNotes));
  }
}

void ScheduleImportDialog::setPreviewMode(bool calendar) {
  m_showCalendarPreview = calendar;
  ShowWindow(m_listPreview, calendar ? SW_HIDE : SW_SHOW);
  if (m_calendarPreview && m_calendarPreview->hwnd()) {
    ShowWindow(m_calendarPreview->hwnd(), calendar ? SW_SHOW : SW_HIDE);
  }
  if (m_btnPreviewList) {
    m_buttonStyles[m_btnPreviewList] = calendar ? BTN_STYLE_GHOST : BTN_STYLE_PRIMARY;
    InvalidateRect(m_btnPreviewList, nullptr, TRUE);
  }
  if (m_btnPreviewCalendar) {
    m_buttonStyles[m_btnPreviewCalendar] = calendar ? BTN_STYLE_PRIMARY : BTN_STYLE_GHOST;
    InvalidateRect(m_btnPreviewCalendar, nullptr, TRUE);
  }
  if (calendar) {
    updateCalendarPreview(false);
  }
}

void ScheduleImportDialog::updateStatusText() {
  if (!m_hasPreview) {
    setControlText(m_lblStatus, L"");
    return;
  }
  int total = static_cast<int>(m_result.items.size());
  int withErrors = 0;
  int withWarnings = 0;
  for (const auto& item : m_result.items) {
    if (!item.errors.empty()) ++withErrors;
    if (!item.warnings.empty()) ++withWarnings;
  }
  std::wstring status = L"Элементов: " + std::to_wstring(total) +
                        L", ошибок: " + std::to_wstring(withErrors) +
                        L", предупреждений: " + std::to_wstring(withWarnings) +
                        L", режим: " + std::wstring(ScheduleJson::importModeLabel(m_result.mode));
  if (!m_result.errors.empty()) {
    status += L" | " + joinMessages(m_result.errors);
  }
  setControlText(m_lblStatus, status);
}

void ScheduleImportDialog::applyImport() {
  if (!validateAndPreview()) return;

  int withErrors = 0;
  for (const auto& item : m_result.items) {
    if (!item.errors.empty()) ++withErrors;
  }
  if (withErrors > 0) {
    MessageBoxW(m_hwnd, L"Исправьте ошибки перед применением.", L"Импорт JSON", MB_ICONWARNING);
    return;
  }

  if (m_result.mode == ScheduleJson::ImportMode::Replace) {
    if (MessageBoxW(m_hwnd, L"Режим replace удалит все существующие заметки. Продолжить?", L"Подтверждение", MB_ICONWARNING | MB_YESNO) != IDYES) {
      return;
    }
    try {
      const fs::path root = AppPaths::notesRootDir();
      for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory()) {
          fs::remove_all(entry.path());
        }
      }
    } catch (...) {
      MessageBoxW(m_hwnd, L"Не удалось очистить существующие заметки.", L"Импорт JSON", MB_ICONERROR);
      return;
    }
  }

  int okCount = 0;
  std::wstring applyErrors;
  const int64_t nowMs = TimeUtils::unixMsNowUtc();

  for (const auto& item : m_result.items) {
    std::wstring err;
    if (item.op == ScheduleJson::ImportOp::Delete) {
      if (item.note.id.empty()) continue;
      if (!NoteRepository::removeById(item.note.id, &err)) {
        if (!err.empty()) {
          applyErrors += L"Удаление " + item.note.id + L": " + err + L"\n";
        }
        continue;
      }
      ++okCount;
      continue;
    }

    if (item.op == ScheduleJson::ImportOp::Update && !item.note.id.empty()) {
      auto existing = NoteRepository::getById(item.note.id, &err);
      if (!existing) {
        applyErrors += L"Не найдена заметка для update: " + item.note.id + L"\n";
        continue;
      }
      Note n = *existing;
      n.title = item.note.title;
      n.scheduledAtUtcMs = item.note.scheduledAtUtcMs;
      n.reminderMinutesBefore = item.note.reminderMinutesBefore;
      n.importance = item.note.importance;
      n.category = item.note.category;
      n.repeatType = item.note.repeatType;
      n.repeatWeekdaysMask = item.note.repeatWeekdaysMask;
      n.autoHideEnabled = item.note.autoHideEnabled;
      n.autoHideSeconds = item.note.autoHideSeconds;
      n.contentMode = item.note.contentMode;
      n.contentRtf = item.note.contentRtf;
      n.contentHtml = item.note.contentHtml;
      n.contentMarkdown = item.note.contentMarkdown;

      const bool scheduledChanged = (existing->scheduledAtUtcMs != n.scheduledAtUtcMs);
      if (scheduledChanged && n.scheduledAtUtcMs > nowMs) {
        n.hasFired = false;
        n.firedAtUtcMs = 0;
        n.dismissed = false;
        n.dismissedAtUtcMs = 0;
      }

      if (!NoteRepository::upsert(n, &err)) {
        if (!err.empty()) {
          applyErrors += L"Ошибка update " + n.id + L": " + err + L"\n";
        }
        continue;
      }
      ++okCount;
      continue;
    }

    // Create (or update by id if provided)
    Note n = item.note;
    if (!NoteRepository::upsert(std::move(n), &err)) {
      if (!err.empty()) {
        applyErrors += L"Ошибка create: " + err + L"\n";
      }
      continue;
    }
    ++okCount;
  }

  if (!applyErrors.empty()) {
    MessageBoxW(m_hwnd, applyErrors.c_str(), L"Ошибки импорта", MB_ICONWARNING);
  }
  if (okCount > 0) {
    m_applied = true;
    if (applyErrors.empty()) {
      MessageBoxW(m_hwnd, L"Импорт завершен.", L"Импорт JSON", MB_ICONINFORMATION);
    }
    DestroyWindow(m_hwnd);
  }
}

void ScheduleImportDialog::persistAiSettings() {
  std::wstring baseUrl = trimCopy(getControlText(m_editBaseUrl));
  while (!baseUrl.empty() && baseUrl.back() == L'/') baseUrl.pop_back();
  std::wstring model = trimCopy(getControlText(m_editModel));
  std::wstring apiKey = trimCopy(getControlText(m_editApiKey));
  int timeout = toIntOr(getControlText(m_editTimeout), AppSettings::aiTimeoutSeconds());
  timeout = std::clamp(timeout, 5, 300);

  AppSettings::setAiBaseUrl(baseUrl);
  AppSettings::setAiModel(model);
  AppSettings::setAiApiKey(apiKey);
  AppSettings::setAiTimeoutSeconds(timeout);

  setControlText(m_editBaseUrl, baseUrl);
  setControlText(m_editModel, model);
  setControlText(m_editApiKey, apiKey);
  setControlText(m_editTimeout, std::to_wstring(timeout));
}

void ScheduleImportDialog::testAiConnection() {
  if (m_aiBusy) {
    setAiStatus(L"Связь: проверка уже выполняется…", AI_STATUS_BUSY);
    return;
  }

  persistAiSettings();
  const std::wstring baseUrl = AppSettings::aiBaseUrl();
  const std::wstring apiKey = AppSettings::aiApiKey();
  const int timeoutSec = AppSettings::aiTimeoutSeconds();

  if (baseUrl.empty()) {
    setAiStatus(L"Связь: не указан endpoint.", AI_STATUS_ERROR);
    return;
  }

  std::wstring url = baseUrl;
  if (!url.empty() && url.back() == L'/') url.pop_back();
  url += L"/v1/models";

  std::wstring headers = L"Accept: application/json\r\n";
  if (!apiKey.empty()) {
    headers += L"Authorization: Bearer " + apiKey + L"\r\n";
  }

  setAiStatus(L"Связь: проверка…", AI_STATUS_BUSY);
  const int timeoutMs = timeoutSec * 1000;
  std::thread([hwnd = m_hwnd, url, headers, timeoutMs]() {
    const auto resp = HttpClient::getJson(url, headers, timeoutMs);
    auto* res = new AiResponse{};
    res->status = resp.status;
    res->body = resp.body;
    res->error = resp.error;
    if (!PostMessageW(hwnd, WM_APP_AI_TEST_DONE, 0, reinterpret_cast<LPARAM>(res))) {
      delete res;
    }
  }).detach();
}

void ScheduleImportDialog::generateFromAi() {
  if (m_aiBusy) {
    setAiStatus(L"AI: запрос уже выполняется…", AI_STATUS_BUSY);
    return;
  }

  const std::wstring prompt = trimCopy(getControlText(m_editPrompt));
  if (prompt.empty()) {
    setAiStatus(L"AI: пустой запрос.", AI_STATUS_ERROR);
    MessageBoxW(m_hwnd, L"Введите текст запроса для AI.", L"AI генерация", MB_ICONINFORMATION);
    return;
  }

  persistAiSettings();

  const std::wstring baseUrl = AppSettings::aiBaseUrl();
  const std::wstring model = AppSettings::aiModel();
  const std::wstring apiKey = AppSettings::aiApiKey();
  const int timeoutSec = AppSettings::aiTimeoutSeconds();

  if (baseUrl.empty()) {
    setAiStatus(L"AI: не указан endpoint.", AI_STATUS_ERROR);
    MessageBoxW(m_hwnd, L"Укажите endpoint.", L"AI генерация", MB_ICONWARNING);
    return;
  }
  if (model.empty()) {
    setAiStatus(L"AI: не указана модель.", AI_STATUS_ERROR);
    MessageBoxW(m_hwnd, L"Укажите модель.", L"AI генерация", MB_ICONWARNING);
    return;
  }

  std::wstring url = baseUrl;
  if (!url.empty() && url.back() == L'/') url.pop_back();
  url += L"/v1/chat/completions";

  const std::wstring body = buildChatRequest(model, prompt);
  std::wstring headers = L"Accept: application/json\r\n";
  if (!apiKey.empty()) {
    headers += L"Authorization: Bearer " + apiKey + L"\r\n";
  }

  setAiStatus(L"AI: запрос отправлен…", AI_STATUS_BUSY);
  const int timeoutMs = timeoutSec * 1000;
  std::thread([hwnd = m_hwnd, url, body, headers, timeoutMs]() {
    const auto resp = HttpClient::postJson(url, body, headers, timeoutMs);
    auto* res = new AiResponse{};
    res->status = resp.status;
    res->body = resp.body;
    res->error = resp.error;
    if (!PostMessageW(hwnd, WM_APP_AI_DONE, 0, reinterpret_cast<LPARAM>(res))) {
      delete res;
    }
  }).detach();
}

std::wstring ScheduleImportDialog::openJsonFileDialog() {
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
    { L"JSON файлы", L"*.json" },
    { L"Все файлы", L"*.*" }
  };
  dlg->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
  dlg->SetTitle(L"Выбрать JSON файл");

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

bool ScheduleImportDialog::loadJsonFromFile(const std::wstring& path, std::wstring* outText, std::wstring* errorOut) {
  if (!outText) return false;
  outText->clear();
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = L"Не удалось открыть файл: " + path;
    return false;
  }
  std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    return true;
  }

  // UTF-16 LE/BE BOM
  if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
    const size_t len = (bytes.size() - 2) / 2;
    const wchar_t* data = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
    outText->assign(data, data + len);
    return true;
  }
  if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF) {
    const size_t len = (bytes.size() - 2) / 2;
    outText->resize(len);
    for (size_t i = 0; i < len; ++i) {
      const unsigned char b0 = static_cast<unsigned char>(bytes[2 + i * 2]);
      const unsigned char b1 = static_cast<unsigned char>(bytes[2 + i * 2 + 1]);
      (*outText)[i] = static_cast<wchar_t>((b0 << 8) | b1);
    }
    return true;
  }
  // UTF-8 BOM
  if (bytes.size() >= 3 &&
      static_cast<unsigned char>(bytes[0]) == 0xEF &&
      static_cast<unsigned char>(bytes[1]) == 0xBB &&
      static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  *outText = WinUtil::fromUtf8(bytes);
  return true;
}

void ScheduleImportDialog::setClipboardText(const std::wstring& text) {
  if (!OpenClipboard(m_hwnd)) return;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (hMem) {
    void* ptr = GlobalLock(hMem);
    if (ptr) {
      memcpy(ptr, text.c_str(), bytes);
      GlobalUnlock(hMem);
      SetClipboardData(CF_UNICODETEXT, hMem);
    }
  }
  CloseClipboard();
}

