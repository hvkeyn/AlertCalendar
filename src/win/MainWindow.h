#pragma once

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "win/UiTheme.h"

class CalendarView;
#include "model/Note.h"

class MainWindow {
public:
  explicit MainWindow(HINSTANCE hInstance);
  ~MainWindow();

  bool create();
  void show(int nCmdShow);
  HWND hwnd() const { return m_hwnd; }

private:
  static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void onCreate();
  void onDestroy();
  void onSize(int width, int height);
  void onCommand(int id);
  void onNotify(NMHDR* hdr);
  void onHScroll(HWND src);
  LRESULT onCtlColorStatic(HDC hdc, HWND hwndCtl);
  LRESULT onCtlColorEdit(HDC hdc, HWND hwndCtl);
  LRESULT onCtlColorBtn(HDC hdc, HWND hwndCtl);
  void refreshNotesForSelectedDate();
  void addTestNote();
  void addNewNote();
  void checkReminders();
  void applyUiZoom();
  void applyUiTheme();
  void recreateBrushes();
  void updateZoomLabel();
  void loadNoteToEditor(const Note& note);
  void clearEditor();
  void saveEditorToNote(bool refreshAfter);
  void deleteCurrentNote();
  void updateAutoHideEnabled();
  void insertImageIntoRich();
  std::wstring openImageFileDialog();
  std::wstring openSoundFileDialog();
  void refreshSoundUi();
  void onSoundComboChanged(int controlId);
  void playSoundForImportance(int importance, bool showErrors);
  void showNotificationPreviewPopup();
  void updateNotificationPreview();
  void markEditorDirty();
  void scheduleAutosave();
  void flushAutosave();

  SYSTEMTIME selectedDateLocal() const;

  void initTray();
  void removeTray();
  void showMainWindowFromTray();
  void hideToTray();
  void showTrayMenu();

  HINSTANCE m_hInstance{};
  HWND m_hwnd{};
  std::unique_ptr<CalendarView> m_calendarView;
  HWND m_list{};
  HWND m_btnAdd{};
  HWND m_btnRefresh{};
  HWND m_lblZoom{};
  HWND m_sliderZoom{};
  HWND m_lblTitle{};
  HWND m_lblTime{};
  HWND m_lblImportance{};

  // Editor (right panel)
  std::vector<std::wstring> m_listNoteIds;
  std::optional<Note> m_currentNote;
  bool m_loadingEditor = false;
  bool m_refreshingList = false;
  bool m_editorDirty = false;
  UINT_PTR m_autosaveTimerId{};

  HWND m_editTitle{};
  HWND m_timePicker{};
  HWND m_comboImportance{};
  HWND m_chkAutoHide{};
  HWND m_editAutoHideSeconds{};
  HWND m_spinAutoHideSeconds{};
  HWND m_btnSave{};
  HWND m_btnDelete{};
  HWND m_chkPreview{};
  // Preview (notification mock)
  HWND m_previewLabel{};
  HWND m_previewStripe{};
  HWND m_previewTitle{};
  HWND m_previewClose{};
  HWND m_previewSnooze{};
  HWND m_previewProgress{};
  HWND m_previewCountdown{};
  HBRUSH m_previewStripeBrush{};
  COLORREF m_previewStripeColor{};

  // Sound settings (global)
  HWND m_chkSound{};
  HWND m_lblSound{};
  HWND m_lblSoundNormal{};
  HWND m_lblSoundImportant{};
  HWND m_lblSoundUrgent{};
  HWND m_comboSoundNormal{};
  HWND m_comboSoundImportant{};
  HWND m_comboSoundUrgent{};
  HWND m_btnTestSound{};
  HWND m_btnPreviewPopup{};

  HWND m_btnBold{};
  HWND m_btnItalic{};
  HWND m_btnUnderline{};
  HWND m_btnBullet{};
  HWND m_btnImage{};

  // Single editor control (WYSIWYG)
  HWND m_editorRich{};
  HWND m_previewRich{};

  HFONT m_font{};
  HFONT m_fontOwned{};
  HFONT m_fontBold{};
  UINT_PTR m_timerId{};

  // Theme
  UiTheme m_theme;
  HBRUSH m_bgBrush{};
  HBRUSH m_panelBrush{};
  HBRUSH m_editorBrush{};

  // Tray
  NOTIFYICONDATAW m_nid{};
  bool m_trayAdded = false;
  HMENU m_trayMenu = nullptr;
  bool m_isQuitting = false;
};


