cmake_minimum_required(VERSION 3.21)

project(AlertCalendar
  VERSION 0.2.0
  DESCRIPTION "Calendar with rich notes and reminders (Windows 10) - WinAPI build"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_SOURCES
  src/main.cpp
  src/app/AppInfo.h
  src/app/AppPaths.cpp
  src/app/AppPaths.h
  src/app/SingleInstance.cpp
  src/app/SingleInstance.h

  src/core/TimeUtils.cpp
  src/core/TimeUtils.h

  src/model/Note.h
  src/model/Note.cpp
  src/model/NoteRepository.cpp
  src/model/NoteRepository.h

  src/settings/AppSettings.cpp
  src/settings/AppSettings.h
  src/settings/AutostartWin.cpp
  src/settings/AutostartWin.h

  src/win/CalendarView.cpp
  src/win/CalendarView.h
  src/win/MainWindow.cpp
  src/win/MainWindow.h
  src/win/NotificationWindow.cpp
  src/win/NotificationWindow.h
  src/win/RichEditUtil.cpp
  src/win/RichEditUtil.h
  src/win/ImageRtf.cpp
  src/win/ImageRtf.h
  src/win/MarkupConvert.cpp
  src/win/MarkupConvert.h
  src/win/UiTheme.cpp
  src/win/UiTheme.h
  src/win/WinUtil.cpp
  src/win/WinUtil.h
)

add_executable(AlertCalendar WIN32 ${APP_SOURCES})

target_include_directories(AlertCalendar PRIVATE src)

target_compile_definitions(AlertCalendar PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(AlertCalendar PRIVATE
  comctl32
  uxtheme
  gdiplus
  shell32
  ole32
  oleaut32
  advapi32
  shlwapi
  dwmapi
  winmm
)

if (MSVC)
  target_compile_options(AlertCalendar PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(AlertCalendar PRIVATE -Wall -Wextra -Wpedantic)
endif()


