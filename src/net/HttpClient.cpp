#include "HttpClient.h"

#include "win/WinUtil.h"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring lastWinHttpError(const wchar_t* prefix) {
  const std::wstring msg = WinUtil::lastErrorMessage();
  return std::wstring(prefix) + L": " + msg;
}

} // namespace

HttpClient::Response HttpClient::postJson(const std::wstring& url, const std::wstring& jsonBody, const std::wstring& extraHeaders, int timeoutMs) {
  Response res;

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);

  std::wstring urlCopy = url;
  if (!WinHttpCrackUrl(urlCopy.data(), static_cast<DWORD>(urlCopy.size()), 0, &parts)) {
    res.error = lastWinHttpError(L"Некорректный URL");
    return res;
  }

  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength > 0) {
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  }
  const bool isHttps = (parts.nScheme == INTERNET_SCHEME_HTTPS);

  HINTERNET hSession = WinHttpOpen(L"AlertCalendar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    res.error = lastWinHttpError(L"WinHttpOpen");
    return res;
  }
  if (timeoutMs < 1000) timeoutMs = 1000;
  WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), parts.nPort, 0);
  if (!hConnect) {
    res.error = lastWinHttpError(L"WinHttpConnect");
    WinHttpCloseHandle(hSession);
    return res;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
    hConnect,
    L"POST",
    path.c_str(),
    nullptr,
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    isHttps ? WINHTTP_FLAG_SECURE : 0
  );
  if (!hRequest) {
    res.error = lastWinHttpError(L"WinHttpOpenRequest");
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  std::wstring headers = L"Content-Type: application/json; charset=utf-8\r\n";
  if (!extraHeaders.empty()) {
    headers += extraHeaders;
    if (headers.back() != L'\n') headers += L"\r\n";
  }

  const std::string bodyUtf8 = WinUtil::toUtf8(jsonBody);
  BOOL ok = WinHttpSendRequest(
    hRequest,
    headers.c_str(),
    static_cast<DWORD>(headers.size()),
    bodyUtf8.empty() ? WINHTTP_NO_REQUEST_DATA : reinterpret_cast<LPVOID>(const_cast<char*>(bodyUtf8.data())),
    static_cast<DWORD>(bodyUtf8.size()),
    static_cast<DWORD>(bodyUtf8.size()),
    0
  );
  if (!ok) {
    res.error = lastWinHttpError(L"WinHttpSendRequest");
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  ok = WinHttpReceiveResponse(hRequest, nullptr);
  if (!ok) {
    res.error = lastWinHttpError(L"WinHttpReceiveResponse");
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
    res.status = static_cast<int>(statusCode);
  }

  std::string data;
  for (;;) {
    DWORD size = 0;
    if (!WinHttpQueryDataAvailable(hRequest, &size)) {
      res.error = lastWinHttpError(L"WinHttpQueryDataAvailable");
      break;
    }
    if (size == 0) break;
    std::vector<char> buffer(size);
    DWORD read = 0;
    if (!WinHttpReadData(hRequest, buffer.data(), size, &read)) {
      res.error = lastWinHttpError(L"WinHttpReadData");
      break;
    }
    if (read > 0) {
      data.append(buffer.data(), buffer.data() + read);
    }
  }

  if (!data.empty()) {
    res.body = WinUtil::fromUtf8(data);
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return res;
}

HttpClient::Response HttpClient::getJson(const std::wstring& url, const std::wstring& extraHeaders, int timeoutMs) {
  Response res;

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);

  std::wstring urlCopy = url;
  if (!WinHttpCrackUrl(urlCopy.data(), static_cast<DWORD>(urlCopy.size()), 0, &parts)) {
    res.error = lastWinHttpError(L"Некорректный URL");
    return res;
  }

  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength > 0) {
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  }
  const bool isHttps = (parts.nScheme == INTERNET_SCHEME_HTTPS);

  HINTERNET hSession = WinHttpOpen(L"AlertCalendar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    res.error = lastWinHttpError(L"WinHttpOpen");
    return res;
  }
  if (timeoutMs < 1000) timeoutMs = 1000;
  WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), parts.nPort, 0);
  if (!hConnect) {
    res.error = lastWinHttpError(L"WinHttpConnect");
    WinHttpCloseHandle(hSession);
    return res;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
    hConnect,
    L"GET",
    path.c_str(),
    nullptr,
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    isHttps ? WINHTTP_FLAG_SECURE : 0
  );
  if (!hRequest) {
    res.error = lastWinHttpError(L"WinHttpOpenRequest");
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  std::wstring headers = L"Accept: application/json\r\n";
  if (!extraHeaders.empty()) {
    headers += extraHeaders;
    if (headers.back() != L'\n') headers += L"\r\n";
  }

  BOOL ok = WinHttpSendRequest(
    hRequest,
    headers.c_str(),
    static_cast<DWORD>(headers.size()),
    WINHTTP_NO_REQUEST_DATA,
    0,
    0,
    0
  );
  if (!ok) {
    res.error = lastWinHttpError(L"WinHttpSendRequest");
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  ok = WinHttpReceiveResponse(hRequest, nullptr);
  if (!ok) {
    res.error = lastWinHttpError(L"WinHttpReceiveResponse");
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
    res.status = static_cast<int>(statusCode);
  }

  std::string data;
  for (;;) {
    DWORD size = 0;
    if (!WinHttpQueryDataAvailable(hRequest, &size)) {
      res.error = lastWinHttpError(L"WinHttpQueryDataAvailable");
      break;
    }
    if (size == 0) break;
    std::vector<char> buffer(size);
    DWORD read = 0;
    if (!WinHttpReadData(hRequest, buffer.data(), size, &read)) {
      res.error = lastWinHttpError(L"WinHttpReadData");
      break;
    }
    if (read > 0) {
      data.append(buffer.data(), buffer.data() + read);
    }
  }

  if (!data.empty()) {
    res.body = WinUtil::fromUtf8(data);
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return res;
}

