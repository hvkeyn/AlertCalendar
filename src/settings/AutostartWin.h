#pragma once

#include <string>

class AutostartWin {
public:
  static bool isAutostartEnabled();
  static bool setAutostartEnabled(bool enabled, std::wstring* errorOut = nullptr);
};


