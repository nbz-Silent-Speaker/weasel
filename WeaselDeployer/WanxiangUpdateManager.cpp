#include "stdafx.h"
#include "WanxiangUpdateManager.h"

#include "WanxiangModelManager.h"

#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include <winhttp.h>
#include <WeaselUtility.h>

namespace {
constexpr wchar_t kReleaseHost[] = L"cnb.cool";
constexpr wchar_t kReleasePath[] =
    L"/amzxyz/rime-wanxiang/-/badge/release.link";
constexpr wchar_t kCnbReleasesPath[] = L"/amzxyz/rime-wanxiang/-/releases";
constexpr wchar_t kReleasePrefix[] =
    L"https://cnb.cool/amzxyz/rime-wanxiang/-/releases/tag/";
constexpr wchar_t kGithubHost[] = L"github.com";
constexpr wchar_t kGithubReleasePath[] =
    L"/amzxyz/rime-wanxiang/releases/latest";
constexpr wchar_t kGithubApiHost[] = L"api.github.com";
constexpr wchar_t kGithubReleasesPath[] =
    L"/repos/amzxyz/rime-wanxiang/releases?per_page=30";
constexpr wchar_t kGithubReleasePrefix[] =
    L"https://github.com/amzxyz/rime-wanxiang/releases/tag/";
constexpr wchar_t kGithubRelativeReleasePrefix[] =
    L"/amzxyz/rime-wanxiang/releases/tag/";
constexpr wchar_t kModelReleasePath[] =
    L"/amzxyz/rime-wanxiang/-/releases/tag/model";
constexpr char kModelAssetPath[] =
    "/amzxyz/rime-wanxiang/-/releases/download/model/"
    "wanxiang-lts-zh-hans.gram";
constexpr char kSchemeAssetName[] = "rime-wanxiang-lite.zip";
constexpr wchar_t kRegistryRoot[] = L"Software\\Rime\\Weasel\\PackageUpdates";
constexpr size_t kMaximumModelReleasePage = 2 * 1024 * 1024;
constexpr size_t kMaximumSchemeReleaseResponse = 8 * 1024 * 1024;

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
  const int length =
      MultiByteToWideChar(CP_UTF8, 0, schema_id.c_str(), -1, nullptr, 0);
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

bool ReadRegistryString(const wchar_t* name, std::wstring* value) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, name, RRF_RT_REG_SZ,
                   nullptr, nullptr, &size) != ERROR_SUCCESS ||
      size < sizeof(wchar_t)) {
    return false;
  }
  std::vector<wchar_t> buffer(size / sizeof(wchar_t));
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, name, RRF_RT_REG_SZ,
                   nullptr, buffer.data(), &size) != ERROR_SUCCESS) {
    return false;
  }
  *value = buffer.data();
  return true;
}

bool LoadInstalledModel(std::wstring* sha256, unsigned long long* size) {
  std::error_code file_error;
  if (!std::filesystem::exists(
          WeaselUserDataPath() / L"wanxiang-lts-zh-hans.gram", file_error) ||
      file_error) {
    return false;
  }
  ULONGLONG installed_size = 0;
  if (ReadRegistryString(L"InstalledModelSha256", sha256) &&
      ReadRegistryQword(L"InstalledModelSize", &installed_size) &&
      installed_size) {
    *size = installed_size;
    return true;
  }
  sha256->assign(WanxiangModelManager::kExpectedSha256,
                 WanxiangModelManager::kExpectedSha256 +
                     std::char_traits<char>::length(
                         WanxiangModelManager::kExpectedSha256));
  *size = WanxiangModelManager::kExpectedSize;
  return true;
}

void WriteAvailableCount(unsigned int count) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  const DWORD value = count;
  RegSetValueExW(key, L"AvailableCount", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
}

bool IsSha256(const std::string& value) {
  if (value.size() != 64)
    return false;
  for (const unsigned char character : value) {
    if (!std::isxdigit(character))
      return false;
  }
  return true;
}

