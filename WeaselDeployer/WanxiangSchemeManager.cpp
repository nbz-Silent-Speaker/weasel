#include "stdafx.h"
#include "WanxiangSchemeManager.h"

#include <bcrypt.h>
#include <shlguid.h>
#include <shobjidl.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

#include <WeaselIPC.h>
#include <WeaselUtility.h>

namespace {
constexpr wchar_t kRegistryRoot[] = L"Software\\Rime\\Weasel\\PackageUpdates";
constexpr wchar_t kArchiveName[] = L"rime-wanxiang-lite.zip";
constexpr wchar_t kTransactionDirectory[] = L"scheme-transaction";

class InternetHandle {
 public:
  explicit InternetHandle(HINTERNET value = nullptr) : value_(value) {}
  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;
  ~InternetHandle() {
    if (value_)
      ::WinHttpCloseHandle(value_);
  }
  operator HINTERNET() const { return value_; }

 private:
  HINTERNET value_;
};

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

bool IsSha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
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
    std::vector<char> buffer(1024 * 1024);
    if (!input)
      goto done;
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

std::wstring SystemError(DWORD code) {
  wchar_t* message = nullptr;
  const DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  std::wstring text = length && message ? message : L"Unknown error";
  if (message)
    ::LocalFree(message);
  while (!text.empty() &&
         (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
    text.pop_back();
  return text;
}

bool ParseHttpsUrl(const std::wstring& url,
                   std::wstring* host,
                   std::wstring* path) {
  constexpr wchar_t prefix[] = L"https://";
  if (url.compare(0, std::size(prefix) - 1, prefix) != 0)
    return false;
  const auto host_start = std::size(prefix) - 1;
  const auto slash = url.find(L'/', host_start);
  if (slash == std::wstring::npos)
    return false;
  *host = url.substr(host_start, slash - host_start);
  *path = url.substr(slash);
  return !host->empty() && !path->empty();
}

bool EndsWith(const std::wstring& value, const std::wstring& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

std::wstring Lowercase(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), towlower);
  return value;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_path())
    return false;
  for (const auto& component : path) {
    if (component.empty() || component == L"." || component == L"..")
      return false;
  }
  return true;
}

std::wstring NormalizedVersion(std::wstring version) {
  if (!version.empty() && (version.front() == L'v' || version.front() == L'V'))
    version.erase(version.begin());
  while (!version.empty() && iswspace(version.back()))
    version.pop_back();
  return version;
}
}  // namespace

WanxiangSchemeManager::WanxiangSchemeManager(std::filesystem::path user_data)
    : user_data_(user_data.empty() ? WeaselUserDataPath()
                                   : std::move(user_data)) {}

void WanxiangSchemeManager::SetProgressCallback(ProgressCallback callback) {
  progress_callback_ = std::move(callback);
}

void WanxiangSchemeManager::Report(Phase phase,
                                   unsigned long long completed,
                                   unsigned long long total) const {
  if (progress_callback_)
    progress_callback_(phase, completed, total);
}

std::filesystem::path WanxiangSchemeManager::PackageRoot() const {
  return user_data_ / L".weasel-packages";
}

std::filesystem::path WanxiangSchemeManager::ArchivePath() const {
  return PackageRoot() / kArchiveName;
}

std::filesystem::path WanxiangSchemeManager::StagingPath() const {
  return PackageRoot() / L"scheme-stage";
}

std::filesystem::path WanxiangSchemeManager::TransactionPath() const {
  return PackageRoot() / kTransactionDirectory;
}

std::filesystem::path WanxiangSchemeManager::BackupPath() const {
  return TransactionPath() / L"backup";
}

std::filesystem::path WanxiangSchemeManager::ManifestPath() const {
  return TransactionPath() / L"manifest.txt";
}

std::filesystem::path WanxiangSchemeManager::ActiveMarkerPath() const {
  return TransactionPath() / L"active";
}

