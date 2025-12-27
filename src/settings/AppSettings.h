#pragma once

#include <string>

class AppSettings {
public:
  static bool minimizeToTray();
  static void setMinimizeToTray(bool enabled);

  static int uiZoomPercent();
  static void setUiZoomPercent(int percent);

  // 0 = Premium, 1 = Minimal
  static int uiThemeStyle();
  static void setUiThemeStyle(int style);

  static bool autostartEnabled();
  static void setAutostartEnabled(bool enabled);

  // Sounds
  static bool soundEnabled();
  static void setSoundEnabled(bool enabled);

  // Value is either a system alias (e.g. "SystemAsterisk") or a WAV file path, or empty = off.
  static std::wstring soundNormal();
  static void setSoundNormal(const std::wstring& value);
  static std::wstring soundImportant();
  static void setSoundImportant(const std::wstring& value);
  static std::wstring soundUrgent();
  static void setSoundUrgent(const std::wstring& value);
};


