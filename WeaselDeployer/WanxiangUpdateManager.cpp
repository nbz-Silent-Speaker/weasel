#include "stdafx.h"
#include "WanxiangUpdateManager.h"

#include <array>
#include <cwctype>
#include <vector>

#include <winhttp.h>

namespace {
constexpr wchar_t kReleaseHost[] = L"cnb.cool";
constexpr wchar_t kReleasePath[] =
    L"/amzxyz/rime-wanxiang/-/badge/release.link";
constexpr wchar_t kReleasePrefix[] =
    L"https://cnb.cool/amzxyz/rime-wanxiang/-/releases/tag/";
constexpr wchar_t kRegistryRoot[] =
    L"Software\\Rime\\Weasel\\PackageUpdates";

class InternetHandle {
 public:
  explicit InternetHandle(HINTERNET value = nullptr) : value_(value) {}
  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;
  ~InternetHandle() {
    if (value_)
      WinHttpCloseHandle(value_);
  }
  operator HINTERNET() const { return value_; }

 private:
  HINTERNET value_;
};

std::wstring RegistryPath(const std::string& schema_id) {
  const int length = MultiByteToWideChar(CP_UTF8, 0, schema_id.c_str(), -1,
                                         nullptr, 0);
  std::wstring id(length > 0 ? length : 0, L'\0');
  if (length > 0) {
    MultiByteToWideChar(CP_UTF8, 0, schema_id.c_str(), -1, id.data(), length);
    id.resize(length - 1);
  }
  return std::wstring(kRegistryRoot) + L"\\" + id;
}

unsigned long long Interval100Nanoseconds(
    WanxiangUpdateManager::Frequency frequency) {
  constexpr unsigned long long kDay = 24ull * 60 * 60 * 10000000;
  switch (frequency) {
    case WanxiangUpdateManager::Frequency::Daily:
      return kDay;
    case WanxiangUpdateManager::Frequency::Weekly:
      return 7 * kDay;
    case WanxiangUpdateManager::Frequency::Monthly:
      return 30 * kDay;
    default:
      return 0;
  }
}

ULONGLONG CurrentFileTime() {
  FILETIME file_time = {};
  GetSystemTimeAsFileTime(&file_time);
  ULARGE_INTEGER value = {};
  value.LowPart = file_time.dwLowDateTime;
  value.HighPart = file_time.dwHighDateTime;
  return value.QuadPart;
}

bool ReadRegistryQword(const wchar_t* name, ULONGLONG* value) {
  DWORD size = sizeof(*value);
  return RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, name, RRF_RT_REG_QWORD,
                      nullptr, value, &size) == ERROR_SUCCESS;
}

bool ParseVersion(const std::wstring& text, std::array<unsigned long, 3>* parts) {
  size_t position =
      !text.empty() && (text[0] == L'v' || text[0] == L'V') ? 1 : 0;
  for (size_t index = 0; index < parts->size(); ++index) {
    if (position >= text.size() || !iswdigit(text[position]))
      return false;
    unsigned long value = 0;
    while (position < text.size() && iswdigit(text[position])) {
      value = value * 10 + static_cast<unsigned long>(text[position] - L'0');
      ++position;
    }
    (*parts)[index] = value;
    if (index + 1 < parts->size()) {
      if (position >= text.size() || text[position] != L'.')
        return false;
      ++position;
    }
  }
  return true;
}

int CompareVersions(const std::wstring& left, const std::wstring& right) {
  std::array<unsigned long, 3> left_parts = {};
  std::array<unsigned long, 3> right_parts = {};
  if (!ParseVersion(left, &left_parts) || !ParseVersion(right, &right_parts))
    return 0;
  if (left_parts < right_parts)
    return -1;
  if (left_parts > right_parts)
    return 1;
  return 0;
}
}  // namespace

WanxiangUpdateManager::Frequency WanxiangUpdateManager::LoadFrequency(
    const std::string& schema_id) {
  DWORD value = static_cast<DWORD>(Frequency::Weekly);
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, RegistryPath(schema_id).c_str(),
                   L"CheckFrequency", RRF_RT_REG_DWORD, nullptr, &value,
                   &size) != ERROR_SUCCESS ||
      value > static_cast<DWORD>(Frequency::Disabled)) {
    return Frequency::Weekly;
  }
  return static_cast<Frequency>(value);
}

