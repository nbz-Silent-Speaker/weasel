#pragma once

#include <WeaselInputMethodIcon.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

namespace input_method_icon {
using Bytes = std::vector<BYTE>;

inline Bytes Read(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary | std::ios::ate);
  const auto size = stream.tellg();
  if (size < 6 || size > 16 * 1024 * 1024)
    return {};
  Bytes data(static_cast<size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(data.data()), data.size());
  return stream ? data : Bytes();
}

inline WORD Word(const Bytes& data, size_t offset) {
  WORD value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}
inline DWORD Dword(const Bytes& data, size_t offset) {
  DWORD value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

// Validate the directory and every image before handing anything to the
// elevated writer. PNG-compressed ICO frames are preserved without resampling.
inline bool Validate(const Bytes& ico) {
  if (ico.size() < 6 || Word(ico, 0) != 0 || Word(ico, 2) != 1)
    return false;
  const unsigned count = Word(ico, 4);
  if (!count || count > 256 || ico.size() < 6 + count * 16)
    return false;
  for (unsigned i = 0; i < count; ++i) {
    const size_t entry = 6 + i * 16;
    const size_t size = Dword(ico, entry + 8);
    const size_t offset = Dword(ico, entry + 12);
    if (!size || offset < 6 + count * 16 || offset > ico.size() ||
        size > ico.size() - offset)
      return false;
    HICON image = ::CreateIconFromResourceEx(
        const_cast<BYTE*>(ico.data() + offset), static_cast<DWORD>(size), TRUE,
        0x00030000, ico[entry] ? ico[entry] : 256,
        ico[entry + 1] ? ico[entry + 1] : 256, LR_DEFAULTCOLOR);
    if (!image)
      return false;
    ::DestroyIcon(image);
  }
  return true;
}

inline bool Write(const std::filesystem::path& file, const Bytes& data) {
  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(data.data()), data.size());
  stream.close();
  return !!stream;
}

inline std::wstring SystemError(DWORD error) {
  wchar_t* message = nullptr;
  ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<LPWSTR>(&message), 0,
                   nullptr);
  std::wstring result = message ? message : L"";
  if (message)
    ::LocalFree(message);
  while (!result.empty() && iswspace(result.back()))
    result.pop_back();
  return result + L" (" + std::to_wstring(error) + L")";
}

// Resource-only PE: no imports, code, entry point, or executable section.
// The same resource image can be extracted by 32/64-bit Windows consumers.
inline bool BuildModule(const Bytes& ico, const std::filesystem::path& path) {
  if (!Validate(ico))
    return false;
  Bytes pe(1024, 0);
  IMAGE_DOS_HEADER dos{};
  dos.e_magic = IMAGE_DOS_SIGNATURE;
  dos.e_lfanew = 128;
  std::memcpy(pe.data(), &dos, sizeof(dos));
  IMAGE_NT_HEADERS32 nt{};
  nt.Signature = IMAGE_NT_SIGNATURE;
  nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
  nt.FileHeader.NumberOfSections = 1;
  nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
  nt.FileHeader.Characteristics =
      IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_DLL;
  auto& optional = nt.OptionalHeader;
  optional.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
  optional.ImageBase = 0x10000000;
  optional.SectionAlignment = 4096;
  optional.FileAlignment = 512;
  optional.MajorOperatingSystemVersion = 6;
  optional.MajorSubsystemVersion = 6;
  optional.SizeOfImage = 8192;
  optional.SizeOfHeaders = 512;
  optional.SizeOfInitializedData = 512;
  optional.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
  optional.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
  optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE] = {4096, 16};
  std::memcpy(pe.data() + 128, &nt, sizeof(nt));
  IMAGE_SECTION_HEADER section{};
  std::memcpy(section.Name, ".rsrc", 5);
  section.Misc.VirtualSize = 16;
  section.VirtualAddress = 4096;
  section.SizeOfRawData = 512;
  section.PointerToRawData = 512;
  section.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
  std::memcpy(pe.data() + 128 + sizeof(nt), &section, sizeof(section));
  if (!Write(path, pe))
    return false;
  HANDLE update = ::BeginUpdateResourceW(path.c_str(), TRUE);
  if (!update)
    return false;
  const WORD count = Word(ico, 4);
  Bytes group(6 + count * 14, 0);
  std::memcpy(group.data(), ico.data(), 6);
  bool success = true;
  for (WORD i = 0; i < count && success; ++i) {
    const size_t entry = 6 + i * 16;
    const WORD id = i + 1;
    std::memcpy(group.data() + 6 + i * 14, ico.data() + entry, 12);
    std::memcpy(group.data() + 18 + i * 14, &id, sizeof(id));
    success = ::UpdateResourceW(
                  update, RT_ICON, MAKEINTRESOURCEW(id), 0,
                  const_cast<BYTE*>(ico.data() + Dword(ico, entry + 12)),
                  Dword(ico, entry + 8)) != FALSE;
  }
  if (success)
    success = ::UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1), 0,
                                group.data(),
                                static_cast<DWORD>(group.size())) != FALSE;
  const bool committed = ::EndUpdateResourceW(update, !success) != FALSE;
  return success && committed;
}

