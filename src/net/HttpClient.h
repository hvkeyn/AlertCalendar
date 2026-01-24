#pragma once

#include <string>

namespace HttpClient {

struct Response {
  int status = 0;
  std::wstring body;
  std::wstring error;
};

Response postJson(const std::wstring& url, const std::wstring& jsonBody, const std::wstring& extraHeaders, int timeoutMs);
Response getJson(const std::wstring& url, const std::wstring& extraHeaders, int timeoutMs);

} // namespace HttpClient

