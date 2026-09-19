#include "stdafx.h"
#include "WanxiangModelManager.h"

#include <bcrypt.h>
#include <bits3_0.h>
#include <ShlObj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <WeaselIPC.h>
#include <WeaselUtility.h>

namespace {
constexpr wchar_t kJobName[] = L"Weasel Wanxiang LTS Grammar Model";
constexpr wchar_t kModelFileName[] = L"wanxiang-lts-zh-hans.gram";
constexpr wchar_t kCompletionArgument[] = L"/model-download-complete";
constexpr wchar_t kUpdateRegistry[] = L"Software\\Rime\\Weasel\\PackageUpdates";

class MaintenanceScope {
 public:
  MaintenanceScope() {
    if (client_.Connect()) {
      client_.StartMaintenance();
      active_ = true;
    }
  }

  ~MaintenanceScope() {
    if (active_ && client_.Connect())
      client_.EndMaintenance();
  }

 private:
  weasel::Client client_;
  bool active_ = false;
};

std::wstring HresultMessage(HRESULT result) {
  wchar_t* message = nullptr;
  const DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, result, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  std::wstring text = length && message ? message : L"Unknown error";
  if (message)
    ::LocalFree(message);
  while (!text.empty() &&
         (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
    text.pop_back();
  return text;
}

bool Sha256File(const std::filesystem::path& path, std::string* digest) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD hash_size = 0;
  DWORD received = 0;
  std::vector<unsigned char> object;
  std::vector<unsigned char> bytes;
  bool success = false;

  if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
    goto done;
  if (!BCRYPT_SUCCESS(
          ::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &received, 0)))
    goto done;
  if (!BCRYPT_SUCCESS(::BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                          reinterpret_cast<PUCHAR>(&hash_size),
                                          sizeof(hash_size), &received, 0)))
    goto done;

  object.resize(object_size);
  bytes.resize(hash_size);
  if (!BCRYPT_SUCCESS(::BCryptCreateHash(algorithm, &hash, object.data(),
                                         object_size, nullptr, 0, 0)))
    goto done;

  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      goto done;
    // Keep the streaming buffer off the default Windows thread stack.
    std::vector<char> buffer(1024 * 1024);
    while (input) {
      input.read(buffer.data(), buffer.size());
      const auto count = input.gcount();
      if (count > 0 && !BCRYPT_SUCCESS(::BCryptHashData(
                           hash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(count), 0)))
        goto done;
    }
    if (!input.eof())
      goto done;
  }

  if (!BCRYPT_SUCCESS(::BCryptFinishHash(hash, bytes.data(), hash_size, 0)))
    goto done;
  {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
      output << std::setw(2) << static_cast<unsigned int>(byte);
    *digest = output.str();
  }
  success = true;

done:
  if (hash)
    ::BCryptDestroyHash(hash);
  if (algorithm)
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
  return success;
}

bool IsSha256(const std::string& value) {
  if (value.size() != 64)
    return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isxdigit(character) != 0;
  });
}

std::string NarrowAscii(const std::wstring& value) {
  return std::string(value.begin(), value.end());
}