bool WanxiangSchemeManager::VerifyArchive(
    const WanxiangUpdateManager::SchemeRelease& release,
    std::wstring* error) const {
  std::error_code file_error;
  const auto size = std::filesystem::file_size(ArchivePath(), file_error);
  if (file_error || size != release.size) {
    if (error)
      *error = L"下载的输入方案大小与发布源记录不一致。";
    return false;
  }
  std::string digest;
  if (!Sha256File(ArchivePath(), &digest)) {
    if (error)
      *error = L"无法计算输入方案压缩包的 SHA-256。";
    return false;
  }
  const std::string expected = wtou8(release.sha256);
  if (!IsSha256(expected) || digest != expected) {
    if (error)
      *error = L"输入方案压缩包的 SHA-256 与发布源记录不一致。";
    return false;
  }
  std::ifstream archive(ArchivePath(), std::ios::binary);
  unsigned char signature[4] = {};
  archive.read(reinterpret_cast<char*>(signature), sizeof(signature));
  if (archive.gcount() != sizeof(signature) || signature[0] != 'P' ||
      signature[1] != 'K' ||
      !((signature[2] == 3 && signature[3] == 4) ||
        (signature[2] == 5 && signature[3] == 6) ||
        (signature[2] == 7 && signature[3] == 8))) {
    if (error)
      *error = L"下载内容不是有效的 ZIP 输入方案包。";
    return false;
  }
  return true;
}

bool WanxiangSchemeManager::Download(
    const WanxiangUpdateManager::SchemeRelease& release,
    std::wstring* error) {
  Report(Phase::Verifying, 0, release.size);
  if (VerifyArchive(release, nullptr))
    return true;
  Report(Phase::Downloading, 0, release.size);
  std::error_code file_error;
  std::filesystem::create_directories(PackageRoot(), file_error);
  if (file_error) {
    if (error)
      *error = L"无法创建输入方案下载缓存目录。";
    return false;
  }
  const auto partial = ArchivePath().wstring() + L".part";
  std::filesystem::remove(partial, file_error);
  std::wstring host;
  std::wstring path;
  if (!ParseHttpsUrl(release.url, &host, &path)) {
    if (error)
      *error = L"输入方案下载地址无效。";
    return false;
  }
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
  constexpr DWORD kProxyMode = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
  InternetHandle session(WinHttpOpen(L"WeaselDeployer/SchemeUpdate", kProxyMode,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0));
  InternetHandle connection(session
                                ? WinHttpConnect(session, host.c_str(),
                                                 INTERNET_DEFAULT_HTTPS_PORT, 0)
                                : nullptr);
  InternetHandle request(
      connection
          ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               WINHTTP_FLAG_SECURE)
          : nullptr);
  if (!session || !connection || !request) {
    if (error)
      *error = L"无法连接输入方案下载源。";
    return false;
  }
  WinHttpSetTimeouts(session, 10000, 10000, 10000, 120000);
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    if (error)
      *error = L"输入方案下载失败：" + SystemError(::GetLastError());
    return false;
  }
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
          WINHTTP_NO_HEADER_INDEX) ||
      status != 200) {
    if (error)
      *error = L"输入方案下载源返回了异常状态。";
    return false;
  }
  std::ofstream output(partial, std::ios::binary | std::ios::trunc);
  if (!output) {
    if (error)
      *error = L"无法写入输入方案下载缓存。";
    return false;
  }
  unsigned long long total = 0;
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      if (error)
        *error = L"读取输入方案下载数据失败。";
      return false;
    }
    if (!available)
      break;
    std::vector<char> buffer((std::min)(available, 1024ul * 1024));
    DWORD received = 0;
    if (!WinHttpReadData(request, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &received) ||
        !received) {
      if (error)
        *error = L"输入方案下载提前中断。";
      return false;
    }
    total += received;
    Report(Phase::Downloading, total, release.size);
    if (total > release.size) {
      if (error)
        *error = L"输入方案下载大小超过发布记录。";
      return false;
    }
    output.write(buffer.data(), received);
    if (!output) {
      if (error)
        *error = L"写入输入方案下载缓存失败。";
      return false;
    }
  }
  output.close();
  if (total != release.size) {
    if (error)
      *error = L"输入方案下载不完整。";
    return false;
  }
  std::filesystem::remove(ArchivePath(), file_error);
  file_error.clear();
  std::filesystem::rename(partial, ArchivePath(), file_error);
  Report(Phase::Verifying, release.size, release.size);
  if (file_error || !VerifyArchive(release, error)) {
    std::filesystem::remove(partial, file_error);
    std::filesystem::remove(ArchivePath(), file_error);
    return false;
  }
  return true;
}

