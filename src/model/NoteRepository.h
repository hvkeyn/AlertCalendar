#pragma once

#include "model/Note.h"
#include "model/CalendarDayMeta.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

class NoteRepository {
public:
  static bool upsert(Note note, std::wstring* errorOut = nullptr);
  static bool removeById(const std::wstring& id, std::wstring* errorOut = nullptr);
  static std::optional<Note> getById(const std::wstring& id, std::wstring* errorOut = nullptr);

  // date is interpreted as LOCAL date (year/month/day) from Windows calendar control.
  static std::vector<Note> listForDate(const SYSTEMTIME& localDate, std::wstring* errorOut = nullptr);
  static std::array<CalendarDayMeta, 32> monthMeta(int year, int month, std::wstring* errorOut = nullptr);
  static std::vector<Note> listDue(int64_t nowUtcMs, int limit = 50, std::wstring* errorOut = nullptr);

  static bool markFired(const std::wstring& id, int64_t firedAtUtcMs, std::wstring* errorOut = nullptr);
  static bool markDismissed(const std::wstring& id, int64_t dismissedAtUtcMs, std::wstring* errorOut = nullptr);
};