bool LoadLatestTarget(std::string* sha256, unsigned long long* size) {
  wchar_t digest[65] = {};
  DWORD digest_size = sizeof(digest);
  ULONGLONG model_size = 0;
  DWORD model_size_size = sizeof(model_size);
  if (::RegGetValueW(HKEY_CURRENT_USER, kUpdateRegistry, L"LastModelSha256",
                     RRF_RT_REG_SZ, nullptr, digest,
                     &digest_size) != ERROR_SUCCESS ||
      ::RegGetValueW(HKEY_CURRENT_USER, kUpdateRegistry, L"LastModelSize",
                     RRF_RT_REG_QWORD, nullptr, &model_size,
                     &model_size_size) != ERROR_SUCCESS) {
    return false;
  }
  std::string converted = NarrowAscii(digest);
  if (!IsSha256(converted) || !model_size)
    return false;
  std::transform(converted.begin(), converted.end(), converted.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  *sha256 = std::move(converted);
  *size = model_size;
  return true;
}

void SaveInstalledTarget(const std::string& sha256, unsigned long long size) {
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kUpdateRegistry, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
    return;
  }
  const auto digest = u8tow(sha256);
  const ULONGLONG model_size = size;
  FILETIME installed = {};
  ::GetSystemTimeAsFileTime(&installed);
  ULARGE_INTEGER installed_value = {};
  installed_value.LowPart = installed.dwLowDateTime;
  installed_value.HighPart = installed.dwHighDateTime;
  ::RegSetValueExW(key, L"InstalledModelSha256", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(digest.c_str()),
                   static_cast<DWORD>((digest.size() + 1) * sizeof(wchar_t)));
  ::RegSetValueExW(key, L"InstalledModelSize", 0, REG_QWORD,
                   reinterpret_cast<const BYTE*>(&model_size),
                   sizeof(model_size));
  ::RegSetValueExW(key, L"LastModelInstalled", 0, REG_QWORD,
                   reinterpret_cast<const BYTE*>(&installed_value.QuadPart),
                   sizeof(installed_value.QuadPart));
  ::RegCloseKey(key);
}

void ClearInstalledTarget() {
  ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kUpdateRegistry,
                       L"InstalledModelSha256");
  ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kUpdateRegistry,
                       L"InstalledModelSize");
  ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kUpdateRegistry,
                       L"LastModelInstalled");
}

bool IsManagedInstalledModel(const std::filesystem::path& path) {
  std::error_code error;
  const auto actual_size = std::filesystem::file_size(path, error);
  if (error)
    return false;
  wchar_t digest[65] = {};
  DWORD digest_size = sizeof(digest);
  ULONGLONG recorded_size = 0;
  DWORD recorded_size_size = sizeof(recorded_size);
  if (::RegGetValueW(HKEY_CURRENT_USER, kUpdateRegistry,
                     L"InstalledModelSha256", RRF_RT_REG_SZ, nullptr, digest,
                     &digest_size) == ERROR_SUCCESS &&
      ::RegGetValueW(HKEY_CURRENT_USER, kUpdateRegistry, L"InstalledModelSize",
                     RRF_RT_REG_QWORD, nullptr, &recorded_size,
                     &recorded_size_size) == ERROR_SUCCESS &&
      recorded_size == actual_size) {
    std::string actual_digest;
    if (Sha256File(path, &actual_digest) &&
        actual_digest == NarrowAscii(digest)) {
      return true;
    }
  }
  return actual_size == WanxiangModelManager::kExpectedSize;
}

void CleanCommittedDownload(const std::filesystem::path& path,
                            const std::string& digest,
                            bool record_commit) {
  if (path.empty())
    return;
  auto receipt = path;
  receipt += L".cleanup";
  std::error_code error;
  if (record_commit) {
    if (!std::filesystem::exists(path, error) || error)
      return;
    std::ofstream marker(receipt, std::ios::binary | std::ios::trunc);
    marker << digest;
    marker.flush();
    if (!marker)
      LOG(WARNING) << "Unable to record pending model cache cleanup.";
  } else {
    std::ifstream marker(receipt, std::ios::binary);
    std::string marker_digest;
    marker >> marker_digest;
    if (marker_digest != digest)
      return;
  }
  // Called only after deployment has committed, or from its cleanup receipt.
  // Never recurse or remove another model/version's cache.
  std::filesystem::remove(path, error);
  if (error) {
    LOG(WARNING) << "Model deployed; cache cleanup will retry: "
                 << error.message();
    return;
  }
  std::filesystem::remove(receipt, error);
}
}  // namespace

WanxiangModelManager::WanxiangModelManager() {
  LoadLatestTarget(&target_sha256_, &target_size_);
  RecoverInterruptedTransaction();
  std::wstring error;
  if (EnsureManager(&error)) {
    AttachExistingJob();
    // A verified installed model takes precedence over an abandoned download
    // from an earlier installation. The exact BITS job is no longer useful.
    if (job_ && InstalledState() == State::Installed) {
      job_->Cancel();
      job_.Release();
      job_path_.clear();
    }
  }
  if (!job_) {
    CleanCommittedDownload(CachePath(), target_sha256_, false);
    CleanCommittedDownload(WeaselUserDataPath() / L".weasel-packages" /
                               L"wanxiang-lts-zh-hans.gram.part",
                           target_sha256_, false);
    std::error_code cache_error;
    ready_to_install_ =
        std::filesystem::file_size(CachePath(), cache_error) == target_size_ &&
        !cache_error;
  }
}

