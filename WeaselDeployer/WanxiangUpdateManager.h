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
    std::wstring latest_model_sha256;
    unsigned long long latest_model_size = 0;
    std::wstring error;
  };

  static constexpr wchar_t kInstalledVersion[] = L"17.10.0";

  static Frequency LoadFrequency(const std::string& schema_id);
  static bool SaveFrequency(const std::string& schema_id, Frequency frequency);
  static bool LoadLastCheck(std::wstring* tag, SYSTEMTIME* local_time);
  static bool LoadLastModelMetadata(std::wstring* sha256,
                                    unsigned long long* size);
  static bool IsAutomaticCheckDue(Frequency frequency);
  static Result CheckNow();

 private:
  struct ModelRelease {
    std::wstring sha256;
    unsigned long long size = 0;
  };

  static std::wstring QueryLatestRelease(std::wstring* error);
  static bool QueryLatestModel(ModelRelease* release, std::wstring* error);
  static bool ParseModelRelease(const std::string& page, ModelRelease* release);
  static void SaveLastAttempt();
  static void SaveLastCheck(const std::wstring& tag, const ModelRelease& model);
};
