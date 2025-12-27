#include "AutostartWin.h"

#include "app/AppInfo.h"
#include "win/WinUtil.h"

#include <windows.h>

namespace {
constexpr const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool readRunValue(std::wstring* outValue) {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
    return false;
  }

  DWORD type = 0;
  DWORD size = 0;
  const LONG q1 = RegQueryValueExW(hKey, AppInfo::AutostartRegistryValueName, nullptr, &type, nullptr, &size);
  if (q1 != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
    RegCloseKey(hKey);
    return false;
  }

  std::wstring buf;
  buf.resize(size / sizeof(wchar_t));
  const LONG q2 = RegQueryValueExW(
    hKey,
    AppInfo::AutostartRegistryValueName,
    nullptr,
    &type,
    reinterpret_cast<LPBYTE>(buf.data()),
    &size
  );
  RegCloseKey(hKey);
  if (q2 != ERROR_SUCCESS) {
    return false;
  }

  buf.resize(wcslen(buf.c_str()));
  *outValue = buf;
  return true;
}

bool writeRunValue(const std::wstring& value, std::wstring* errorOut) {
  HKEY hKey = nullptr;
  const LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr);
  if (r != ERROR_SUCCESS) {
    if (errorOut) *errorOut = WinUtil::lastErrorMessage(r);
    return false;
  }
  const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  const LONG w = RegSetValueExW(
    hKey,
    AppInfo::AutostartRegistryValueName,
    0,
    REG_SZ,
    reinterpret_cast<const BYTE*>(value.c_str()),
    bytes
  );
  RegCloseKey(hKey);
  if (w != ERROR_SUCCESS) {
    if (errorOut) *errorOut = WinUtil::lastErrorMessage(w);
    return false;
  }
  return true;
}

bool deleteRunValue(std::wstring* errorOut) {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
    return true;
  }
  const LONG d = RegDeleteValueW(hKey, AppInfo::AutostartRegistryValueName);
  RegCloseKey(hKey);
  if (d != ERROR_SUCCESS && d != ERROR_FILE_NOT_FOUND) {
    if (errorOut) *errorOut = WinUtil::lastErrorMessage(d);
    return false;
  }
  return true;
}
} // namespace

bool AutostartWin::isAutostartEnabled() {
  std::wstring v;
  return readRunValue(&v);
}

bool AutostartWin::setAutostartEnabled(bool enabled, std::wstring* errorOut) {
  if (!enabled) {
    return deleteRunValue(errorOut);
  }

  const std::wstring exe = WinUtil::getExePath();
  const std::wstring value = L"\"" + exe + L"\"";
  return writeRunValue(value, errorOut);
}


