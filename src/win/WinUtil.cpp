#include "WinUtil.h"

#include <objbase.h>
#include <shlwapi.h>

#include <stdexcept>
#include <vector>

namespace {
std::wstring widenFromErrorMessage(DWORD err) {
  LPWSTR buf = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD len = FormatMessageW(flags, nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
  if (len == 0 || !buf) {
    return L"Ошибка WinAPI: " + std::to_wstring(err);
  }
  std::wstring msg(buf, len);
  LocalFree(buf);
  return msg;
}
} // namespace

void WinUtil::enableDpiAwareness() {
  // Windows 10+ (dynamic, to keep compatibility).
  HMODULE user32 = LoadLibraryW(L"user32.dll");
  if (user32) {
    using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (fn) {
      fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
      FreeLibrary(user32);
      return;
    }
    FreeLibrary(user32);
  }

  // Fallback for older APIs.
  SetProcessDPIAware();
}

std::wstring WinUtil::getExePath() {
  std::wstring buf;
  buf.resize(32768);
  const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
  if (len == 0 || len >= buf.size()) {
    return L"";
  }
  buf.resize(len);
  return buf;
}

std::wstring WinUtil::getExeDir() {
  std::wstring path = getExePath();
  if (path.empty()) {
    return L"";
  }
  PathRemoveFileSpecW(path.data());
  // PathRemoveFileSpecW may leave extra characters; trim at first null.
  path.resize(wcslen(path.c_str()));
  return path;
}

std::wstring WinUtil::guidString() {
  GUID g{};
  if (FAILED(CoCreateGuid(&g))) {
    return L"";
  }
  wchar_t buf[64]{};
  const int n = StringFromGUID2(g, buf, 64);
  if (n <= 0) {
    return L"";
  }
  std::wstring s(buf);
  // Remove braces for nicer folder names: {xxxx-...} -> xxxx-...
  if (s.size() >= 2 && s.front() == L'{' && s.back() == L'}') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

std::wstring WinUtil::lastErrorMessage(DWORD error) {
  return widenFromErrorMessage(error);
}

std::string WinUtil::toUtf8(const std::wstring& ws) {
  if (ws.empty()) {
    return {};
  }
  const int needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string out;
  out.resize(needed);
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
  return out;
}

std::wstring WinUtil::fromUtf8(const std::string& s) {
  if (s.empty()) {
    return {};
  }
  const int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring out;
  out.resize(needed);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
  return out;
}

std::wstring WinUtil::formatHHMM(const SYSTEMTIME& stLocal) {
  wchar_t buf[16]{};
  swprintf_s(buf, L"%02u:%02u", stLocal.wHour, stLocal.wMinute);
  return buf;
}