bool WanxiangUpdateManager::SaveFrequency(const std::string& schema_id,
                                           Frequency frequency) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, RegistryPath(schema_id).c_str(), 0,
                      nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                      &key, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  const DWORD value = static_cast<DWORD>(frequency);
  const LSTATUS result = RegSetValueExW(
      key, L"CheckFrequency", 0, REG_DWORD,
      reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool WanxiangUpdateManager::LoadLastCheck(std::wstring* tag,
                                          SYSTEMTIME* local_time) {
  if (!tag || !local_time)
    return false;
  ULONGLONG value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, L"LastChecked",
                   RRF_RT_REG_QWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t release[128] = {};
  size = sizeof(release);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, L"LastReleaseTag",
                   RRF_RT_REG_SZ, nullptr, release, &size) != ERROR_SUCCESS) {
    return false;
  }
  ULARGE_INTEGER time = {};
  time.QuadPart = value;
  FILETIME utc = {time.LowPart, time.HighPart};
  FILETIME local = {};
  if (!FileTimeToLocalFileTime(&utc, &local) ||
      !FileTimeToSystemTime(&local, local_time)) {
    return false;
  }
  *tag = release;
  return true;
}

bool WanxiangUpdateManager::IsAutomaticCheckDue(Frequency frequency) {
  if (frequency == Frequency::Disabled)
    return false;
  ULONGLONG last_attempt = 0;
  if (!ReadRegistryQword(L"LastAttempt", &last_attempt))
    return true;

  unsigned long long interval = Interval100Nanoseconds(frequency);
  if (frequency == Frequency::Smart) {
    constexpr unsigned long long kDay = 24ull * 60 * 60 * 10000000;
    wchar_t release[128] = {};
    DWORD size = sizeof(release);
    const bool has_release =
        RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, L"LastReleaseTag",
                     RRF_RT_REG_SZ, nullptr, release, &size) == ERROR_SUCCESS;
    std::wstring normalized = has_release ? release : L"";
    if (!normalized.empty() &&
        (normalized.front() == L'v' || normalized.front() == L'V')) {
      normalized.erase(normalized.begin());
    }
    interval = normalized == kInstalledVersion ? 30 * kDay : 7 * kDay;
  }
  return CurrentFileTime() >= last_attempt + interval;
}

void WanxiangUpdateManager::SaveLastAttempt() {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  const ULONGLONG attempt = CurrentFileTime();
  RegSetValueExW(key, L"LastAttempt", 0, REG_QWORD,
                 reinterpret_cast<const BYTE*>(&attempt), sizeof(attempt));
  RegCloseKey(key);
}

void WanxiangUpdateManager::SaveLastCheck(const std::wstring& tag) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  FILETIME checked = {};
  GetSystemTimeAsFileTime(&checked);
  ULARGE_INTEGER time = {};
  time.LowPart = checked.dwLowDateTime;
  time.HighPart = checked.dwHighDateTime;
  RegSetValueExW(key, L"LastChecked", 0, REG_QWORD,
                 reinterpret_cast<const BYTE*>(&time.QuadPart),
                 sizeof(time.QuadPart));
  RegSetValueExW(key, L"LastReleaseTag", 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(tag.c_str()),
                 static_cast<DWORD>((tag.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
}

WanxiangUpdateManager::Result WanxiangUpdateManager::CheckNow() {
  Result result;
  SaveLastAttempt();
  result.latest_tag = QueryLatestRelease(&result.error);
  if (result.latest_tag.empty())
    return result;
  SaveLastCheck(result.latest_tag);
  result.success = true;
  result.update_available =
      CompareVersions(result.latest_tag, kInstalledVersion) > 0;
  return result;
}

std::wstring WanxiangUpdateManager::QueryLatestRelease(std::wstring* error) {
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
  InternetHandle session(WinHttpOpen(
      L"WeaselDeployer/UpdateCheck", kProxyMode, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    *error = L"WinHTTP initialization failed.";
    return {};
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

  InternetHandle connection(
      WinHttpConnect(session, kReleaseHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) {
    *error = L"Unable to connect to the update source.";
    return {};
  }
  InternetHandle request(WinHttpOpenRequest(
      connection, L"GET", kReleasePath, nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) {
    *error = L"Unable to create the update request.";
    return {};
  }
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                        &redirect_policy, sizeof(redirect_policy)) ||
      !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    *error = L"The update source did not respond.";
    return {};
  }
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                           WINHTTP_NO_HEADER_INDEX) ||
      status < 300 || status >= 400) {
    *error = L"The update source returned an unexpected response.";
    return {};
  }
  DWORD location_size = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                      WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                      &location_size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !location_size) {
    *error = L"The update response did not contain a release address.";
    return {};
  }
  std::vector<wchar_t> location(location_size / sizeof(wchar_t));
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                           WINHTTP_HEADER_NAME_BY_INDEX, location.data(),
                           &location_size, WINHTTP_NO_HEADER_INDEX)) {
    *error = L"Unable to read the latest release address.";
    return {};
  }
  const std::wstring address(location.data());
  const std::wstring prefix(kReleasePrefix);
  if (address.compare(0, prefix.size(), prefix) != 0) {
    *error = L"The update source returned an unrecognized release address.";
    return {};
  }
  std::wstring tag = address.substr(prefix.size());
  const auto delimiter = tag.find_first_of(L"?#/");
  if (delimiter != std::wstring::npos)
    tag.resize(delimiter);
  return tag;
}
