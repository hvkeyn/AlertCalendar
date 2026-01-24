#pragma once

#include "model/Note.h"

#include <string>
#include <vector>
#include <windows.h>

namespace ScheduleJson {

enum class ImportMode : int {
  Merge = 0,
  Replace = 1
};

enum class ImportOp : int {
  Create = 0,
  Update = 1,
  Delete = 2
};

struct ImportItem {
  int index = 0;
  ImportOp op = ImportOp::Create;
  Note note{};
  bool hasDateTime = false;
  SYSTEMTIME localDateTime{};
  std::vector<std::wstring> errors;
  std::vector<std::wstring> warnings;
};

struct ImportResult {
  ImportMode mode = ImportMode::Merge;
  std::vector<ImportItem> items;
  std::vector<std::wstring> errors;
};

// Returns false only on invalid JSON (syntax/root type). Validation errors
// are returned in ImportResult.
bool parseSchedule(const std::wstring& jsonText, ImportResult* out, std::wstring* errorOut = nullptr);
bool extractChatCompletionContent(const std::wstring& responseJson, std::wstring* contentOut, std::wstring* errorOut = nullptr);

const wchar_t* importModeLabel(ImportMode mode);
const wchar_t* importOpLabel(ImportOp op);

std::wstring buildSchemaLegend();

} // namespace ScheduleJson

