#include "NoteRepository.h"

#include "app/AppPaths.h"
#include "core/TimeUtils.h"
#include "win/WinUtil.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
fs::path noteDirNoCreate(const std::wstring& id) {
  return AppPaths::notesRootDir() / id;
}

fs::path metaPath(const std::wstring& id) {
  return noteDirNoCreate(id) / L"meta.txt";
}

fs::path titlePath(const std::wstring& id) {
  return noteDirNoCreate(id) / L"title.txt";
}

fs::path contentRtfPath(const std::wstring& id) {
  return noteDirNoCreate(id) / L"content.rtf";
}

fs::path contentHtmlPath(const std::wstring& id) {
  return noteDirNoCreate(id) / L"content.html";
}

fs::path contentMdPath(const std::wstring& id) {
  return noteDirNoCreate(id) / L"content.md";
}

bool readFileUtf8(const fs::path& p, std::wstring* out, std::wstring* errorOut) {
  (void)errorOut;
  out->clear();
  std::ifstream f(p, std::ios::binary);
  if (!f.is_open()) {
    return false;
  }
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  *out = WinUtil::fromUtf8(data);
  return true;
}

bool writeFileUtf8(const fs::path& p, const std::wstring& text, std::wstring* errorOut) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) {
    if (errorOut) {
      *errorOut = L"Не удалось открыть файл для записи: " + p.wstring();
    }
    return false;
  }
  const std::string data = WinUtil::toUtf8(text);
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return true;
}

std::unordered_map<std::string, std::string> parseMeta(const std::string& meta) {
  std::unordered_map<std::string, std::string> m;
  std::istringstream ss(meta);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty()) continue;
    const auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    const std::string key = line.substr(0, pos);
    const std::string val = line.substr(pos + 1);
    m[key] = val;
  }
  return m;
}

bool readMeta(const std::wstring& id, Note& out, std::wstring* errorOut) {
  (void)errorOut;
  out = Note{};
  out.id = id;

  // title
  {
    std::wstring title;
    readFileUtf8(titlePath(id), &title, nullptr);
    out.title = title;
  }

  // meta
  std::ifstream f(metaPath(id), std::ios::binary);
  if (!f.is_open()) {
    // treat missing meta as missing note
    return false;
  }
  const std::string meta((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const auto m = parseMeta(meta);

  auto getI64 = [&](const char* key, int64_t def = 0) -> int64_t {
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return def;
    try { return std::stoll(it->second); } catch (...) { return def; }
  };

  auto getI = [&](const char* key, int def = 0) -> int {
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
  };

  out.scheduledAtUtcMs = getI64("scheduledAtUtcMs", 0);
  out.importance = getI("importance", 0);
  out.contentMode = static_cast<NoteContentMode>(getI("contentMode", 0));
  out.autoHideEnabled = getI("autoHideEnabled", 0) != 0;
  out.autoHideSeconds = getI("autoHideSeconds", 0);

  const int64_t fired = getI64("firedAtUtcMs", 0);
  if (fired != 0) {
    out.hasFired = true;
    out.firedAtUtcMs = fired;
  }

  const int64_t dismissed = getI64("dismissedAtUtcMs", 0);
  if (dismissed != 0) {
    out.dismissed = true;
    out.dismissedAtUtcMs = dismissed;
  }

  out.createdAtUtcMs = getI64("createdAtUtcMs", 0);
  out.updatedAtUtcMs = getI64("updatedAtUtcMs", 0);

  // content (optional)
  {
    std::wstring rtf;
    if (readFileUtf8(contentRtfPath(id), &rtf, nullptr)) {
      out.contentRtf = rtf;
    }
    std::wstring html;
    if (readFileUtf8(contentHtmlPath(id), &html, nullptr)) {
      out.contentHtml = html;
    }
    std::wstring md;
    if (readFileUtf8(contentMdPath(id), &md, nullptr)) {
      out.contentMarkdown = md;
    }
  }

  return true;
}

bool writeMeta(const Note& n, std::wstring* errorOut) {
  const fs::path dir = AppPaths::noteDir(n.id); // creates dir

  // title
  if (!writeFileUtf8(dir / L"title.txt", n.title, errorOut)) {
    return false;
  }

  std::ostringstream ss;
  ss << "scheduledAtUtcMs=" << n.scheduledAtUtcMs << "\n";
  ss << "importance=" << n.importance << "\n";
  ss << "contentMode=" << static_cast<int>(n.contentMode) << "\n";
  ss << "autoHideEnabled=" << (n.autoHideEnabled ? 1 : 0) << "\n";
  ss << "autoHideSeconds=" << n.autoHideSeconds << "\n";
  ss << "firedAtUtcMs=" << (n.hasFired ? n.firedAtUtcMs : 0) << "\n";
  ss << "dismissedAtUtcMs=" << (n.dismissed ? n.dismissedAtUtcMs : 0) << "\n";
  ss << "createdAtUtcMs=" << n.createdAtUtcMs << "\n";
  ss << "updatedAtUtcMs=" << n.updatedAtUtcMs << "\n";

  const std::string meta = ss.str();
  std::ofstream f(dir / L"meta.txt", std::ios::binary | std::ios::trunc);
  if (!f.is_open()) {
    if (errorOut) {
      *errorOut = L"Не удалось открыть meta.txt для записи.";
    }
    return false;
  }
  f.write(meta.data(), static_cast<std::streamsize>(meta.size()));

  // content files: сохраняем то, что передано
  // Important: if content becomes empty, we must clear old files, otherwise "old text comes back".
  auto writeOrDelete = [&](const fs::path& p, const std::wstring& text) -> bool {
    if (text.empty()) {
      std::error_code ec;
      fs::remove(p, ec);
      return true;
    }
    return writeFileUtf8(p, text, errorOut);
  };

  if (!writeOrDelete(dir / L"content.rtf", n.contentRtf)) return false;
  if (!writeOrDelete(dir / L"content.html", n.contentHtml)) return false;
  if (!writeOrDelete(dir / L"content.md", n.contentMarkdown)) return false;

  return true;
}

bool isSameLocalDate(const SYSTEMTIME& a, const SYSTEMTIME& b) {
  return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}
} // namespace

bool NoteRepository::upsert(Note note, std::wstring* errorOut) {
  try {
    if (note.id.empty()) {
      note.id = WinUtil::guidString();
    }

    const int64_t now = TimeUtils::unixMsNowUtc();
    if (note.createdAtUtcMs == 0) {
      note.createdAtUtcMs = now;
    }
    note.updatedAtUtcMs = now;

    if (!writeMeta(note, errorOut)) {
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка upsert: " + WinUtil::fromUtf8(e.what());
    }
    return false;
  }
}

bool NoteRepository::removeById(const std::wstring& id, std::wstring* errorOut) {
  try {
    const fs::path dir = noteDirNoCreate(id);
    if (!fs::exists(dir)) {
      return true;
    }
    fs::remove_all(dir);
    return true;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка удаления заметки: " + WinUtil::fromUtf8(e.what());
    }
    return false;
  }
}