bool WanxiangModelManager::ConfigureTarget(const std::wstring& sha256,
                                           unsigned long long size) {
  std::string digest = NarrowAscii(sha256);
  std::transform(digest.begin(), digest.end(), digest.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (!IsSha256(digest) || !size)
    return false;
  const bool changed = digest != target_sha256_ || size != target_size_;
  if (changed && job_) {
    job_->Cancel();
    job_.Release();
    job_path_.clear();
  }
  if (changed)
    ready_to_install_ = false;
  target_sha256_ = std::move(digest);
  target_size_ = size;
  return true;
}

void WanxiangModelManager::RecoverInterruptedTransaction() {
  std::error_code file_error;
  const bool committed =
      std::filesystem::exists(CommitMarkerPath(), file_error);
  if (file_error) {
    LOG(ERROR) << "Unable to inspect the Wanxiang model transaction marker: "
               << file_error.message();
    return;
  }
  if (committed) {
    std::filesystem::remove(BackupPath(), file_error);
    if (file_error) {
      LOG(ERROR) << "Unable to clean a committed Wanxiang model backup: "
                 << file_error.message();
      return;
    }
    std::filesystem::remove(CommitMarkerPath(), file_error);
    if (file_error) {
      LOG(ERROR) << "Unable to clean the Wanxiang model transaction marker: "
                 << file_error.message();
    }
    return;
  }

  const bool has_backup = std::filesystem::exists(BackupPath(), file_error);
  if (file_error) {
    LOG(ERROR) << "Unable to inspect the Wanxiang model backup: "
               << file_error.message();
    return;
  }
  if (!has_backup)
    return;

  MaintenanceScope maintenance;
  const bool has_model = std::filesystem::exists(ModelPath(), file_error);
  if (file_error) {
    LOG(ERROR) << "Unable to inspect the installed Wanxiang model: "
               << file_error.message();
    return;
  }
  if (has_model) {
    std::filesystem::remove(ModelPath(), file_error);
    if (file_error) {
      LOG(ERROR) << "Unable to remove an interrupted Wanxiang model install: "
                 << file_error.message();
      return;
    }
  }
  file_error.clear();
  std::filesystem::rename(BackupPath(), ModelPath(), file_error);
  if (file_error) {
    LOG(ERROR) << "Unable to restore an interrupted Wanxiang model backup: "
               << file_error.message();
  } else {
    LOG(WARNING) << "Restored Wanxiang model after an interrupted transaction.";
  }
}

bool WanxiangModelManager::EnsureManager(std::wstring* error) {
  if (manager_)
    return true;
  const HRESULT result =
      manager_.CoCreateInstance(__uuidof(BackgroundCopyManager));
  if (FAILED(result)) {
    if (error)
      *error = HresultMessage(result);
    return false;
  }
  return true;
}

bool WanxiangModelManager::AttachExistingJob() {
  CComPtr<IEnumBackgroundCopyJobs> jobs;
  if (FAILED(manager_->EnumJobs(0, &jobs)))
    return false;
  while (true) {
    CComPtr<IBackgroundCopyJob> candidate;
    ULONG fetched = 0;
    if (jobs->Next(1, &candidate, &fetched) != S_OK || fetched != 1)
      break;
    LPWSTR display_name = nullptr;
    if (SUCCEEDED(candidate->GetDisplayName(&display_name))) {
      const bool matches = display_name && !wcscmp(display_name, kJobName);
      ::CoTaskMemFree(display_name);
      if (matches) {
        BG_JOB_STATE state;
        if (SUCCEEDED(candidate->GetState(&state)) &&
            state != BG_JOB_STATE_CANCELLED &&
            state != BG_JOB_STATE_ACKNOWLEDGED) {
          CComPtr<IEnumBackgroundCopyFiles> files;
          CComPtr<IBackgroundCopyFile> file;
          ULONG count = 0;
          if (FAILED(candidate->EnumFiles(&files)) ||
              FAILED(files->GetCount(&count)) || count != 1 ||
              files->Next(1, &file, &count) != S_OK || count != 1)
            continue;
          LPWSTR remote = nullptr;
          LPWSTR local = nullptr;
          const HRESULT remote_result = file->GetRemoteName(&remote);
          const HRESULT local_result = file->GetLocalName(&local);
          const bool valid =
              SUCCEEDED(remote_result) && SUCCEEDED(local_result) && remote &&
              local && !wcscmp(remote, WanxiangModelManager::kDownloadUrl) &&
              std::filesystem::path(local).filename() ==
                  L"wanxiang-lts-zh-hans.gram.part";
          if (valid)
            job_path_ = local;
          ::CoTaskMemFree(remote);
          ::CoTaskMemFree(local);
          if (!valid)
            continue;
          job_ = candidate;
          return true;
        }
      }
    }
  }
  return false;
}

std::filesystem::path WanxiangModelManager::StagingPath() const {
  return job_path_.empty() ? CachePath() : job_path_;
}

std::filesystem::path WanxiangModelManager::CachePath() const {
  PWSTR directory = nullptr;
  if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                    nullptr, &directory)))
    return {};
  const std::filesystem::path root(directory);
  ::CoTaskMemFree(directory);
  return root / L"Rime" / L"Weasel" / L"Downloads" / u8tow(target_sha256_) /
         L"wanxiang-lts-zh-hans.gram.part";
}