bool WanxiangSchemeManager::Extract(std::wstring* error) {
  Report(Phase::Extracting);
  std::error_code file_error;
  std::filesystem::remove_all(StagingPath(), file_error);
  file_error.clear();
  std::filesystem::create_directories(StagingPath(), file_error);
  if (file_error) {
    if (error)
      *error = L"无法创建输入方案解压目录。";
    return false;
  }
  const HRESULT initialized =
      ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  CComPtr<IShellItem> archive;
  CComPtr<IEnumShellItems> contents;
  CComPtr<IShellItem> destination;
  CComPtr<IFileOperation> operation;
  HRESULT result = ::SHCreateItemFromParsingName(ArchivePath().c_str(), nullptr,
                                                 IID_PPV_ARGS(&archive));
  if (SUCCEEDED(result))
    result = archive->BindToHandler(nullptr, BHID_EnumItems,
                                    IID_PPV_ARGS(&contents));
  if (SUCCEEDED(result))
    result = ::SHCreateItemFromParsingName(StagingPath().c_str(), nullptr,
                                           IID_PPV_ARGS(&destination));
  if (SUCCEEDED(result))
    result = operation.CoCreateInstance(CLSID_FileOperation);
  if (SUCCEEDED(result))
    result = operation->SetOperationFlags(
        FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT);
  while (SUCCEEDED(result)) {
    CComPtr<IShellItem> item;
    ULONG fetched = 0;
    const HRESULT next = contents->Next(1, &item, &fetched);
    if (next != S_OK || fetched != 1)
      break;
    result = operation->CopyItem(item, destination, nullptr, nullptr);
  }
  if (SUCCEEDED(result))
    result = operation->PerformOperations();
  BOOL aborted = FALSE;
  if (SUCCEEDED(result))
    result = operation->GetAnyOperationsAborted(&aborted);
  if (uninitialize)
    ::CoUninitialize();
  if (FAILED(result) || aborted) {
    if (error)
      *error =
          L"无法解压输入方案包：" +
          SystemError(FAILED(result) ? HRESULT_CODE(result) : ERROR_CANCELLED);
    return false;
  }
  return true;
}

bool WanxiangSchemeManager::ValidateStaging(
    const WanxiangUpdateManager::SchemeRelease& release,
    std::filesystem::path* source_root,
    std::wstring* error) const {
  std::vector<std::filesystem::path> candidates = {StagingPath()};
  std::error_code file_error;
  for (const auto& entry :
       std::filesystem::directory_iterator(StagingPath(), file_error)) {
    if (entry.is_directory())
      candidates.push_back(entry.path());
  }
  for (const auto& candidate : candidates) {
    if (!std::filesystem::is_regular_file(candidate / L"version.txt") ||
        !std::filesystem::is_regular_file(candidate /
                                          L"wanxiang_lite.schema.yaml") ||
        !std::filesystem::is_directory(candidate / L"dicts") ||
        !std::filesystem::is_directory(candidate / L"lua")) {
      continue;
    }
    std::ifstream input(candidate / L"version.txt", std::ios::binary);
    std::string version_utf8;
    std::getline(input, version_utf8);
    if (NormalizedVersion(u8tow(version_utf8)) !=
        NormalizedVersion(release.tag)) {
      if (error)
        *error = L"输入方案包内版本与检查到的发布版本不一致。";
      return false;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(candidate, file_error)) {
      if (file_error || entry.is_symlink()) {
        if (error)
          *error = L"输入方案包包含无法安全处理的文件。";
        return false;
      }
    }
    *source_root = candidate;
    return true;
  }
  if (error)
    *error = L"输入方案包缺少 version.txt、方案文件、词库或 Lua 目录。";
  return false;
}

bool WanxiangSchemeManager::IsPreserved(
    const std::filesystem::path& relative) const {
  const auto generic = Lowercase(relative.generic_wstring());
  const auto filename = Lowercase(relative.filename().wstring());
  const auto first = Lowercase(relative.begin()->wstring());
  // Package updates are deliberately scoped to the files required by the
  // Lite schema.  Everything else is treated as user or non-Lite content and
  // is never entered into the transaction manifest.
  if (generic == L"custom_phrase.txt" || generic == L"user.yaml" ||
      generic == L"installation.yaml" || generic == L"sync" ||
      generic.compare(0, 5, L"sync/") == 0 || EndsWith(filename, L".userdb") ||
      EndsWith(filename, L".userdb.txt") ||
      filename.compare(0, 8, L"sequence") == 0 || first == L"my_dicts" ||
      (relative.parent_path().empty() && EndsWith(filename, L".custom.yaml"))) {
    return true;
  }

  if (first == L"custom" || first == L"lua")
    return false;
  if (first == L"dicts") {
    return !(EndsWith(filename, L".lite.dict.yaml") ||
             filename == L"en.dict.yaml" || filename == L"mixed.dict.yaml");
  }
  if (relative.has_parent_path() && relative.parent_path() != L".")
    return true;

  static constexpr const wchar_t* kLiteRootFiles[] = {
      L"version.txt",
      L"wanxiang_lite.dict.yaml",
      L"wanxiang_lite.schema.yaml",
      L"wanxiang_algebra.yaml",
      L"wanxiang_symbols.yaml",
      L"custom_phrase.dict.yaml",
      L"wanxiang_abbrev.dict.yaml",
      L"wanxiang_english.dict.yaml",
      L"wanxiang_mixedcode.dict.yaml",
      L"wanxiang_reverse.dict.yaml",
  };
  return std::none_of(
      std::begin(kLiteRootFiles), std::end(kLiteRootFiles),
      [&filename](const wchar_t* allowed) { return filename == allowed; });
}

