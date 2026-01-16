#pragma once

#include <string>

struct CalendarDayMeta {
  int count = 0;
  int maxImportance = 0; // 0..2
  bool hasNormal = false;
  bool hasImportant = false;
  bool hasUrgent = false;
  std::wstring preview;  // one-line summary (e.g. "09:30 Врач")
};


