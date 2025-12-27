#include "AppSettings.h"

#include "settings/AutostartWin.h"
#include "win/WinUtil.h"

#include <windows.h>

namespace {
constexpr const wchar_t* kSettingsKeyPath = L"Software\\AlertCalendar";

DWORD readDwordValue(const wchar_t* name, DWORD def) {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
    return def;
  }
  DWORD type = 0;
  DWORD data = 0;
  DWORD size = sizeof(DWORD);
  const LONG r = RegQueryValueExW(hKey, name, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &size);
  RegCloseKey(hKey);
  if (r != ERROR_SUCCESS || type != REG_DWORD) {
    return def;
  }
  return data;
}

void writeDwordValue(const wchar_t* name, DWORD value) {
  HKEY hKey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
  RegCloseKey(hKey);
}

std::wstring readStringValue(const wchar_t* name, const std::wstring& def) {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
    return def;
  }

  DWORD type = 0;
  DWORD size = 0;
  LONG r = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
  if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
    RegCloseKey(hKey);
    return def;
  }

  std::wstring out;
  out.resize(size / sizeof(wchar_t));
  r = RegQueryValueExW(hKey, name, nullptr, &type, reinterpret_cast<LPBYTE>(out.data()), &size);
  RegCloseKey(hKey);
  if (r != ERROR_SUCCESS) {
    return def;
  }

  // Trim trailing nulls
  while (!out.empty() && out.back() == L'\0') out.pop_back();
  return out.empty() ? def : out;
}

void writeStringValue(const wchar_t* name, const std::wstring& value) {
  HKEY hKey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  const std::wstring v = value; // ensure null-terminated write
  const DWORD size = static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t));
  RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), size);
  RegCloseKey(hKey);
}
} // namespace

bool AppSettings::minimizeToTray() {
  return readDwordValue(L"MinimizeToTray", 1) != 0;
}

void AppSettings::setMinimizeToTray(bool enabled) {
  writeDwordValue(L"MinimizeToTray", enabled ? 1 : 0);
}

int AppSettings::uiZoomPercent() {
  return static_cast<int>(readDwordValue(L"ZoomPercent", 100));
}

void AppSettings::setUiZoomPercent(int percent) {
  if (percent < 50) percent = 50;
  if (percent > 250) percent = 250;
  writeDwordValue(L"ZoomPercent", static_cast<DWORD>(percent));
}

int AppSettings::uiThemeStyle() {
  const DWORD v = readDwordValue(L"ThemeStyle", 0);
  return static_cast<int>(v);
}

void AppSettings::setUiThemeStyle(int style) {
  if (style < 0) style = 0;
  if (style > 1) style = 1;
  writeDwordValue(L"ThemeStyle", static_cast<DWORD>(style));
}

bool AppSettings::autostartEnabled() {
  return AutostartWin::isAutostartEnabled();
}

void AppSettings::setAutostartEnabled(bool enabled) {
  std::wstring err;
  AutostartWin::setAutostartEnabled(enabled, &err);
}

bool AppSettings::soundEnabled() {
  return readDwordValue(L"SoundEnabled", 1) != 0;
}

void AppSettings::setSoundEnabled(bool enabled) {
  writeDwordValue(L"SoundEnabled", enabled ? 1 : 0);
}

std::wstring AppSettings::soundNormal() {
  return readStringValue(L"SoundNormal", L"SystemAsterisk");
}

void AppSettings::setSoundNormal(const std::wstring& value) {
  writeStringValue(L"SoundNormal", value);
}

std::wstring AppSettings::soundImportant() {
  return readStringValue(L"SoundImportant", L"SystemExclamation");
}

void AppSettings::setSoundImportant(const std::wstring& value) {
  writeStringValue(L"SoundImportant", value);
}

std::wstring AppSettings::soundUrgent() {
  return readStringValue(L"SoundUrgent", L"SystemHand");
}

void AppSettings::setSoundUrgent(const std::wstring& value) {
  writeStringValue(L"SoundUrgent", value);
}


