#include "TimeUtils.h"

namespace {
constexpr int64_t kUnixEpochDiff100ns = 116444736000000000LL; // 1601->1970 in 100ns
}

int64_t TimeUtils::unixMsNowUtc() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  return fileTimeToUnixMsUtc(ft);
}

int64_t TimeUtils::fileTimeToUnixMsUtc(const FILETIME& ftUtc) {
  ULARGE_INTEGER u{};
  u.LowPart = ftUtc.dwLowDateTime;
  u.HighPart = ftUtc.dwHighDateTime;
  const int64_t t100 = static_cast<int64_t>(u.QuadPart);
  return (t100 - kUnixEpochDiff100ns) / 10'000;
}

FILETIME TimeUtils::unixMsToFileTimeUtc(int64_t msUtc) {
  const int64_t t100 = msUtc * 10'000 + kUnixEpochDiff100ns;
  ULARGE_INTEGER u{};
  u.QuadPart = static_cast<ULONGLONG>(t100);
  FILETIME ft{};
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  return ft;
}

int64_t TimeUtils::systemTimeUtcToUnixMs(const SYSTEMTIME& stUtc) {
  FILETIME ft{};
  SystemTimeToFileTime(&stUtc, &ft);
  return fileTimeToUnixMsUtc(ft);
}

SYSTEMTIME TimeUtils::unixMsToSystemTimeUtc(int64_t msUtc) {
  FILETIME ft = unixMsToFileTimeUtc(msUtc);
  SYSTEMTIME st{};
  FileTimeToSystemTime(&ft, &st);
  return st;
}

SYSTEMTIME TimeUtils::unixMsToSystemTimeLocal(int64_t msUtc) {
  FILETIME ftUtc = unixMsToFileTimeUtc(msUtc);
  FILETIME ftLocal{};
  FileTimeToLocalFileTime(&ftUtc, &ftLocal);
  SYSTEMTIME st{};
  FileTimeToSystemTime(&ftLocal, &st);
  return st;
}

int64_t TimeUtils::localSystemTimeToUnixMsUtc(const SYSTEMTIME& stLocal) {
  SYSTEMTIME stUtc{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &stLocal, &stUtc)) {
    // fallback: treat as UTC if conversion failed
    stUtc = stLocal;
  }
  return systemTimeUtcToUnixMs(stUtc);
}

int TimeUtils::weekdaySunday0(int year, int month, int day) {
  if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  int y = year;
  if (month < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

int TimeUtils::weekdaySunday0(const SYSTEMTIME& stLocal) {
  return weekdaySunday0(static_cast<int>(stLocal.wYear),
                        static_cast<int>(stLocal.wMonth),
                        static_cast<int>(stLocal.wDay));
}