bool WanxiangSchemeManager::InstallStaged(
    const std::filesystem::path& source_root,
    std::wstring* error) {
  Report(Phase::Installing);
  if (!RecoverInterruptedTransaction(error))
    return false;
  std::error_code file_error;
  std::filesystem::remove_all(TransactionPath(), file_error);
  file_error.clear();
  std::filesystem::create_directories(BackupPath(), file_error);
  if (file_error) {
    if (error)
      *error = L"无法创建输入方案更新备份目录。";
    return false;
  }
  std::vector<ManifestEntry> entries;
  for (const auto& item :
       std::filesystem::recursive_directory_iterator(source_root, file_error)) {
    if (file_error)
      break;
    if (!item.is_regular_file())
      continue;
    const auto relative =
        std::filesystem::relative(item.path(), source_root, file_error);
    if (file_error || !IsSafeRelativePath(relative) || IsPreserved(relative)) {
      file_error.clear();
      continue;
    }
    const auto destination = user_data_ / relative;
    const bool existed = std::filesystem::exists(destination, file_error);
    if (file_error ||
        (existed && !std::filesystem::is_regular_file(destination))) {
      if (error)
        *error = L"输入方案目标路径与现有目录冲突。";
      return false;
    }
    if (existed) {
      const auto backup = BackupPath() / relative;
      std::filesystem::create_directories(backup.parent_path(), file_error);
      if (!file_error)
        std::filesystem::copy_file(
            destination, backup,
            std::filesystem::copy_options::overwrite_existing, file_error);
      if (file_error) {
        if (error)
          *error = L"备份现有输入方案文件失败。";
        return false;
      }
    }
    entries.push_back({existed, relative});
  }
  if (file_error || entries.empty()) {
    if (error)
      *error = L"无法读取待安装的输入方案文件。";
    return false;
  }
  std::ofstream manifest(ManifestPath(), std::ios::binary | std::ios::trunc);
  for (const auto& entry : entries)
    manifest << (entry.existed ? "E\t" : "N\t")
             << wtou8(entry.relative.wstring()) << '\n';
  manifest.close();
  if (!manifest) {
    if (error)
      *error = L"无法写入输入方案更新清单。";
    return false;
  }
  std::ofstream marker(ActiveMarkerPath(), std::ios::binary | std::ios::trunc);
  marker << "active";
  marker.close();
  if (!marker) {
    if (error)
      *error = L"无法创建输入方案更新事务标记。";
    return false;
  }
  MaintenanceScope maintenance;
  for (const auto& entry : entries) {
    const auto source = source_root / entry.relative;
    const auto destination = user_data_ / entry.relative;
    std::filesystem::create_directories(destination.parent_path(), file_error);
    if (!file_error)
      std::filesystem::copy_file(
          source, destination,
          std::filesystem::copy_options::overwrite_existing, file_error);
    if (file_error) {
      std::wstring rollback_error;
      Rollback(&rollback_error);
      if (error) {
        *error = L"安装输入方案文件失败。";
        if (!rollback_error.empty())
          *error += L"\n" + rollback_error;
      }
      return false;
    }
  }
  installed_ = true;
  return true;
}

bool WanxiangSchemeManager::LoadManifest(std::vector<ManifestEntry>* entries,
                                         std::wstring* error) const {
  std::ifstream input(ManifestPath(), std::ios::binary);
  std::string line;
  while (std::getline(input, line)) {
    if (line.size() < 3 || line[1] != '\t' ||
        (line[0] != 'E' && line[0] != 'N')) {
      if (error)
        *error = L"输入方案更新清单已损坏。";
      return false;
    }
    std::filesystem::path relative = u8tow(line.substr(2));
    if (!IsSafeRelativePath(relative)) {
      if (error)
        *error = L"输入方案更新清单包含无效路径。";
      return false;
    }
    entries->push_back({line[0] == 'E', std::move(relative)});
  }
  if (!input.eof() || entries->empty()) {
    if (error)
      *error = L"无法读取输入方案更新清单。";
    return false;
  }
  return true;
}

