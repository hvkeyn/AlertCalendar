#pragma once

#include <cstdint>
#include <windows.h>

namespace TimeUtils {
int64_t unixMsNowUtc();

int64_t fileTimeToUnixMsUtc(const FILETIME& ftUtc);
FILETIME unixMsToFileTimeUtc(int64_t msUtc);

int64_t systemTimeUtcToUnixMs(const SYSTEMTIME& stUtc);
SYSTEMTIME unixMsToSystemTimeUtc(int64_t msUtc);

SYSTEMTIME unixMsToSystemTimeLocal(int64_t msUtc);
int64_t localSystemTimeToUnixMsUtc(const SYSTEMTIME& stLocal);

// Day of week: 0=Sunday .. 6=Saturday
int weekdaySunday0(int year, int month, int day);
int weekdaySunday0(const SYSTEMTIME& stLocal);
}