bool ExtractJsonString(const std::string& object,
                       const std::string& name,
                       std::string* value) {
  const std::string marker = "\"" + name + "\":\"";
  const auto start = object.find(marker);
  if (start == std::string::npos)
    return false;
  const auto value_start = start + marker.size();
  const auto end = object.find('"', value_start);
  if (end == std::string::npos)
    return false;
  *value = object.substr(value_start, end - value_start);
  return true;
}

bool ExtractJsonUnsigned(const std::string& object,
                         const std::string& name,
                         unsigned long long* value) {
  const std::string marker = "\"" + name + "\":";
  auto position = object.find(marker);
  if (position == std::string::npos)
    return false;
  position += marker.size();
  if (position >= object.size() ||
      !std::isdigit(static_cast<unsigned char>(object[position])))
    return false;
  unsigned long long result = 0;
  while (position < object.size() &&
         std::isdigit(static_cast<unsigned char>(object[position]))) {
    const unsigned int digit = object[position] - '0';
    if (result >
        ((std::numeric_limits<unsigned long long>::max)() - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
    ++position;
  }
  *value = result;
  return true;
}

bool ParseVersion(const std::wstring& text,
                  std::array<unsigned long, 3>* parts) {
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
  return position == text.size();
}

bool ExtractJsonBoolean(const std::string& object,
                        const std::string& name,
                        bool* value) {
  const std::string marker = "\"" + name + "\":";
  const auto position = object.find(marker);
  if (position == std::string::npos)
    return false;
  const auto value_position = position + marker.size();
  if (object.compare(value_position, 4, "true") == 0) {
    *value = true;
    return true;
  }
  if (object.compare(value_position, 5, "false") == 0) {
    *value = false;
    return true;
  }
  return false;
}

bool ExtractJsonArray(const std::string& object,
                      const std::string& name,
                      std::string* value) {
  const std::string marker = "\"" + name + "\":[";
  const auto marker_position = object.find(marker);
  if (marker_position == std::string::npos)
    return false;
  const auto start = marker_position + marker.size() - 1;
  size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t position = start; position < object.size(); ++position) {
    const char character = object[position];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        in_string = false;
      }
      continue;
    }
    if (character == '"') {
      in_string = true;
    } else if (character == '[') {
      ++depth;
    } else if (character == ']') {
      if (!--depth) {
        *value = object.substr(start, position - start + 1);
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> SplitJsonArrayObjects(const std::string& array) {
  std::vector<std::string> objects;
  size_t start = std::string::npos;
  size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t position = 0; position < array.size(); ++position) {
    const char character = array[position];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        in_string = false;
      }
      continue;
    }
    if (character == '"') {
      in_string = true;
    } else if (character == '{') {
      if (!depth)
        start = position;
      ++depth;
    } else if (character == '}' && depth) {
      if (!--depth && start != std::string::npos) {
        objects.push_back(array.substr(start, position - start + 1));
        start = std::string::npos;
      }
    }
  }
  return objects;
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

bool ReadHttpsBody(const wchar_t* host,
                   const wchar_t* path,
                   const wchar_t* accept,
                   size_t maximum_size,
                   std::string* body,
                   std::wstring* error) {
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
  InternetHandle session(WinHttpOpen(L"WeaselDeployer/UpdateCheck", kProxyMode,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    *error = L"WinHTTP initialization failed.";
    return false;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 15000);
  InternetHandle connection(
      WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) {
    *error = L"Unable to connect to the update source.";
    return false;
  }
  InternetHandle request(
      WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) {
    *error = L"Unable to create the update request.";
    return false;
  }
  std::wstring headers = L"Accept: ";
  headers += accept;
  headers += L"\r\nCache-Control: no-cache\r\n";
  if (!WinHttpAddRequestHeaders(
          request, headers.c_str(), -1,
          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
      !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    *error = L"The update source did not respond.";
    return false;
  }
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
          WINHTTP_NO_HEADER_INDEX) ||
      status != 200) {
    *error = L"The update source returned an unexpected response.";
    return false;
  }
  body->clear();
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      *error = L"Unable to read the update response.";
      return false;
    }
    if (!available)
      return true;
    if (body->size() + available > maximum_size) {
      *error = L"The update response was unexpectedly large.";
      return false;
    }
    const auto offset = body->size();
    body->resize(offset + available);
    DWORD received = 0;
    if (!WinHttpReadData(request, body->data() + offset, available,
                         &received) ||
        !received) {
      *error = L"The update response ended unexpectedly.";
      return false;
    }
    body->resize(offset + received);
  }
}

