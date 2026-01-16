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

bool readFileBinary(const fs::path& p, std::string* out) {
  out->clear();
  std::ifstream f(p, std::ios::binary);
  if (!f.is_open()) return false;
  *out = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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

bool writeFileBinary(const fs::path& p, const std::string& data, std::wstring* errorOut) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) {
    if (errorOut) {
      *errorOut = L"Не удалось открыть файл для записи: " + p.wstring();
    }
    return false;
  }
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
  out.category = getI("category", 0);
  out.reminderMinutesBefore = getI("reminderMinutesBefore", 0);
  out.repeatType = static_cast<RepeatType>(std::clamp(getI("repeatType", 0), 0, 2));
  out.repeatWeekdaysMask = getI("repeatWeekdaysMask", 0);
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
    std::string rtf;
    if (readFileBinary(contentRtfPath(id), &rtf)) {
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
  ss << "category=" << n.category << "\n";
  ss << "reminderMinutesBefore=" << n.reminderMinutesBefore << "\n";
  ss << "repeatType=" << static_cast<int>(n.repeatType) << "\n";
  ss << "repeatWeekdaysMask=" << n.repeatWeekdaysMask << "\n";
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
  auto writeOrDeleteUtf8 = [&](const fs::path& p, const std::wstring& text) -> bool {
    if (text.empty()) {
      std::error_code ec;
      fs::remove(p, ec);
      return true;
    }
    return writeFileUtf8(p, text, errorOut);
  };

  // RTF is binary-safe (can contain \bin)
  if (n.contentRtf.empty()) {
    std::error_code ec;
    fs::remove(dir / L"content.rtf", ec);
  } else {
    if (!writeFileBinary(dir / L"content.rtf", n.contentRtf, errorOut)) return false;
  }

  if (!writeOrDeleteUtf8(dir / L"content.html", n.contentHtml)) return false;
  if (!writeOrDeleteUtf8(dir / L"content.md", n.contentMarkdown)) return false;

  return true;
}

