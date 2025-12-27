#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace WinUtil {
void enableDpiAwareness();

std::wstring getExePath();
std::wstring getExeDir();

std::wstring guidString();
std::wstring lastErrorMessage(DWORD error = ::GetLastError());

std::string toUtf8(const std::wstring& ws);
std::wstring fromUtf8(const std::string& s);

std::wstring formatHHMM(const SYSTEMTIME& stLocal);
}


