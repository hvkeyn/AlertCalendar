#include "SingleInstance.h"

SingleInstance::SingleInstance(std::wstring mutexName) : m_mutexName(std::move(mutexName)) {}

SingleInstance::~SingleInstance() {
  if (m_mutex) {
    ReleaseMutex(m_mutex);
    CloseHandle(m_mutex);
    m_mutex = nullptr;
  }
}

bool SingleInstance::tryLock() {
  m_mutex = CreateMutexW(nullptr, TRUE, m_mutexName.c_str());
  if (!m_mutex) {
    return false;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(m_mutex);
    m_mutex = nullptr;
    return false;
  }
  return true;
}


