#pragma once

#include <filesystem>
#include <string>

class AppPaths {
public:
  static std::filesystem::path appDataDir();
  static std::filesystem::path notesRootDir();
  static std::filesystem::path mediaRootDir();
  static std::filesystem::path noteDir(const std::wstring& noteId);
  static std::filesystem::path noteMediaDir(const std::wstring& noteId);
};