bool ParseSchemeReleaseList(const std::string& json,
                            bool cnb,
                            WanxiangUpdateManager::SchemeRelease* release) {
  if (!release)
    return false;
  WanxiangUpdateManager::SchemeRelease best;
  for (const auto& object : SplitJsonArrayObjects(json)) {
    std::string tag_utf8;
    bool draft = false;
    bool prerelease = false;
    if (!ExtractJsonString(object, "tag_name", &tag_utf8) ||
        (ExtractJsonBoolean(object, "draft", &draft) && draft) ||
        (ExtractJsonBoolean(object, "prerelease", &prerelease) && prerelease)) {
      continue;
    }
    const std::wstring tag = u8tow(tag_utf8);
    std::array<unsigned long, 3> ignored = {};
    if (!ParseVersion(tag, &ignored) ||
        (!best.tag.empty() && CompareVersions(tag, best.tag) <= 0)) {
      continue;
    }
    std::string assets;
    if (!ExtractJsonArray(object, "assets", &assets))
      continue;
    for (const auto& asset : SplitJsonArrayObjects(assets)) {
      std::string name;
      if (!ExtractJsonString(asset, "name", &name) ||
          name != kSchemeAssetName) {
        continue;
      }
      std::string digest;
      std::string algorithm;
      unsigned long long size = 0;
      if (cnb) {
        if (!ExtractJsonString(asset, "hash_algo", &algorithm) ||
            algorithm != "sha256" ||
            !ExtractJsonString(asset, "hash_value", &digest)) {
          continue;
        }
      } else {
        if (!ExtractJsonString(asset, "digest", &digest) ||
            digest.compare(0, 7, "sha256:") != 0) {
          continue;
        }
        digest.erase(0, 7);
      }
      if (!IsSha256(digest) || !ExtractJsonUnsigned(asset, "size", &size) ||
          !size) {
        continue;
      }
      for (auto& character : digest)
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
      best.tag = tag;
      best.sha256.assign(digest.begin(), digest.end());
      best.size = size;
      best.from_cnb = cnb;
      best.url = cnb ? L"https://cnb.cool/amzxyz/rime-wanxiang/-/releases/"
                       L"download/"
                     : L"https://github.com/amzxyz/rime-wanxiang/releases/"
                       L"download/";
      best.url += tag + L"/" + u8tow(kSchemeAssetName);
      break;
    }
  }
  if (best.tag.empty())
    return false;
  *release = std::move(best);
  return true;
}