inline std::wstring Digest(const Bytes& bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BYTE hash[32]{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0)
    return {};
  BCRYPT_HASH_HANDLE context = nullptr;
  auto status =
      BCryptCreateHash(algorithm, &context, nullptr, 0, nullptr, 0, 0);
  if (status >= 0)
    status = BCryptHashData(context, const_cast<BYTE*>(bytes.data()),
                            static_cast<ULONG>(bytes.size()), 0);
  if (status >= 0)
    status = BCryptFinishHash(context, hash, sizeof(hash), 0);
  if (context)
    BCryptDestroyHash(context);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status < 0)
    return {};
  std::wstring value;
  for (BYTE byte : hash) {
    value += L"0123456789abcdef"[byte >> 4];
    value += L"0123456789abcdef"[byte & 15];
  }
  return value;
}

// Record exact types/bytes, including missing values. A failure restores all
// earlier writes; existing Enable, default input profile and HKL are untouched.
class RegistryTransaction {
 public:
  ~RegistryTransaction() {
    if (!committed_) {
      for (auto i = changes_.rbegin(); i != changes_.rend(); ++i) {
        if (i->present)
          ::RegSetValueExW(i->key, i->name.c_str(), 0, i->type, i->bytes.data(),
                           static_cast<DWORD>(i->bytes.size()));
        else
          ::RegDeleteValueW(i->key, i->name.c_str());
      }
    }
    for (auto& change : changes_)
      ::RegCloseKey(change.key);
  }
  LSTATUS Set(HKEY root,
              const std::wstring& path,
              REGSAM view,
              const wchar_t* name,
              DWORD type,
              const BYTE* bytes,
              DWORD size) {
    Change change{};
    auto result = ::RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0,
                                    KEY_QUERY_VALUE | KEY_SET_VALUE | view,
                                    nullptr, &change.key, nullptr);
    if (result != ERROR_SUCCESS)
      return result;
    change.name = name;
    DWORD old_size = 0;
    result = ::RegQueryValueExW(change.key, name, nullptr, &change.type,
                                nullptr, &old_size);
    change.present = result == ERROR_SUCCESS;
    if (change.present) {
      change.bytes.resize(old_size);
      result = ::RegQueryValueExW(change.key, name, nullptr, &change.type,
                                  change.bytes.data(), &old_size);
    }
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
      ::RegCloseKey(change.key);
      return result;
    }
    changes_.push_back(std::move(change));
    if (size) {
      result =
          ::RegSetValueExW(changes_.back().key, name, 0, type, bytes, size);
      if (result != ERROR_SUCCESS)
        return result;
      Bytes actual(size);
      DWORD actual_size = size, actual_type = 0;
      result = ::RegQueryValueExW(changes_.back().key, name, nullptr,
                                  &actual_type, actual.data(), &actual_size);
      return result == ERROR_SUCCESS && actual_type == type &&
                     actual_size == size &&
                     !std::memcmp(actual.data(), bytes, size)
                 ? ERROR_SUCCESS
                 : ERROR_WRITE_FAULT;
    }
    result = ::RegDeleteValueW(changes_.back().key, name);
    return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
  }
  LSTATUS String(HKEY root,
                 const std::wstring& key,
                 REGSAM view,
                 const wchar_t* name,
                 const std::wstring& value) {
    return Set(root, key, view, name, REG_SZ,
               reinterpret_cast<const BYTE*>(value.c_str()),
               value.empty() ? 0 : static_cast<DWORD>((value.size() + 1) * 2));
  }
  void Commit() { committed_ = true; }
  RegistryTransaction() = default;
  RegistryTransaction(const RegistryTransaction&) = delete;
  RegistryTransaction& operator=(const RegistryTransaction&) = delete;

 private:
  struct Change {
    HKEY key = nullptr;
    std::wstring name;
    DWORD type = 0;
    Bytes bytes;
    bool present = false;
  };
  std::vector<Change> changes_;
  bool committed_ = false;
};

