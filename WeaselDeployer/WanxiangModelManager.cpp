#include "stdafx.h"
#include "WanxiangModelManager.h"

#include <bcrypt.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <WeaselIPC.h>
#include <WeaselUtility.h>

namespace {
constexpr wchar_t kJobName[] = L"Weasel Wanxiang LTS Model";
constexpr wchar_t kModelFileName[] = L"wanxiang-lts-zh-hans.gram";
constexpr wchar_t kModelUrl[] =
    L"https://cnb.cool/amzxyz/rime-wanxiang/-/releases/download/model/"
    L"wanxiang-lts-zh-hans.gram";
constexpr char kModelSha256[] =
    "9f80530f470033cfb6d4b44bb861b540f64100426f92dd0f87140883632a3d93";
constexpr wchar_t kCompletionArgument[] = L"/model-download-complete";

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
    std::array<char, 1024 * 1024> buffer;
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
}  // namespace

WanxiangModelManager::WanxiangModelManager() {
  RecoverInterruptedTransaction();
  std::wstring error;
  if (EnsureManager(&error))
    AttachExistingJob();
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
          if (state == BG_JOB_STATE_SUSPENDED && FAILED(candidate->Resume()))
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
  return WeaselUserDataPath() / L".weasel-packages" /
         L"wanxiang-lts-zh-hans.gram.part";
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
  return size == kExpectedSize ? State::Installed : State::Modified;
}

std::wstring WanxiangModelManager::GetJobError() const {
  if (!job_)
    return L"Background download is unavailable.";
  CComPtr<IBackgroundCopyError> error;
  if (FAILED(job_->GetError(&error)) || !error)
    return L"Background download failed.";
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
    result.state = InstalledState();
    result.total = kExpectedSize;
    return result;
  }

  BG_JOB_PROGRESS progress{};
  if (SUCCEEDED(job_->GetProgress(&progress))) {
    result.transferred = progress.BytesTransferred;
    result.total = progress.BytesTotal == BG_SIZE_UNKNOWN ? kExpectedSize
                                                          : progress.BytesTotal;
  }
  BG_JOB_STATE state;
  if (FAILED(job_->GetState(&state))) {
    result.state = State::Error;
    result.error = L"Unable to read background download state.";
  } else if (state == BG_JOB_STATE_TRANSFERRED) {
    result.state = State::Transferred;
  } else if (state == BG_JOB_STATE_ERROR ||
             state == BG_JOB_STATE_TRANSIENT_ERROR) {
    result.state = State::Error;
    result.error = GetJobError();
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
  if (job_)
    return true;
  if (!EnsureManager(error))
    return false;

  std::error_code file_error;
  std::filesystem::create_directories(StagingPath().parent_path(), file_error);
  if (file_error) {
    if (error)
      *error = file_error.message().empty()
                   ? L"Unable to create the model download directory."
                   : u8tow(file_error.message());
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
  HRESULT result = job_->SetPriority(BG_JOB_PRIORITY_LOW);
  if (SUCCEEDED(result))
    result = job_->AddFile(kModelUrl, StagingPath().c_str());
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
    result =
        job_->SetNotifyFlags(BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR);
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

void WanxiangModelManager::Cancel() {
  if (job_)
    job_->Cancel();
  job_.Release();
  std::error_code error;
  std::filesystem::remove(StagingPath(), error);
}

bool WanxiangModelManager::VerifyStagedFile(std::wstring* error) const {
  std::error_code file_error;
  const auto size = std::filesystem::file_size(StagingPath(), file_error);
  if (file_error || size != kExpectedSize) {
    if (error)
      *error = L"Downloaded model size does not match the verified package.";
    return false;
  }
  std::string digest;
  if (!Sha256File(StagingPath(), &digest)) {
    if (error)
      *error = L"Unable to calculate the downloaded model checksum.";
    return false;
  }
  if (digest != kModelSha256) {
    if (error)
      *error =
          L"Downloaded model checksum does not match the verified CNB file.";
    return false;
  }
  return true;
}

bool WanxiangModelManager::CompleteAndInstall(std::wstring* error) {
  if (!job_) {
    if (error)
      *error = L"No completed model download was found.";
    return false;
  }
  std::error_code file_error;
  const bool has_marker =
      std::filesystem::exists(CommitMarkerPath(), file_error);
  if (file_error || has_marker) {
    job_->Cancel();
    job_.Release();
    if (error)
      *error = file_error
                   ? u8tow(file_error.message())
                   : std::wstring(
                         L"An earlier model transaction still needs cleanup.");
    return false;
  }
  const bool has_backup = std::filesystem::exists(BackupPath(), file_error);
  if (file_error) {
    job_->Cancel();
    job_.Release();
    if (error)
      *error = u8tow(file_error.message());
    return false;
  }
  if (has_backup) {
    job_->Cancel();
    job_.Release();
    if (error)
      *error = L"An earlier model backup still needs recovery.";
    return false;
  }

  const HRESULT completed = job_->Complete();
  job_.Release();
  if (FAILED(completed)) {
    std::error_code ignored;
    std::filesystem::remove(StagingPath(), ignored);
    if (error)
      *error = HresultMessage(completed);
    return false;
  }
  if (!VerifyStagedFile(error)) {
    std::error_code ignored;
    std::filesystem::remove(StagingPath(), ignored);
    return false;
  }

  MaintenanceScope maintenance;
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
  std::filesystem::rename(StagingPath(), ModelPath(), file_error);
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
                         L"An earlier model transaction still needs cleanup.");
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
      *error = L"An earlier model backup still needs recovery.";
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
          *error = L"Unable to create the model transaction marker.";
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
  installed_this_session_ = false;
  backed_up_existing_ = false;
  return true;
}
