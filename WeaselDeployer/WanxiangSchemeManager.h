#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "WanxiangUpdateManager.h"

class WanxiangSchemeManager {
 public:
  enum class Phase {
    Preparing,
    Downloading,
    Verifying,
    Extracting,
    Installing
  };
  using ProgressCallback =
      std::function<void(Phase, unsigned long long, unsigned long long)>;

  explicit WanxiangSchemeManager(std::filesystem::path user_data = {});
  void SetProgressCallback(ProgressCallback callback);

  bool PrepareAndInstall(const WanxiangUpdateManager::SchemeRelease& release,
                         std::wstring* error);
  bool Commit(std::wstring* error);
  bool Rollback(std::wstring* error);
  bool RecoverInterruptedTransaction(std::wstring* error);

 private:
  struct ManifestEntry {
    bool existed = false;
    std::filesystem::path relative;
  };

  bool Download(const WanxiangUpdateManager::SchemeRelease& release,
                std::wstring* error);
  bool VerifyArchive(const WanxiangUpdateManager::SchemeRelease& release,
                     std::wstring* error) const;
  bool Extract(std::wstring* error);
  bool ValidateStaging(const WanxiangUpdateManager::SchemeRelease& release,
                       std::filesystem::path* source_root,
                       std::wstring* error) const;
  bool InstallStaged(const std::filesystem::path& source_root,
                     std::wstring* error);
  bool LoadManifest(std::vector<ManifestEntry>* entries,
                    std::wstring* error) const;
  bool SaveInstalledVersion(std::wstring* error) const;
  bool IsPreserved(const std::filesystem::path& relative) const;
  void Report(Phase phase,
              unsigned long long completed = 0,
              unsigned long long total = 0) const;
  std::filesystem::path PackageRoot() const;
  std::filesystem::path ArchivePath() const;
  std::filesystem::path StagingPath() const;
  std::filesystem::path TransactionPath() const;
  std::filesystem::path BackupPath() const;
  std::filesystem::path ManifestPath() const;
  std::filesystem::path ActiveMarkerPath() const;

  std::filesystem::path user_data_;
  std::wstring installed_tag_;
  bool installed_ = false;
  ProgressCallback progress_callback_;
};