inline std::wstring ReadRegistryString(HKEY root,
                                       const wchar_t* path,
                                       const wchar_t* name,
                                       REGSAM view) {
  HKEY key = nullptr;
  if (::RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | view, &key) !=
      ERROR_SUCCESS)
    return {};
  DWORD size = 0, type = 0;
  std::wstring result;
  if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) ==
          ERROR_SUCCESS &&
      type == REG_SZ && size >= sizeof(wchar_t)) {
    result.resize(size / sizeof(wchar_t));
    if (::RegQueryValueExW(key, name, nullptr, &type,
                           reinterpret_cast<BYTE*>(result.data()),
                           &size) != ERROR_SUCCESS)
      result.clear();
    while (!result.empty() && result.back() == 0)
      result.pop_back();
  }
  ::RegCloseKey(key);
  return result;
}

// Update only the two branding values of already installed Weasel profiles.
inline LSTATUS UpdateProfiles(HKEY root,
                              REGSAM view,
                              const std::wstring& module,
                              RegistryTransaction& transaction,
                              unsigned* updated) {
  const std::wstring default_module =
      ReadRegistryString(root,
                         L"SOFTWARE\\Classes\\CLSID\\{A3F4CDED-B1E9-41EE-9CA6-"
                         L"7B4D0DE6CB0A}\\InprocServer32",
                         nullptr, view);
  const auto& icon = module.empty() ? default_module : module;
  for (const wchar_t* language : {L"0x00000404", L"0x00000804", L"0x00000c04",
                                  L"0x00001004", L"0x00001404"}) {
    const std::wstring key = std::wstring(weasel::kInputMethodProfileKey) +
                             L"\\" + language +
                             L"\\{3D02CAB6-2B8E-4781-BA20-1C9267529467}";
    HKEY existing = nullptr;
    auto result = ::RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE | view,
                                  &existing);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
      continue;
    if (result != ERROR_SUCCESS)
      return result;
    ::RegCloseKey(existing);
    if (icon.empty())
      return ERROR_FILE_NOT_FOUND;
    result = transaction.String(root, key, view, L"IconFile", icon);
    if (result != ERROR_SUCCESS)
      return result;
    const DWORD index = 0;
    result =
        transaction.Set(root, key, view, L"IconIndex", REG_DWORD,
                        reinterpret_cast<const BYTE*>(&index), sizeof(index));
    if (result != ERROR_SUCCESS)
      return result;
    ++*updated;
  }
  return ERROR_SUCCESS;
}

inline DWORD ApplyElevated(const std::wstring& source) {
  // A system-owned, stable directory survives versioned installation updates.
  std::wstring saved_source, module;
  if (!source.empty()) {
    const Bytes ico = Read(source);
    if (!Validate(ico))
      return ERROR_INVALID_DATA;
    const auto digest = Digest(ico);
    if (digest.empty())
      return ERROR_INVALID_DATA;
    PWSTR program_data = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr,
                                      &program_data)))
      return ERROR_PATH_NOT_FOUND;
    const auto directory =
        std::filesystem::path(program_data) / L"WeaselInputMethodIcons";
    ::CoTaskMemFree(program_data);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
            SDDL_REVISION_1, &descriptor, nullptr))
      return ::GetLastError();
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const bool created =
        ::CreateDirectoryW(directory.c_str(), &security) != FALSE;
    const DWORD directory_error = created ? ERROR_SUCCESS : ::GetLastError();
    // Do not traverse a link or a user-writable pre-created directory.
    const DWORD attributes = ::GetFileAttributesW(directory.c_str());
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR existing_security = nullptr;
    bool trusted_owner = created;
    if (!created && directory_error == ERROR_ALREADY_EXISTS &&
        ::GetNamedSecurityInfoW(directory.c_str(), SE_FILE_OBJECT,
                                OWNER_SECURITY_INFORMATION, &owner, nullptr,
                                nullptr, nullptr,
                                &existing_security) == ERROR_SUCCESS) {
      trusted_owner = ::IsWellKnownSid(owner, WinBuiltinAdministratorsSid) ||
                      ::IsWellKnownSid(owner, WinLocalSystemSid);
    }
    if (existing_security)
      ::LocalFree(existing_security);
    bool secured = false;
    if (trusted_owner && (created || directory_error == ERROR_ALREADY_EXISTS) &&
        attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      secured = ::SetFileSecurityW(directory.c_str(),
                                   DACL_SECURITY_INFORMATION |
                                       PROTECTED_DACL_SECURITY_INFORMATION,
                                   descriptor) != FALSE;
    }
    ::LocalFree(descriptor);
    if (!secured)
      return ERROR_ACCESS_DENIED;
    // Windows may still hold the previous DLL open. Publish an immutable,
    // unique version instead of replacing a resource mapped by another process.
    GUID version{};
    if (FAILED(::CoCreateGuid(&version)))
      return ERROR_GEN_FAILURE;
    wchar_t suffix[40]{};
    ::StringFromGUID2(version, suffix, _countof(suffix));
    const auto stem = digest + suffix;
    saved_source = (directory / (stem + L".ico")).wstring();
    module = (directory / (stem + L".dll")).wstring();
    const auto temporary = directory / (stem + L".tmp");
    if (!BuildModule(ico, temporary)) {
      ::DeleteFileW(temporary.c_str());
      return ERROR_INVALID_DATA;
    }
    if (!::MoveFileExW(temporary.c_str(), module.c_str(), 0)) {
      const auto error = ::GetLastError();
      ::DeleteFileW(temporary.c_str());
      return error;
    }
    // Publish by rename so an existing file is never followed as a link.
    const auto source_temporary = temporary.wstring() + L".ico";
    if (!Write(source_temporary, ico)) {
      ::DeleteFileW(source_temporary.c_str());
      return ERROR_WRITE_FAULT;
    }
    if (!::MoveFileExW(source_temporary.c_str(), saved_source.c_str(), 0)) {
      const auto error = ::GetLastError();
      ::DeleteFileW(source_temporary.c_str());
      return error;
    }
  }
  RegistryTransaction transaction;
  unsigned updated = 0;
  for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
    const auto result =
        UpdateProfiles(HKEY_LOCAL_MACHINE, view, module, transaction, &updated);
    if (result != ERROR_SUCCESS)
      return result;
  }
  if (!updated)
    return ERROR_NOT_FOUND;
  auto result =
      transaction.String(HKEY_LOCAL_MACHINE, weasel::kInputMethodIconKey,
                         KEY_WOW64_64KEY, L"Source", saved_source);
  if (result == ERROR_SUCCESS)
    result = transaction.String(HKEY_LOCAL_MACHINE, weasel::kInputMethodIconKey,
                                KEY_WOW64_64KEY, L"Module", module);
  if (result != ERROR_SUCCESS)
    return result;
  transaction.Commit();
  ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  DWORD_PTR ignored = 0;
  ::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Software\\Microsoft\\CTF"),
                        SMTO_ABORTIFHUNG, 200, &ignored);
  return ERROR_SUCCESS;
}

