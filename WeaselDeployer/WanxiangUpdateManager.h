#pragma once

#include <windows.h>

#include <string>

class WanxiangUpdateManager {
 public:
  enum class Frequency : unsigned long {
    Smart = 0,
    Daily = 1,
    Weekly = 2,
    Monthly = 3,
    Disabled = 4,
  };

  struct Result {
    bool success = false;
    bool update_available = false;
    bool scheme_update_available = false;
    bool model_update_available = false;
    std::wstring latest_tag;
    std::wstring installed_scheme_version;
    std::wstring latest_scheme_url;
    std::wstring latest_scheme_sha256;
    unsigned long long latest_scheme_size = 0;
    bool latest_scheme_from_cnb = false;
    std::wstring latest_model_sha256;
    unsigned long long latest_model_size = 0;
    std::wstring error;
  };

  struct SchemeRelease {
    std::wstring tag;
    std::wstring url;
    std::wstring sha256;
    unsigned long long size = 0;
    bool from_cnb = false;
  };

  static constexpr wchar_t kInstalledVersion[] = L"17.10.0";

  static Frequency LoadFrequency(const std::string& schema_id);
  static bool SaveFrequency(const std::string& schema_id, Frequency frequency);
  static bool LoadLastCheck(std::wstring* tag, SYSTEMTIME* local_time);
  static bool LoadLastModelMetadata(std::wstring* sha256,
                                    unsigned long long* size);
  static bool LoadCachedResult(Result* result);
  static unsigned int LoadAvailableCount();
  static void StoreAvailableCount(unsigned int count);
  static bool IsAutomaticCheckDue(Frequency frequency);
  static Result CheckNow();
  static bool ParseReleaseAddress(const std::wstring& address,
                                  std::wstring* tag);
  static bool IsNewerVersion(const std::wstring& candidate,
                             const std::wstring& baseline);
  static std::wstring LoadInstalledSchemeVersion();
  static bool ParseCnbSchemeReleases(const std::string& json,
                                     SchemeRelease* release);
  static bool ParseGithubSchemeReleases(const std::string& json,
                                        SchemeRelease* release);
  static bool QueryGithubSchemeRelease(const std::wstring& tag,
                                       SchemeRelease* release,
                                       std::wstring* error);

 private:
  struct ModelRelease {
    std::wstring sha256;
    unsigned long long size = 0;
  };

  static bool QueryLatestScheme(SchemeRelease* release, std::wstring* error);
  static bool QueryLatestModel(ModelRelease* release, std::wstring* error);
  static bool ParseModelRelease(const std::string& page, ModelRelease* release);
  static void SaveLastAttempt();
  static void SaveLastRelease(const std::wstring& tag);
  static void SaveLastModel(const ModelRelease& model);
};
