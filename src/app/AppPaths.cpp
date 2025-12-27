#include "AppPaths.h"

#include "app/AppInfo.h"

#include <ShlObj.h>

#include <filesystem>
#include <system_error>

std::filesystem::path AppPaths::appDataDir() {
  PWSTR roaming = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming);

  std::filesystem::path base;
  if (SUCCEEDED(hr) && roaming) {
    base = roaming;
    CoTaskMemFree(roaming);
  } else {
    base = std::filesystem::current_path();
  }

  base /= AppInfo::ApplicationName;
  std::filesystem::create_directories(base);
  return base;
}

std::filesystem::path AppPaths::notesRootDir() {
  auto dir = appDataDir() / L"notes";
  std::filesystem::create_directories(dir);
  return dir;
}

std::filesystem::path AppPaths::mediaRootDir() {
  auto dir = appDataDir() / L"media";
  std::filesystem::create_directories(dir);
  return dir;
}

std::filesystem::path AppPaths::noteDir(const std::wstring& noteId) {
  auto dir = notesRootDir() / noteId;
  std::filesystem::create_directories(dir);
  return dir;
}

std::filesystem::path AppPaths::noteMediaDir(const std::wstring& noteId) {
  auto dir = mediaRootDir() / noteId;
  std::filesystem::create_directories(dir);
  return dir;
}


