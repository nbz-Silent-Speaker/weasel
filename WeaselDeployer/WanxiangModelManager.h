#pragma once

#include <atlbase.h>
#include <bits.h>
#include <filesystem>
#include <string>

class WanxiangModelManager {
 public:
  enum class State {
    NotInstalled,
    Installed,
    Modified,
    Downloading,
    Transferred,
    Error,
  };

  struct Progress {
    State state = State::NotInstalled;
    unsigned long long transferred = 0;
    unsigned long long total = 0;
    std::wstring error;
  };

  WanxiangModelManager();

  Progress GetProgress();
  bool Start(std::wstring* error);
  void Cancel();
  bool CompleteAndInstall(std::wstring* error);
  bool RemoveInstalled(std::wstring* error);
  bool Rollback(std::wstring* error);
  bool Commit(std::wstring* error);

  static constexpr unsigned long long kExpectedSize = 420343852;

 private:
  bool EnsureManager(std::wstring* error);
  bool AttachExistingJob();
  void RecoverInterruptedTransaction();
  State InstalledState() const;
  std::filesystem::path StagingPath() const;
  std::filesystem::path ModelPath() const;
  std::filesystem::path BackupPath() const;
  std::filesystem::path CommitMarkerPath() const;
  std::wstring GetJobError() const;
  bool VerifyStagedFile(std::wstring* error) const;

  CComPtr<IBackgroundCopyManager> manager_;
  CComPtr<IBackgroundCopyJob> job_;
  bool installed_this_session_ = false;
  bool backed_up_existing_ = false;
};