std::wstring QueryReleaseRedirect(const wchar_t* host,
                                  const wchar_t* path,
                                  std::wstring* error) {
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
  InternetHandle session(WinHttpOpen(L"WeaselDeployer/UpdateCheck", kProxyMode,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    *error = L"WinHTTP initialization failed.";
    return {};
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

  InternetHandle connection(
      WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) {
    *error = L"Unable to connect to the update source.";
    return {};
  }
  InternetHandle request(
      WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
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
  if (!WinHttpQueryHeaders(
          request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
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
  std::wstring tag;
  if (!WanxiangUpdateManager::ParseReleaseAddress(location.data(), &tag)) {
    *error = L"The update source returned an unrecognized release address.";
    return {};
  }
  return tag;
}
}  // namespace

bool WanxiangUpdateManager::ParseReleaseAddress(const std::wstring& address,
                                                std::wstring* tag) {
  if (!tag)
    return false;
  constexpr const wchar_t* prefixes[] = {kReleasePrefix, kGithubReleasePrefix,
                                         kGithubRelativeReleasePrefix};
  for (const wchar_t* prefix : prefixes) {
    const size_t prefix_length = std::char_traits<wchar_t>::length(prefix);
    if (address.compare(0, prefix_length, prefix) != 0)
      continue;
    std::wstring candidate = address.substr(prefix_length);
    const auto delimiter = candidate.find_first_of(L"?#/");
    if (delimiter != std::wstring::npos)
      candidate.resize(delimiter);
    std::array<unsigned long, 3> ignored = {};
    if (!ParseVersion(candidate, &ignored))
      return false;
    *tag = std::move(candidate);
    return true;
  }
  return false;
}

bool WanxiangUpdateManager::IsNewerVersion(const std::wstring& candidate,
                                           const std::wstring& baseline) {
  return CompareVersions(candidate, baseline) > 0;
}

std::wstring WanxiangUpdateManager::LoadInstalledSchemeVersion() {
  std::ifstream version_file(WeaselUserDataPath() / L"version.txt",
                             std::ios::binary);
  std::string version_utf8;
  if (version_file)
    std::getline(version_file, version_utf8);
  if (version_utf8.size() >= 3 &&
      static_cast<unsigned char>(version_utf8[0]) == 0xef &&
      static_cast<unsigned char>(version_utf8[1]) == 0xbb &&
      static_cast<unsigned char>(version_utf8[2]) == 0xbf) {
    version_utf8.erase(0, 3);
  }
  while (!version_utf8.empty() &&
         std::isspace(static_cast<unsigned char>(version_utf8.back()))) {
    version_utf8.pop_back();
  }
  std::wstring version = u8tow(version_utf8);
  std::array<unsigned long, 3> ignored = {};
  if (ParseVersion(version, &ignored))
    return version;
  if (ReadRegistryString(L"InstalledSchemeVersion", &version) &&
      ParseVersion(version, &ignored)) {
    return version;
  }
  return kInstalledVersion;
}

bool WanxiangUpdateManager::ParseCnbSchemeReleases(const std::string& json,
                                                   SchemeRelease* release) {
  return ParseSchemeReleaseList(json, true, release);
}

bool WanxiangUpdateManager::ParseGithubSchemeReleases(const std::string& json,
                                                      SchemeRelease* release) {
  return ParseSchemeReleaseList(json, false, release);
}

bool WanxiangUpdateManager::QueryGithubSchemeRelease(const std::wstring& tag,
                                                     SchemeRelease* release,
                                                     std::wstring* error) {
  std::array<unsigned long, 3> ignored = {};
  if (!release || !error || !ParseVersion(tag, &ignored))
    return false;
  const std::wstring path = L"/repos/amzxyz/rime-wanxiang/releases/tags/" + tag;
  std::string response;
  if (!ReadHttpsBody(kGithubApiHost, path.c_str(),
                     L"application/vnd.github+json",
                     kMaximumSchemeReleaseResponse, &response, error)) {
    return false;
  }
  response.insert(response.begin(), '[');
  response.push_back(']');
  if (!ParseGithubSchemeReleases(response, release) || release->tag != tag) {
    *error = L"The matching GitHub release metadata was not recognized.";
    return false;
  }
  return true;
}

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
  const LSTATUS result =
      RegSetValueExW(key, L"CheckFrequency", 0, REG_DWORD,
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

bool WanxiangUpdateManager::LoadLastModelMetadata(std::wstring* sha256,
                                                  unsigned long long* size) {
  if (!sha256 || !size)
    return false;
  std::wstring latest;
  ULONGLONG latest_size = 0;
  if (!ReadRegistryString(L"LastModelSha256", &latest) ||
      !ReadRegistryQword(L"LastModelSize", &latest_size)) {
    return false;
  }
  const std::string latest_ascii = wtou8(latest);
  if (!IsSha256(latest_ascii) || !latest_size)
    return false;
  *sha256 = latest;
  *size = latest_size;
  return true;
}

bool WanxiangUpdateManager::LoadCachedResult(Result* result) {
  if (!result)
    return false;
  SYSTEMTIME ignored = {};
  Result cached;
  if (!LoadLastCheck(&cached.latest_tag, &ignored)) {
    return false;
  }
  cached.success = true;
  cached.installed_scheme_version = LoadInstalledSchemeVersion();
  cached.scheme_update_available =
      IsNewerVersion(cached.latest_tag, cached.installed_scheme_version);
  if (LoadLastModelMetadata(&cached.latest_model_sha256,
                            &cached.latest_model_size)) {
    std::wstring installed_sha256;
    unsigned long long installed_size = 0;
    const bool model_installed =
        LoadInstalledModel(&installed_sha256, &installed_size);
    cached.model_update_available =
        model_installed && (cached.latest_model_sha256 != installed_sha256 ||
                            cached.latest_model_size != installed_size);
  }
  cached.update_available =
      cached.scheme_update_available || cached.model_update_available;
  *result = std::move(cached);
  return true;
}

unsigned int WanxiangUpdateManager::LoadAvailableCount() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryRoot, L"AvailableCount",
                   RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return 0;
  }
  return (std::min)(value, 99ul);
}

void WanxiangUpdateManager::StoreAvailableCount(unsigned int count) {
  WriteAvailableCount((std::min)(count, 99u));
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

void WanxiangUpdateManager::SaveLastRelease(const std::wstring& tag) {
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
  // The model source is checked independently.  Do not let metadata from an
  // earlier successful check masquerade as part of this fresh result if the
  // model endpoint fails afterwards.
  RegDeleteValueW(key, L"LastModelSha256");
  RegDeleteValueW(key, L"LastModelSize");
  RegCloseKey(key);
}

void WanxiangUpdateManager::SaveLastModel(const ModelRelease& model) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  const std::wstring baseline(WanxiangModelManager::kExpectedSha256,
                              WanxiangModelManager::kExpectedSha256 +
                                  std::char_traits<char>::length(
                                      WanxiangModelManager::kExpectedSha256));
  RegSetValueExW(key, L"LastModelBaselineSha256", 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(baseline.c_str()),
                 static_cast<DWORD>((baseline.size() + 1) * sizeof(wchar_t)));
  RegSetValueExW(
      key, L"LastModelSha256", 0, REG_SZ,
      reinterpret_cast<const BYTE*>(model.sha256.c_str()),
      static_cast<DWORD>((model.sha256.size() + 1) * sizeof(wchar_t)));
  const ULONGLONG model_size = model.size;
  RegSetValueExW(key, L"LastModelSize", 0, REG_QWORD,
                 reinterpret_cast<const BYTE*>(&model_size),
                 sizeof(model_size));
  RegCloseKey(key);
}

WanxiangUpdateManager::Result WanxiangUpdateManager::CheckNow() {
  Result result;
  SaveLastAttempt();
  SchemeRelease scheme;
  if (!QueryLatestScheme(&scheme, &result.error))
    return result;
  result.latest_tag = scheme.tag;
  result.latest_scheme_url = scheme.url;
  result.latest_scheme_sha256 = scheme.sha256;
  result.latest_scheme_size = scheme.size;
  result.latest_scheme_from_cnb = scheme.from_cnb;
  result.installed_scheme_version = LoadInstalledSchemeVersion();
  result.success = true;
  result.scheme_update_available =
      IsNewerVersion(result.latest_tag, result.installed_scheme_version);
  SaveLastRelease(result.latest_tag);

  ModelRelease model;
  std::wstring model_error;
  if (QueryLatestModel(&model, &model_error)) {
    SaveLastModel(model);
    result.latest_model_sha256 = model.sha256;
    result.latest_model_size = model.size;
    std::wstring installed_sha256;
    unsigned long long installed_size = 0;
    const bool model_installed =
        LoadInstalledModel(&installed_sha256, &installed_size);
    result.model_update_available =
        model_installed &&
        (model.sha256 != installed_sha256 || model.size != installed_size);
  } else {
    result.error = std::move(model_error);
  }
  result.update_available =
      result.scheme_update_available || result.model_update_available;
  WriteAvailableCount(static_cast<unsigned int>(result.scheme_update_available +
                                                result.model_update_available));
  return result;
}

bool WanxiangUpdateManager::ParseModelRelease(const std::string& page,
                                              ModelRelease* model) {
  if (!model)
    return false;
  const std::string path_marker =
      "\"path\":\"" + std::string(kModelAssetPath) + "\"";
  const auto path_position = page.find(path_marker);
  if (path_position == std::string::npos)
    return false;
  const auto object_start = page.rfind("{\"id\":", path_position);
  const auto object_end = page.find("\"author\":", path_position);
  if (object_start == std::string::npos || object_end == std::string::npos ||
      object_end <= object_start || object_end - object_start > 4096) {
    return false;
  }
  const auto object = page.substr(object_start, object_end - object_start);
  std::string algorithm;
  std::string digest;
  unsigned long long size = 0;
  if (!ExtractJsonString(object, "hashAlgo", &algorithm) ||
      algorithm != "sha256" ||
      !ExtractJsonString(object, "hashValue", &digest) || !IsSha256(digest) ||
      !ExtractJsonUnsigned(object, "sizeInByte", &size) || !size) {
    return false;
  }
  for (auto& character : digest)
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  model->sha256.assign(digest.begin(), digest.end());
  model->size = size;
  return true;
}

bool WanxiangUpdateManager::QueryLatestModel(ModelRelease* model,
                                             std::wstring* error) {
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
  InternetHandle session(WinHttpOpen(L"WeaselDeployer/ModelUpdateCheck",
                                     kProxyMode, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    *error = L"WinHTTP initialization failed.";
    return false;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

  InternetHandle connection(
      WinHttpConnect(session, kReleaseHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) {
    *error = L"Unable to connect to the model update source.";
    return false;
  }
  InternetHandle request(WinHttpOpenRequest(
      connection, L"GET", kModelReleasePath, nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request ||
      !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    *error = L"The model update source did not respond.";
    return false;
  }
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
          WINHTTP_NO_HEADER_INDEX) ||
      status != 200) {
    *error = L"The model update source returned an unexpected response.";
    return false;
  }

  std::string page;
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      *error = L"Unable to read the model update response.";
      return false;
    }
    if (!available)
      break;
    if (page.size() + available > kMaximumModelReleasePage) {
      *error = L"The model update response was unexpectedly large.";
      return false;
    }
    const auto offset = page.size();
    page.resize(offset + available);
    DWORD received = 0;
    if (!WinHttpReadData(request, page.data() + offset, available, &received)) {
      *error = L"Unable to read the model update response.";
      return false;
    }
    if (!received) {
      *error = L"The model update response ended unexpectedly.";
      return false;
    }
    page.resize(offset + received);
  }
  if (!ParseModelRelease(page, model)) {
    *error = L"The model release metadata was not recognized.";
    return false;
  }
  return true;
}

bool WanxiangUpdateManager::QueryLatestScheme(SchemeRelease* release,
                                              std::wstring* error) {
  std::wstring cnb_error;
  std::string response;
  if (ReadHttpsBody(kReleaseHost, kCnbReleasesPath, L"application/json",
                    kMaximumSchemeReleaseResponse, &response, &cnb_error) &&
      ParseCnbSchemeReleases(response, release)) {
    return true;
  }
  if (cnb_error.empty())
    cnb_error = L"The CNB release metadata was not recognized.";

  std::wstring github_error;
  response.clear();
  if (ReadHttpsBody(kGithubApiHost, kGithubReleasesPath,
                    L"application/vnd.github+json",
                    kMaximumSchemeReleaseResponse, &response, &github_error) &&
      ParseGithubSchemeReleases(response, release)) {
    return true;
  }
  if (github_error.empty())
    github_error = L"The GitHub release metadata was not recognized.";
  *error = L"CNB: " + cnb_error + L" GitHub: " + github_error;
  return false;
}
