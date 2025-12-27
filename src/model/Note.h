#pragma once

#include <cstdint>
#include <string>

enum class NoteContentMode : int {
  VisualRtf = 0,
  Html = 1,
  Markdown = 2
};

struct Note {
  std::wstring id;
  std::wstring title;

  int64_t scheduledAtUtcMs = 0;
  int importance = 0; // 0..2 (обычная/важная/срочная)

  NoteContentMode contentMode = NoteContentMode::VisualRtf;
  std::wstring contentRtf;
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


