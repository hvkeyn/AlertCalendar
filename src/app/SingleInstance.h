#pragma once

#include <string>
#include <windows.h>

class SingleInstance {
public:
  explicit SingleInstance(std::wstring mutexName);
  ~SingleInstance();
  bool tryLock();

private:
  std::wstring m_mutexName;
  HANDLE m_mutex = nullptr;
};