bool WanxiangModelManager::JobCacheMissing() const {
  if (!job_)
    return false;
  // An unavailable drive may still contain resumable data. Do not cancel its
  // BITS task or report lost data just because the volume is currently offline.
  if (::GetFileAttributesW(StagingPath().root_path().c_str()) ==
      INVALID_FILE_ATTRIBUTES)
    return false;
  const DWORD directory =
      ::GetFileAttributesW(StagingPath().parent_path().c_str());
  const DWORD directory_error = ::GetLastError();
  const bool directory_missing = directory == INVALID_FILE_ATTRIBUTES &&
                                 (directory_error == ERROR_FILE_NOT_FOUND ||
                                  directory_error == ERROR_PATH_NOT_FOUND);
  // BITS owns a temporary file until Complete(). The destination .part file
  // normally does not exist yet, so its absence is not evidence of lost data.
  CComPtr<IEnumBackgroundCopyFiles> files;
  CComPtr<IBackgroundCopyFile> file;
  ULONG fetched = 0;
  if (FAILED(job_->EnumFiles(&files)) ||
      files->Next(1, &file, &fetched) != S_OK || fetched != 1)
    return directory_missing;
  CComQIPtr<IBackgroundCopyFile3> file3(file);
  LPWSTR temporary = nullptr;
  if (!file3 || FAILED(file3->GetTemporaryName(&temporary)))
    return directory_missing;
  const DWORD attributes =
      temporary ? ::GetFileAttributesW(temporary) : INVALID_FILE_ATTRIBUTES;
  const DWORD error = ::GetLastError();
  ::CoTaskMemFree(temporary);
  return attributes == INVALID_FILE_ATTRIBUTES &&
         (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}

std::filesystem::path WanxiangModelManager::ModelPath() const {
  return WeaselUserDataPath() / kModelFileName;
}

std::filesystem::path WanxiangModelManager::BackupPath() const {
  return WeaselUserDataPath() / L".weasel-packages" /
         L"wanxiang-lts-zh-hans.gram.backup";
}

std::filesystem::path WanxiangModelManager::CommitMarkerPath() const {
  return WeaselUserDataPath() / L".weasel-packages" /
         L"wanxiang-lts-zh-hans.gram.committed";
}

WanxiangModelManager::State WanxiangModelManager::InstalledState() const {
  std::error_code error;
  const auto size = std::filesystem::file_size(ModelPath(), error);
  if (error)
    return State::NotInstalled;
  return size == target_size_ ? State::Installed : State::Modified;
}

std::wstring WanxiangModelManager::GetJobError(
    HRESULT* error_code,
    BG_ERROR_CONTEXT* error_context) const {
  if (error_code)
    *error_code = E_FAIL;
  if (!job_)
    return L"Background download is unavailable.";
  CComPtr<IBackgroundCopyError> error;
  if (FAILED(job_->GetError(&error)) || !error)
    return L"Background download failed.";
  BG_ERROR_CONTEXT context = BG_ERROR_CONTEXT_NONE;
  HRESULT code = E_FAIL;
  if (SUCCEEDED(error->GetError(&context, &code))) {
    if (error_code)
      *error_code = code;
    if (error_context)
      *error_context = context;
  }
  LPWSTR description = nullptr;
  if (FAILED(error->GetErrorDescription(GetThreadUILanguage(), &description)) ||
      !description)
    return L"Background download failed.";
  std::wstring text(description);
  ::CoTaskMemFree(description);
  return text;
}

WanxiangModelManager::Progress WanxiangModelManager::GetProgress() {
  Progress result;
  if (!job_) {
    result.state = ready_to_install_ ? State::Transferred : InstalledState();
    result.total = target_size_;
    if (ready_to_install_)
      result.transferred = target_size_;
    return result;
  }

  BG_JOB_PROGRESS progress{};
  if (SUCCEEDED(job_->GetProgress(&progress))) {
    result.transferred = progress.BytesTransferred;
    result.total = progress.BytesTotal == BG_SIZE_UNKNOWN ? target_size_
                                                          : progress.BytesTotal;
  }
  BG_JOB_STATE state;
  const HRESULT state_result = job_->GetState(&state);
  if (FAILED(state_result)) {
    result.state = State::Error;
    result.error_code = state_result;
    result.error = L"Unable to read background download state.";
  } else if (state == BG_JOB_STATE_TRANSFERRED) {
    result.state =
        JobCacheMissing() ? State::RestartRequired : State::Transferred;
  } else if (state == BG_JOB_STATE_TRANSIENT_ERROR) {
    result.state = State::WaitingRetry;
    result.error = GetJobError(&result.error_code, &result.error_context);
    if (result.error_context == BG_ERROR_CONTEXT_LOCAL_FILE)
      result.state = JobCacheMissing() ? State::RestartRequired : State::Error;
  } else if (state == BG_JOB_STATE_ERROR || state == BG_JOB_STATE_SUSPENDED) {
    result.state = State::Paused;
    if (state == BG_JOB_STATE_ERROR)
      result.error = GetJobError(&result.error_code, &result.error_context);
    if (state == BG_JOB_STATE_ERROR)
      result.state = result.error_context == BG_ERROR_CONTEXT_LOCAL_FILE &&
                             JobCacheMissing()
                         ? State::RestartRequired
                         : State::Error;
    else if (result.transferred > 0 && JobCacheMissing())
      result.state = State::RestartRequired;
  } else if (state == BG_JOB_STATE_CANCELLED ||
             state == BG_JOB_STATE_ACKNOWLEDGED) {
    job_.Release();
    result.state = InstalledState();
  } else {
    result.state = State::Downloading;
  }
  return result;
}

bool WanxiangModelManager::Start(std::wstring* error) {
  if (job_) {
    const auto progress = GetProgress();
    if (progress.state == State::RestartRequired) {
      // The UI explicitly offers restarting when BITS' real cache is gone.
      const HRESULT cancelled = job_->Cancel();
      if (FAILED(cancelled)) {
        if (error)
          *error = HresultMessage(cancelled);
        return false;
      }
      job_.Release();
      job_path_.clear();
    }
  }
  if (job_) {
    BG_JOB_STATE state = BG_JOB_STATE_ERROR;
    HRESULT result = job_->GetState(&state);
    if (SUCCEEDED(result) && (state == BG_JOB_STATE_CANCELLED ||
                              state == BG_JOB_STATE_ACKNOWLEDGED)) {
      job_.Release();
    } else if (SUCCEEDED(result) && state == BG_JOB_STATE_TRANSFERRED) {
      return true;
    } else if (SUCCEEDED(result)) {
      std::error_code directory_error;
      std::filesystem::create_directories(StagingPath().parent_path(),
                                          directory_error);
      if (directory_error) {
        if (error)
          *error = L"无法访问下载缓存目录，请检查目录权限和磁盘状态。";
        return false;
      }
      job_->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
      job_->SetNotifyFlags(BG_NOTIFY_JOB_TRANSFERRED);
      if (state == BG_JOB_STATE_ERROR ||
          state == BG_JOB_STATE_TRANSIENT_ERROR ||
          state == BG_JOB_STATE_SUSPENDED) {
        result = job_->Resume();
      }
      if (FAILED(result)) {
        if (error)
          *error = HresultMessage(result);
        return false;
      }
      return true;
    } else {
      if (error)
        *error = HresultMessage(result);
      return false;
    }
  }
  if (!EnsureManager(error))
    return false;

  // A verified download is reusable after installation/deployment failed.
  if (!StagingPath().empty() && VerifyStagedFile(nullptr)) {
    ready_to_install_ = true;
    return true;
  }
  job_path_.clear();
  if (StagingPath().empty()) {
    if (error)
      *error = L"无法获取当前用户的本地缓存目录。";
    return false;
  }
  std::error_code file_error;
  std::filesystem::create_directories(StagingPath().parent_path(), file_error);
  if (file_error) {
    if (error)
      *error = L"无法创建下载缓存目录，请检查目录权限和磁盘状态。";
    return false;
  }
  std::filesystem::remove(StagingPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }

  GUID job_id{};
  const HRESULT created =
      manager_->CreateJob(kJobName, BG_JOB_TYPE_DOWNLOAD, &job_id, &job_);
  if (FAILED(created)) {
    if (error)
      *error = HresultMessage(created);
    return false;
  }
  HRESULT result = job_->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
  if (SUCCEEDED(result))
    result = job_->SetMinimumRetryDelay(60);
  if (SUCCEEDED(result))
    result = job_->SetNoProgressTimeout(7 * 24 * 60 * 60);
  if (SUCCEEDED(result))
    result = job_->AddFile(kDownloadUrl, StagingPath().c_str());
  wchar_t executable[MAX_PATH] = {};
  if (SUCCEEDED(result)) {
    const DWORD length =
        ::GetModuleFileNameW(nullptr, executable, _countof(executable));
    if (!length || length >= _countof(executable))
      result = HRESULT_FROM_WIN32(length ? ERROR_INSUFFICIENT_BUFFER
                                         : ::GetLastError());
  }
  CComQIPtr<IBackgroundCopyJob2> job2(job_);
  if (SUCCEEDED(result) && !job2)
    result = E_NOINTERFACE;
  if (SUCCEEDED(result))
    result = job2->SetNotifyCmdLine(executable, kCompletionArgument);
  if (SUCCEEDED(result))
    result = job_->SetNotifyFlags(BG_NOTIFY_JOB_TRANSFERRED);
  if (SUCCEEDED(result))
    result = job_->Resume();
  if (FAILED(result)) {
    job_->Cancel();
    job_.Release();
    if (error)
      *error = HresultMessage(result);
    return false;
  }
  return true;
}

bool WanxiangModelManager::Pause(std::wstring* error) {
  const HRESULT result = job_ ? job_->Suspend() : E_UNEXPECTED;
  if (FAILED(result) && error)
    *error = HresultMessage(result);
  return SUCCEEDED(result);
}

void WanxiangModelManager::Cancel() {
  if (job_)
    job_->Cancel();
  job_.Release();
  std::error_code error;
  std::filesystem::remove(StagingPath(), error);
  ready_to_install_ = false;
  job_path_.clear();
}

bool WanxiangModelManager::VerifyStagedFile(std::wstring* error) const {
  std::error_code file_error;
  const auto size = std::filesystem::file_size(StagingPath(), file_error);
  if (file_error || size != target_size_) {
    if (error)
      *error =
          L"Downloaded grammar model size does not match the verified package.";
    return false;
  }
  std::string digest;
  if (!Sha256File(StagingPath(), &digest)) {
    if (error)
      *error = L"Unable to calculate the downloaded grammar model checksum.";
    return false;
  }
  if (digest != target_sha256_) {
    if (error)
      *error =
          L"Downloaded grammar model checksum does not match the verified CNB "
          L"file.";
    return false;
  }
  return true;
}

bool WanxiangModelManager::CompleteDownload(std::wstring* error) {
  if (!job_ && !VerifyStagedFile(nullptr)) {
    if (error)
      *error = L"No completed grammar model download was found.";
    return false;
  }
  // Preserve completed data even when a previous install transaction needs
  // recovery. Recreate only the destination folder if BITS still has its data.
  if (job_) {
    std::error_code directory_error;
    std::filesystem::create_directories(StagingPath().parent_path(),
                                        directory_error);
    if (directory_error) {
      job_->Suspend();
      if (error)
        *error = L"无法恢复下载缓存目录，请检查目录权限和磁盘状态。";
      return false;
    }
  }
  const HRESULT completed = job_ ? job_->Complete() : S_OK;
  if (FAILED(completed)) {
    // Keep the BITS task for an explicit retry; do not discard downloaded data.
    job_->Suspend();
    if (error)
      *error = HresultMessage(completed);
    return false;
  }
  job_.Release();
  if (!VerifyStagedFile(error)) {
    std::error_code ignored;
    std::filesystem::remove(StagingPath(), ignored);
    ready_to_install_ = false;
    return false;
  }
  ready_to_install_ = true;
  return true;
}

bool WanxiangModelManager::CompleteAndInstall(std::wstring* error) {
  if (!CompleteDownload(error))
    return false;

  std::error_code file_error;
  const bool has_marker =
      std::filesystem::exists(CommitMarkerPath(), file_error);
  if (file_error || has_marker) {
    if (job_)
      job_->Cancel();
    job_.Release();
    if (error)
      *error = file_error
                   ? u8tow(file_error.message())
                   : std::wstring(
                         L"An earlier grammar model transaction still needs "
                         L"cleanup.");
    return false;
  }
  const bool has_backup = std::filesystem::exists(BackupPath(), file_error);
  if (file_error) {
    if (job_)
      job_->Cancel();
    job_.Release();
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (has_backup) {
    if (job_)
      job_->Cancel();
    job_.Release();
    if (error)
      *error = L"An earlier grammar model backup still needs recovery.";
    return false;
  }

  // Prepare on the target volume before replacing the installed model. The
  // per-user download cache may be on a different drive from the Rime folder.
  const auto prepared =
      BackupPath().parent_path() / L"wanxiang-lts-zh-hans.gram.installing";
  std::filesystem::create_directories(prepared.parent_path(), file_error);
  if (!file_error)
    std::filesystem::copy_file(
        StagingPath(), prepared,
        std::filesystem::copy_options::overwrite_existing, file_error);
  std::string prepared_digest;
  if (file_error || !Sha256File(prepared, &prepared_digest) ||
      prepared_digest != target_sha256_) {
    std::error_code ignored;
    std::filesystem::remove(prepared, ignored);
    if (error)
      *error =
          L"无法将语法模型写入用户文件夹，请检查权限和剩余空间。下载文件已保留"
          L"。";
    return false;
  }
  if (InstalledState() == State::Modified &&
      !IsManagedInstalledModel(ModelPath())) {
    if (error)
      *error =
          L"用户文件夹中出现了其他版本的语法模型，已保留原文件和下载缓存。";
    return false;
  }

  file_error.clear();
  const bool has_model = std::filesystem::exists(ModelPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (has_model) {
    std::filesystem::rename(ModelPath(), BackupPath(), file_error);
    if (file_error) {
      if (error)
        *error = u8tow(file_error.message());
      return false;
    }
    backed_up_existing_ = true;
  }
  std::filesystem::rename(prepared, ModelPath(), file_error);
  if (file_error) {
    if (backed_up_existing_) {
      std::error_code ignored;
      std::filesystem::rename(BackupPath(), ModelPath(), ignored);
      backed_up_existing_ = false;
    }
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  installed_this_session_ = true;
  removed_this_session_ = false;
  ready_to_install_ = false;
  return true;
}

bool WanxiangModelManager::RemoveInstalled(std::wstring* error) {
  std::error_code file_error;
  const bool has_model = std::filesystem::exists(ModelPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (!has_model)
    return true;
  MaintenanceScope maintenance;
  std::filesystem::create_directories(BackupPath().parent_path(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  const bool has_marker =
      std::filesystem::exists(CommitMarkerPath(), file_error);
  if (file_error || has_marker) {
    if (error)
      *error = file_error
                   ? u8tow(file_error.message())
                   : std::wstring(
                         L"An earlier grammar model transaction still needs "
                         L"cleanup.");
    return false;
  }
  const bool has_backup = std::filesystem::exists(BackupPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (has_backup) {
    if (error)
      *error = L"An earlier grammar model backup still needs recovery.";
    return false;
  }
  file_error.clear();
  std::filesystem::rename(ModelPath(), BackupPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  installed_this_session_ = true;
  backed_up_existing_ = true;
  removed_this_session_ = true;
  return true;
}

bool WanxiangModelManager::Rollback(std::wstring* error) {
  if (!installed_this_session_)
    return true;
  MaintenanceScope maintenance;
  std::error_code file_error;
  std::filesystem::remove(CommitMarkerPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  std::filesystem::remove(ModelPath(), file_error);
  if (file_error) {
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (backed_up_existing_) {
    file_error.clear();
    std::filesystem::rename(BackupPath(), ModelPath(), file_error);
    if (file_error) {
      if (error)
        *error = u8tow(file_error.message());
      return false;
    }
  }
  installed_this_session_ = false;
  backed_up_existing_ = false;
  removed_this_session_ = false;
  std::error_code cache_error;
  ready_to_install_ =
      std::filesystem::file_size(StagingPath(), cache_error) == target_size_ &&
      !cache_error;
  return true;
}

bool WanxiangModelManager::Commit(std::wstring* error) {
  if (backed_up_existing_) {
    std::error_code file_error;
    {
      std::ofstream marker(CommitMarkerPath(),
                           std::ios::binary | std::ios::trunc);
      marker << "committed\n";
      marker.flush();
      if (!marker) {
        if (error)
          *error = L"Unable to create the grammar model transaction marker.";
        return false;
      }
    }
    std::filesystem::remove(BackupPath(), file_error);
    if (file_error) {
      if (error)
        *error = u8tow(file_error.message());
      return false;
    }
    std::filesystem::remove(CommitMarkerPath(), file_error);
    if (file_error) {
      LOG(ERROR) << "Unable to clean the Wanxiang model commit marker: "
                 << file_error.message();
    }
  }
  const bool installed = installed_this_session_ && !removed_this_session_;
  installed_this_session_ = false;
  backed_up_existing_ = false;
  removed_this_session_ = false;
  if (installed)
    SaveInstalledTarget(target_sha256_, target_size_);
  else
    ClearInstalledTarget();
  CleanCommittedDownload(StagingPath(), target_sha256_, true);
  return true;
}

bool WanxiangModelManager::LoadLastInstalledTime(SYSTEMTIME* local_time) {
  if (!local_time)
    return false;
  ULONGLONG value = 0;
  DWORD size = sizeof(value);
  if (::RegGetValueW(HKEY_CURRENT_USER, kUpdateRegistry, L"LastModelInstalled",
                     RRF_RT_REG_QWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS) {
    return false;
  }
  ULARGE_INTEGER packed = {};
  packed.QuadPart = value;
  FILETIME utc = {packed.LowPart, packed.HighPart};
  FILETIME local = {};
  return ::FileTimeToLocalFileTime(&utc, &local) &&
         ::FileTimeToSystemTime(&local, local_time);
}
