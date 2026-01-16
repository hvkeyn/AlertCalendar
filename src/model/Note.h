#pragma once

#include <cstdint>
#include <string>

enum class NoteContentMode : int {
  VisualRtf = 0,
  Html = 1,
  Markdown = 2
};

enum class RepeatType : int {
  None = 0,
  Daily = 1,
  Weekly = 2
};

constexpr int kRepeatDayMon = 1 << 0;
constexpr int kRepeatDayTue = 1 << 1;
constexpr int kRepeatDayWed = 1 << 2;
constexpr int kRepeatDayThu = 1 << 3;
constexpr int kRepeatDayFri = 1 << 4;
constexpr int kRepeatDaySat = 1 << 5;
constexpr int kRepeatDaySun = 1 << 6;

struct Note {
  std::wstring id;
  std::wstring title;

  int64_t scheduledAtUtcMs = 0;      // Время начала события (UTC ms)
  int reminderMinutesBefore = 0;     // За сколько минут до начала напомнить (0 = в момент начала)
  int importance = 0; // 0..2 (обычная/важная/срочная)
  int category = 0;   // 0=нет, 1=жёлтая, 2=зелёная, 3=красная, 4=лиловая, 5=оранжевая, 6=синяя
  RepeatType repeatType = RepeatType::None;
  int repeatWeekdaysMask = 0; // bitmask for weekly repeat (Mon..Sun)

  NoteContentMode contentMode = NoteContentMode::VisualRtf;
  // RTF is stored as raw bytes (can include binary blocks like \bin for images).
  std::string contentRtf;
  std::wstring contentHtml;
  std::wstring contentMarkdown;

  bool autoHideEnabled = false;
  int autoHideSeconds = 0;

  bool hasFired = false;
  int64_t firedAtUtcMs = 0;

  bool dismissed = false;
  int64_t dismissedAtUtcMs = 0;

  int64_t createdAtUtcMs = 0;
  int64_t updatedAtUtcMs = 0;
};