std::optional<Note> NoteRepository::getById(const std::wstring& id, std::wstring* errorOut) {
  try {
    Note n;
    if (!readMeta(id, n, errorOut)) {
      return std::nullopt;
    }
    return n;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка чтения заметки: " + WinUtil::fromUtf8(e.what());
    }
    return std::nullopt;
  }
}

std::vector<Note> NoteRepository::listForDate(const SYSTEMTIME& localDate, std::wstring* errorOut) {
  std::vector<Note> out;
  try {
    const fs::path root = AppPaths::notesRootDir();
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const std::wstring id = entry.path().filename().wstring();

      Note n;
      if (!readMeta(id, n, nullptr)) {
        continue;
      }

      const SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
      if (isSameLocalDate(stLocal, localDate)) {
        out.push_back(std::move(n));
      }
    }

    std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
      return a.scheduledAtUtcMs < b.scheduledAtUtcMs;
    });

    return out;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка listForDate: " + WinUtil::fromUtf8(e.what());
    }
    return {};
  }
}

std::array<CalendarDayMeta, 32> NoteRepository::monthMeta(int year, int month, std::wstring* errorOut) {
  std::array<CalendarDayMeta, 32> meta{};
  for (auto& d : meta) d = CalendarDayMeta{};

  try {
    // Track earliest note per day for preview
    std::array<int64_t, 32> earliest{};
    earliest.fill(0);

    const fs::path root = AppPaths::notesRootDir();
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const std::wstring id = entry.path().filename().wstring();

      Note n;
      if (!readMeta(id, n, nullptr)) {
        continue;
      }

      if (n.scheduledAtUtcMs == 0) continue;
      const SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
      if (stLocal.wYear != year || stLocal.wMonth != month) continue;
      if (stLocal.wDay < 1 || stLocal.wDay > 31) continue;

      auto& d = meta[stLocal.wDay];
      d.count += 1;
      d.maxImportance = std::max(d.maxImportance, n.importance);

      // Preview: earliest scheduled time + title
      const int64_t prev = earliest[stLocal.wDay];
      if (prev == 0 || n.scheduledAtUtcMs < prev) {
        earliest[stLocal.wDay] = n.scheduledAtUtcMs;
        const std::wstring time = WinUtil::formatHHMM(stLocal);
        std::wstring title = n.title.empty() ? L"(без названия)" : n.title;
        // truncate a bit for cell
        if (title.size() > 22) {
          title.resize(22);
          title += L"…";
        }
        d.preview = time + L" " + title;
      }
    }
    return meta;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка monthMeta: " + WinUtil::fromUtf8(e.what());
    }
    return meta;
  }
}

std::vector<Note> NoteRepository::listDue(int64_t nowUtcMs, int limit, std::wstring* errorOut) {
  std::vector<Note> out;
  try {
    const fs::path root = AppPaths::notesRootDir();
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const std::wstring id = entry.path().filename().wstring();

      Note n;
      if (!readMeta(id, n, nullptr)) {
        continue;
      }

      if (n.hasFired) continue;
      if (n.scheduledAtUtcMs == 0) continue;
      if (n.scheduledAtUtcMs <= nowUtcMs) {
        out.push_back(std::move(n));
      }
    }

    std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
      return a.scheduledAtUtcMs < b.scheduledAtUtcMs;
    });

    if (static_cast<int>(out.size()) > limit) {
      out.resize(static_cast<size_t>(limit));
    }

    return out;
  } catch (const std::exception& e) {
    if (errorOut) {
      *errorOut = L"Ошибка listDue: " + WinUtil::fromUtf8(e.what());
    }
    return {};
  }
}

bool NoteRepository::markFired(const std::wstring& id, int64_t firedAtUtcMs, std::wstring* errorOut) {
  auto opt = getById(id, errorOut);
  if (!opt) return false;
  Note n = *opt;
  n.hasFired = true;
  n.firedAtUtcMs = firedAtUtcMs;
  return upsert(std::move(n), errorOut);
}

bool NoteRepository::markDismissed(const std::wstring& id, int64_t dismissedAtUtcMs, std::wstring* errorOut) {
  auto opt = getById(id, errorOut);
  if (!opt) return false;
  Note n = *opt;
  n.dismissed = true;
  n.dismissedAtUtcMs = dismissedAtUtcMs;
  return upsert(std::move(n), errorOut);
}