bool isSameLocalDate(const SYSTEMTIME& a, const SYSTEMTIME& b) {
  return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

int compareLocalDate(const SYSTEMTIME& a, const SYSTEMTIME& b) {
  if (a.wYear != b.wYear) return (a.wYear < b.wYear) ? -1 : 1;
  if (a.wMonth != b.wMonth) return (a.wMonth < b.wMonth) ? -1 : 1;
  if (a.wDay != b.wDay) return (a.wDay < b.wDay) ? -1 : 1;
  return 0;
}

bool isOnOrAfterLocalDate(const SYSTEMTIME& a, const SYSTEMTIME& b) {
  return compareLocalDate(a, b) >= 0;
}

SYSTEMTIME normalizeLocalDate(SYSTEMTIME d) {
  d.wHour = 0;
  d.wMinute = 0;
  d.wSecond = 0;
  d.wMilliseconds = 0;
  FILETIME ft{};
  if (SystemTimeToFileTime(&d, &ft)) {
    SYSTEMTIME out{};
    if (FileTimeToSystemTime(&ft, &out)) {
      return out;
    }
  }
  return d;
}

SYSTEMTIME addDaysLocalDate(SYSTEMTIME d, int deltaDays) {
  d = normalizeLocalDate(d);
  FILETIME ft{};
  if (!SystemTimeToFileTime(&d, &ft)) return d;
  ULARGE_INTEGER u{};
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  const LONGLONG day100ns = 24LL * 60 * 60 * 10'000'000;
  u.QuadPart = static_cast<ULONGLONG>(static_cast<LONGLONG>(u.QuadPart) + day100ns * deltaDays);
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  SYSTEMTIME out{};
  if (FileTimeToSystemTime(&ft, &out)) {
    return out;
  }
  return d;
}

int weekdayMaskFromWDayOfWeek(WORD dow) {
  switch (dow) {
    case 1: return kRepeatDayMon;
    case 2: return kRepeatDayTue;
    case 3: return kRepeatDayWed;
    case 4: return kRepeatDayThu;
    case 5: return kRepeatDayFri;
    case 6: return kRepeatDaySat;
    case 0: return kRepeatDaySun;
    default: return 0;
  }
}

int effectiveWeeklyMask(const Note& n, const SYSTEMTIME& startLocal) {
  if (n.repeatWeekdaysMask != 0) return n.repeatWeekdaysMask;
  return weekdayMaskFromWDayOfWeek(startLocal.wDayOfWeek);
}

SYSTEMTIME withStartTime(const SYSTEMTIME& date, const SYSTEMTIME& startLocal) {
  SYSTEMTIME out = date;
  out.wHour = startLocal.wHour;
  out.wMinute = startLocal.wMinute;
  out.wSecond = 0;
  out.wMilliseconds = 0;
  return out;
}

bool occurrenceOnDateLocal(const Note& n, const SYSTEMTIME& localDate, int64_t* outUtcMs) {
  if (n.scheduledAtUtcMs == 0) return false;
  const SYSTEMTIME startLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
  SYSTEMTIME date = normalizeLocalDate(localDate);
  const SYSTEMTIME startDate = normalizeLocalDate(startLocal);

  if (!isOnOrAfterLocalDate(date, startDate)) return false;

  if (n.repeatType == RepeatType::None) {
    if (!isSameLocalDate(date, startDate)) return false;
  } else if (n.repeatType == RepeatType::Weekly) {
    const int mask = effectiveWeeklyMask(n, startLocal);
    const int dayMask = weekdayMaskFromWDayOfWeek(date.wDayOfWeek);
    if ((mask & dayMask) == 0) return false;
  } else if (n.repeatType != RepeatType::Daily) {
    return false;
  }

  const SYSTEMTIME occLocal = withStartTime(date, startLocal);
  if (outUtcMs) {
    *outUtcMs = TimeUtils::localSystemTimeToUnixMsUtc(occLocal);
  }
  return true;
}

bool nextOccurrenceOnOrAfterNowDate(const Note& n, int64_t nowUtcMs, int64_t* outUtcMs) {
  if (n.scheduledAtUtcMs == 0) return false;

  const SYSTEMTIME startLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
  const SYSTEMTIME startDate = normalizeLocalDate(startLocal);
  SYSTEMTIME nowLocal = TimeUtils::unixMsToSystemTimeLocal(nowUtcMs);
  SYSTEMTIME nowDate = normalizeLocalDate(nowLocal);

  if (n.repeatType == RepeatType::Daily) {
    SYSTEMTIME candidate = isOnOrAfterLocalDate(nowDate, startDate) ? nowDate : startDate;
    SYSTEMTIME occLocal = withStartTime(candidate, startLocal);
    int64_t occUtc = TimeUtils::localSystemTimeToUnixMsUtc(occLocal);
    if (occUtc < nowUtcMs) {
      candidate = addDaysLocalDate(candidate, 1);
      occLocal = withStartTime(candidate, startLocal);
      occUtc = TimeUtils::localSystemTimeToUnixMsUtc(occLocal);
    }
    if (outUtcMs) *outUtcMs = occUtc;
    return true;
  }

  if (n.repeatType == RepeatType::Weekly) {
    const int mask = effectiveWeeklyMask(n, startLocal);
    SYSTEMTIME base = isOnOrAfterLocalDate(nowDate, startDate) ? nowDate : startDate;
    for (int ahead = 0; ahead < 14; ++ahead) {
      SYSTEMTIME date = addDaysLocalDate(base, ahead);
      if (!isOnOrAfterLocalDate(date, startDate)) continue;
      const int dayMask = weekdayMaskFromWDayOfWeek(date.wDayOfWeek);
      if ((mask & dayMask) == 0) continue;
      const SYSTEMTIME occLocal = withStartTime(date, startLocal);
      const int64_t occUtc = TimeUtils::localSystemTimeToUnixMsUtc(occLocal);
      if (occUtc < nowUtcMs) continue;
      if (outUtcMs) *outUtcMs = occUtc;
      return true;
    }
    return false;
  }

  if (n.repeatType == RepeatType::None) {
    if (!occurrenceOnDateLocal(n, nowDate, outUtcMs)) return false;
    return true;
  }

  return false;
}

bool isLeapYear(int year) {
  if (year % 400 == 0) return true;
  if (year % 100 == 0) return false;
  return (year % 4) == 0;
}

int daysInMonth(int year, int month) {
  static const int kDays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (month < 1 || month > 12) return 30;
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[month - 1];
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

      int64_t occUtc = 0;
      if (occurrenceOnDateLocal(n, localDate, &occUtc)) {
        n.scheduledAtUtcMs = occUtc;
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

    auto applyMeta = [&](int day, const Note& n, int64_t occUtc) {
      if (day < 1 || day > 31) return;
      auto& d = meta[day];
      d.count += 1;
      d.maxImportance = std::max(d.maxImportance, n.importance);
      if (n.importance >= 2) d.hasUrgent = true;
      else if (n.importance == 1) d.hasImportant = true;
      else d.hasNormal = true;

      const int64_t prev = earliest[day];
      if (prev == 0 || occUtc < prev) {
        earliest[day] = occUtc;
        const SYSTEMTIME occLocal = TimeUtils::unixMsToSystemTimeLocal(occUtc);
        const std::wstring time = WinUtil::formatHHMM(occLocal);
        std::wstring title = n.title.empty() ? L"(без названия)" : n.title;
        if (title.size() > 22) {
          title.resize(22);
          title += L"…";
        }
        d.preview = time + L" " + title;
      }
    };

    const fs::path root = AppPaths::notesRootDir();
    const int dim = daysInMonth(year, month);
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const std::wstring id = entry.path().filename().wstring();

      Note n;
      if (!readMeta(id, n, nullptr)) {
        continue;
      }

      if (n.scheduledAtUtcMs == 0) continue;

      if (n.repeatType == RepeatType::None) {
        const SYSTEMTIME stLocal = TimeUtils::unixMsToSystemTimeLocal(n.scheduledAtUtcMs);
        if (stLocal.wYear != year || stLocal.wMonth != month) continue;
        if (stLocal.wDay < 1 || stLocal.wDay > 31) continue;
        applyMeta(stLocal.wDay, n, n.scheduledAtUtcMs);
        continue;
      }

      for (int day = 1; day <= dim; ++day) {
        SYSTEMTIME date{};
        date.wYear = static_cast<WORD>(year);
        date.wMonth = static_cast<WORD>(month);
        date.wDay = static_cast<WORD>(day);
        date = normalizeLocalDate(date);

        int64_t occUtc = 0;
        if (!occurrenceOnDateLocal(n, date, &occUtc)) continue;
        applyMeta(day, n, occUtc);
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
    constexpr int64_t kGraceAtStartMs = 30 * 1000; // allow slight delay at start time
    constexpr int64_t kRecentEditSuppressMs = 60 * 1000;   // 1 minute

    const fs::path root = AppPaths::notesRootDir();
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const std::wstring id = entry.path().filename().wstring();

      Note n;
      if (!readMeta(id, n, nullptr)) {
        continue;
      }

      if (n.scheduledAtUtcMs == 0) continue;
      if (n.updatedAtUtcMs != 0 && (nowUtcMs - n.updatedAtUtcMs) < kRecentEditSuppressMs) continue;

      if (n.repeatType == RepeatType::None) {
        if (n.hasFired) continue;
        // Уведомление показывается за reminderMinutesBefore минут до начала события
        const int64_t reminderTimeMs = n.scheduledAtUtcMs - static_cast<int64_t>(n.reminderMinutesBefore) * 60000;
        if (nowUtcMs < reminderTimeMs) continue;
        const int64_t grace = (n.reminderMinutesBefore <= 0) ? kGraceAtStartMs : 0;
        // Не показываем после времени события (кроме небольшого допуска для "в момент")
        if (nowUtcMs > n.scheduledAtUtcMs + grace) continue;
        out.push_back(std::move(n));
        continue;
      }

      int64_t occUtc = 0;
      if (!nextOccurrenceOnOrAfterNowDate(n, nowUtcMs, &occUtc)) continue;
      const int64_t reminderTimeMs = occUtc - static_cast<int64_t>(n.reminderMinutesBefore) * 60000;
      if (occUtc <= n.firedAtUtcMs) continue;
      if (nowUtcMs < reminderTimeMs) continue;
      const int64_t grace = (n.reminderMinutesBefore <= 0) ? kGraceAtStartMs : 0;
      if (nowUtcMs > occUtc + grace) continue;
      n.scheduledAtUtcMs = occUtc;
      out.push_back(std::move(n));
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


