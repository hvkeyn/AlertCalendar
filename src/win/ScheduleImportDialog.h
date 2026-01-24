#pragma once

#include "import/ScheduleJson.h"
#include "win/UiTheme.h"
#include "win/CalendarView.h"

#include <windows.h>
#include <string>
#include <unordered_map>

class ScheduleImportDialog {
public:
  ScheduleImportDialog(HINSTANCE hInstance, HWND parent);
  bool showModal();
  bool applied() const { return m_applied; }

private:
  static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void onCreate();
  void onDestroy();
  void onSize(int width, int height);
  void onCommand(int id, int code, HWND hwndCtl);

  bool validateAndPreview(bool showMessage = true);
  void updatePreviewList();
  void updateCalendarPreview(bool resetMonth);
  void setPreviewMode(bool calendar);
  void updateStatusText();
  void applyImport();
  void generateFromAi();
  void testAiConnection();
  void persistAiSettings();
  void recreateBrushes();
  void registerButtonStyle(HWND hwnd, int style);
  LRESULT onDrawItem(DRAWITEMSTRUCT* dis);
  LRESULT onCtlColorStatic(HDC hdc, HWND hwnd);
  LRESULT onCtlColorEdit(HDC hdc, HWND hwnd);
  void setAiStatus(const std::wstring& text, int state);

  std::wstring getJsonText() const;
  void setJsonText(const std::wstring& text);

  std::wstring openJsonFileDialog();
  bool loadJsonFromFile(const std::wstring& path, std::wstring* outText, std::wstring* errorOut);
  void setClipboardText(const std::wstring& text);

  HINSTANCE m_hInstance{};
  HWND m_parent{};
  HWND m_hwnd{};
  HFONT m_font{};

  HWND m_lblInput{};
  HWND m_lblPreview{};
  HWND m_btnPreviewList{};
  HWND m_btnPreviewCalendar{};
  HWND m_lblFile{};
  HWND m_lblStatus{};
  HWND m_btnLoadFile{};
  HWND m_btnLegend{};
  HWND m_btnValidate{};
  HWND m_editJson{};
  HWND m_listPreview{};
  std::unique_ptr<CalendarView> m_calendarPreview;
  HWND m_lblAi{};
  HWND m_lblAiStatus{};
  HWND m_aiProgress{};
  HWND m_editPrompt{};
  HWND m_btnGenerate{};
  HWND m_lblBaseUrl{};
  HWND m_editBaseUrl{};
  HWND m_btnTestConnection{};
  HWND m_lblModel{};
  HWND m_editModel{};
  HWND m_lblApiKey{};
  HWND m_editApiKey{};
  HWND m_lblTimeout{};
  HWND m_editTimeout{};
  HWND m_btnApply{};
  HWND m_btnClose{};

  UiTheme m_theme{};
  HBRUSH m_bgBrush{};
  HBRUSH m_editorBrush{};
  std::unordered_map<HWND, int> m_buttonStyles{};

  ScheduleJson::ImportResult m_result{};
  bool m_hasPreview = false;
  bool m_applied = false;
  bool m_done = false;
  bool m_showCalendarPreview = false;
  bool m_aiBusy = false;
  int m_aiStatusState = 0;
  std::wstring m_loadedFile;
  std::wstring m_lastParseError;
};