inline DWORD Apply(HWND owner, const std::wstring& source) {
  if (weasel::IsSettingsPreviewMode()) {
    const weasel::UserSettingsStore store(
        HKEY_CURRENT_USER,
        L"Software\\Rime\\Weasel\\PreviewUserSettings\\InputMethodIcon");
    return store.WriteString(L"Source", source);
  }
  if (!source.empty() &&
      (!std::filesystem::path(source).is_absolute() ||
       source.find(L'"') != std::wstring::npos || !Validate(Read(source))))
    return ERROR_INVALID_DATA;
  wchar_t executable[32768]{};
  if (!::GetModuleFileNameW(nullptr, executable, _countof(executable)))
    return ::GetLastError();
  const std::wstring arguments = L"/input-method-icon \"" + source + L"\"";
  SHELLEXECUTEINFOW execute{sizeof(execute)};
  execute.fMask =
      SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  execute.hwnd = owner;
  execute.lpVerb = L"runas";
  execute.lpFile = executable;
  execute.lpParameters = arguments.c_str();
  execute.nShow = SW_HIDE;
  const HWND frame = ::GetAncestor(owner, GA_ROOT);
  const bool was_enabled = frame && ::IsWindowEnabled(frame);
  if (was_enabled)
    ::EnableWindow(frame, FALSE);
  if (!::ShellExecuteExW(&execute)) {
    const auto error = ::GetLastError();
    if (was_enabled)
      ::EnableWindow(frame, TRUE);
    return error;
  }
  if (!execute.hProcess) {
    if (was_enabled)
      ::EnableWindow(frame, TRUE);
    return ERROR_INVALID_HANDLE;
  }
  // Keep repainting while the short-lived elevated resource writer runs.
  bool quit = false;
  int quit_code = 0;
  while (::MsgWaitForMultipleObjects(1, &execute.hProcess, FALSE, INFINITE,
                                     QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        quit = true;
        quit_code = static_cast<int>(message.wParam);
        continue;
      }
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
  }
  DWORD result = ERROR_GEN_FAILURE;
  ::GetExitCodeProcess(execute.hProcess, &result);
  ::CloseHandle(execute.hProcess);
  if (was_enabled && ::IsWindow(frame)) {
    ::EnableWindow(frame, TRUE);
    ::SetActiveWindow(frame);
  }
  if (quit)
    ::PostQuitMessage(quit_code);
  return result;
}
}  // namespace input_method_icon
