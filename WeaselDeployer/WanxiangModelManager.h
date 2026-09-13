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
    WaitingRetry,
    Paused,
    RestartRequired,
    Transferred,
    Error,
  };

  struct Progress {
    State state = State::NotInstalled;
    unsigned long long transferred = 0;
    unsigned long long total = 0;
    HRESULT error_code = S_OK;
    BG_ERROR_CONTEXT error_context = BG_ERROR_CONTEXT_NONE;
    std::wstring error;
  };

  WanxiangModelManager();

  Progress GetProgress();
  bool Start(std::wstring* error);
  bool Pause(std::wstring* error);
  void Cancel();
  bool CompleteAndInstall(std::wstring* error);
  bool RemoveInstalled(std::wstring* error);
  bool Rollback(std::wstring* error);
  bool Commit(std::wstring* error);

  static constexpr unsigned long long kExpectedSize = 420343852;

 private:
  bool EnsureManager(std::wstring* error);
  bool AttachExistingJob();
  bool JobCacheMissing() const;
  std::filesystem::path CachePath() const;
  void RecoverInterruptedTransaction();
  State InstalledState() const;
  std::filesystem::path StagingPath() const;
  std::filesystem::path ModelPath() const;
  std::filesystem::path BackupPath() const;
  std::filesystem::path CommitMarkerPath() const;
  std::wstring GetJobError(HRESULT* error_code,
                           BG_ERROR_CONTEXT* error_context) const;
  bool VerifyStagedFile(std::wstring* error) const;

  CComPtr<IBackgroundCopyManager> manager_;
  CComPtr<IBackgroundCopyJob> job_;
  std::filesystem::path job_path_;
  bool ready_to_install_ = false;
  bool installed_this_session_ = false;
  bool backed_up_existing_ = false;
};