bool WanxiangSchemeManager::PrepareAndInstall(
    const WanxiangUpdateManager::SchemeRelease& release,
    std::wstring* error) {
  Report(Phase::Preparing);
  installed_tag_ = release.tag;
  if (release.tag.empty() || release.url.empty() || release.size == 0 ||
      release.sha256.size() != 64) {
    if (error)
      *error = L"输入方案发布信息不完整。";
    return false;
  }
  if (!RecoverInterruptedTransaction(error) || !Download(release, error) ||
      !Extract(error)) {
    return false;
  }
  std::filesystem::path source_root;
  if (!ValidateStaging(release, &source_root, error))
    return false;
  return InstallStaged(source_root, error);
}

bool WanxiangSchemeManager::Rollback(std::wstring* error) {
  std::error_code file_error;
  if (!std::filesystem::exists(ActiveMarkerPath(), file_error)) {
    if (file_error && error)
      *error = L"无法检查输入方案更新事务。";
    return !file_error;
  }
  std::vector<ManifestEntry> entries;
  if (!LoadManifest(&entries, error))
    return false;
  MaintenanceScope maintenance;
  for (auto iterator = entries.rbegin(); iterator != entries.rend();
       ++iterator) {
    const auto destination = user_data_ / iterator->relative;
    if (iterator->existed) {
      const auto backup = BackupPath() / iterator->relative;
      std::filesystem::create_directories(destination.parent_path(),
                                          file_error);
      if (!file_error)
        std::filesystem::copy_file(
            backup, destination,
            std::filesystem::copy_options::overwrite_existing, file_error);
    } else {
      std::filesystem::remove(destination, file_error);
    }
    if (file_error) {
      if (error)
        *error = L"恢复更新前的输入方案文件失败。";
      return false;
    }
  }
  std::filesystem::remove_all(TransactionPath(), file_error);
  if (file_error) {
    if (error)
      *error = L"输入方案已恢复，但无法清理事务目录。";
    return false;
  }
  installed_ = false;
  return true;
}

bool WanxiangSchemeManager::RecoverInterruptedTransaction(std::wstring* error) {
  std::error_code file_error;
  const bool active = std::filesystem::exists(ActiveMarkerPath(), file_error);
  if (file_error) {
    if (error)
      *error = L"无法检查未完成的输入方案更新。";
    return false;
  }
  return !active || Rollback(error);
}

bool WanxiangSchemeManager::SaveInstalledVersion(std::wstring* error) const {
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
    if (error)
      *error = L"无法记录已安装的输入方案版本。";
    return false;
  }
  const auto version = NormalizedVersion(installed_tag_);
  FILETIME installed = {};
  ::GetSystemTimeAsFileTime(&installed);
  ULARGE_INTEGER installed_value = {};
  installed_value.LowPart = installed.dwLowDateTime;
  installed_value.HighPart = installed.dwHighDateTime;
  const LSTATUS version_result = ::RegSetValueExW(
      key, L"InstalledSchemeVersion", 0, REG_SZ,
      reinterpret_cast<const BYTE*>(version.c_str()),
      static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
  const LSTATUS time_result =
      ::RegSetValueExW(key, L"LastSchemeInstalled", 0, REG_QWORD,
                       reinterpret_cast<const BYTE*>(&installed_value.QuadPart),
                       sizeof(installed_value.QuadPart));
  ::RegCloseKey(key);
  if (version_result != ERROR_SUCCESS || time_result != ERROR_SUCCESS) {
    if (error)
      *error = L"无法记录已安装的输入方案版本。";
    return false;
  }
  return true;
}

bool WanxiangSchemeManager::Commit(std::wstring* error) {
  if (!installed_)
    return false;
  std::wstring registry_error;
  if (!SaveInstalledVersion(&registry_error))
    LOG(WARNING) << "Unable to save schema update bookkeeping: "
                 << wtou8(registry_error);
  std::error_code file_error;
  std::filesystem::remove_all(TransactionPath(), file_error);
  if (file_error) {
    if (error)
      *error = L"输入方案已安装，但无法清理事务备份。";
    return false;
  }
  std::filesystem::remove_all(StagingPath(), file_error);
  if (file_error)
    LOG(WARNING) << "Unable to clean the schema update staging directory: "
                 << file_error.message();
  installed_ = false;
  return true;
}
