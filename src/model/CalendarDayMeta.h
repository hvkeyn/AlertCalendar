#pragma once

#include <string>

struct CalendarDayMeta {
  int count = 0;
  int maxImportance = 0; // 0..2
  std::wstring preview;  // one-line summary (e.g. "09:30 Врач")
};


