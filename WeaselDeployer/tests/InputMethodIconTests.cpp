#include <windows.h>
#include <iostream>
#include "../InputMethodIcon.h"

namespace {
void Require(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
}  // namespace

int Run(int argc, wchar_t** argv) {
  if (argc != 3)
    return 2;
  const auto ico = input_method_icon::Read(argv[1]);
  Require(
      input_method_icon::Digest({}) ==
          L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "SHA-256 digest differs from standard test vector");
  Require(input_method_icon::Validate(ico), "Default ICO validation failed");
  const auto module_path = std::filesystem::path(argv[2]);
  Require(input_method_icon::BuildModule(ico, module_path),
          "Resource DLL creation failed");
  HMODULE module =
      ::LoadLibraryExW(module_path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
  Require(module != nullptr, "Windows could not load resource DLL");
  const HRSRC group =
      ::FindResourceW(module, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
  Require(group != nullptr, "Icon group missing");
  const auto* group_data =
      static_cast<const BYTE*>(::LockResource(::LoadResource(module, group)));
  WORD count = 0;
  std::memcpy(&count, group_data + 4, 2);
  Require(count == input_method_icon::Word(ico, 4), "ICO frame count changed");
  for (WORD i = 0; i < count; ++i) {
    const HRSRC frame =
        ::FindResourceW(module, MAKEINTRESOURCEW(i + 1), RT_ICON);
    const auto size = input_method_icon::Dword(ico, 6 + i * 16 + 8);
    const auto offset = input_method_icon::Dword(ico, 6 + i * 16 + 12);
    Require(frame && ::SizeofResource(module, frame) == size,
            "Frame size changed");
    Require(std::memcmp(::LockResource(::LoadResource(module, frame)),
                        ico.data() + offset, size) == 0,
            "Frame pixels were modified");
  }
  for (int size : {16, 20, 24, 28, 32, 48}) {
    HICON icon = static_cast<HICON>(
        ::LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0));
    Require(icon != nullptr, "Windows icon size extraction failed");
    ::DestroyIcon(icon);
  }
  ::FreeLibrary(module);
  HICON shell_icon = nullptr;
  Require(
      ::ExtractIconExW(module_path.c_str(), 0, nullptr, &shell_icon, 1) == 1,
      "Shell icon-index extraction failed");
  ::DestroyIcon(shell_icon);
  auto invalid = ico;
  invalid.resize(5);
  Require(!input_method_icon::Validate(invalid), "Truncated ICO accepted");
  invalid = ico;
  const DWORD overflow = 0xfffffff0;
  std::memcpy(invalid.data() + 18, &overflow, 4);
  Require(!input_method_icon::Validate(invalid), "Out-of-range ICO accepted");

  // A private HKCU sandbox models HKLM; never touch installed TSF profiles.
  const auto sandbox = L"Software\\Rime\\Weasel\\Tests\\Branding-" +
                       std::to_wstring(::GetCurrentProcessId());
  HKEY root = nullptr;
  Require(::RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.c_str(), 0, nullptr, 0,
                            KEY_ALL_ACCESS, nullptr, &root,
                            nullptr) == ERROR_SUCCESS,
          "Sandbox registry creation failed");
  const std::wstring key =
      std::wstring(weasel::kInputMethodProfileKey) +
      L"\\0x00000804\\{3D02CAB6-2B8E-4781-BA20-1C9267529467}";
  const std::wstring class_key =
      L"SOFTWARE\\Classes\\CLSID\\{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}"
      L"\\InprocServer32";
  {
    input_method_icon::RegistryTransaction setup;
    Require(setup.String(root, key, 0, L"IconFile", L"original.dll") ==
                ERROR_SUCCESS,
            "Setup failed");
    Require(
        setup.String(root, key, 0, L"Sentinel", L"preserve") == ERROR_SUCCESS,
        "Setup failed");
    const DWORD enabled = 1;
    Require(
        setup.Set(root, key, 0, L"Enable", REG_DWORD,
                  reinterpret_cast<const BYTE*>(&enabled), 4) == ERROR_SUCCESS,
        "Setup failed");
    setup.Commit();
  }
  {
    input_method_icon::RegistryTransaction rollback;
    unsigned count = 0;
    Require(input_method_icon::UpdateProfiles(root, 0, module_path, rollback,
                                              &count) == ERROR_SUCCESS &&
                count == 1,
            "Existing profile update failed");
    // Simulate a later apply failure: do not commit.
  }
  Require(input_method_icon::ReadRegistryString(root, key.c_str(), L"IconFile",
                                                0) == L"original.dll",
          "Rollback did not restore original icon");
  DWORD restored_index = 0, restored_size = sizeof(restored_index);
  Require(
      ::RegGetValueW(root, key.c_str(), L"IconIndex", RRF_RT_REG_DWORD, nullptr,
                     &restored_index, &restored_size) == ERROR_FILE_NOT_FOUND,
      "Rollback did not remove previously absent value");
  {
    input_method_icon::RegistryTransaction commit;
    unsigned count = 0;
    Require(input_method_icon::UpdateProfiles(root, 0, module_path, commit,
                                              &count) == ERROR_SUCCESS &&
                count == 1,
            "Profile update failed");
    commit.Commit();
  }
  Require(input_method_icon::ReadRegistryString(root, key.c_str(), L"IconFile",
                                                0) == module_path,
          "Updated icon path missing");
  Require(input_method_icon::ReadRegistryString(root, key.c_str(), L"Sentinel",
                                                0) == L"preserve",
          "Unrelated profile metadata changed");
  DWORD enabled = 0, size = sizeof(enabled);
  Require(::RegGetValueW(root, key.c_str(), L"Enable", RRF_RT_REG_DWORD,
                         nullptr, &enabled, &size) == ERROR_SUCCESS &&
              enabled == 1,
          "Profile enabled state changed");
  {
    input_method_icon::RegistryTransaction restore;
    unsigned count = 0;
    Require(input_method_icon::UpdateProfiles(root, 0, L"", restore, &count) ==
                ERROR_FILE_NOT_FOUND,
            "Missing default component should reject restore");
  }
  Require(input_method_icon::ReadRegistryString(root, key.c_str(), L"IconFile",
                                                0) == module_path,
          "Failed restore changed the active icon");
  {
    input_method_icon::RegistryTransaction setup;
    // Empty name is the default COM-server registry value.
    Require(setup.String(root, class_key, 0, L"", L"installed-weasel.dll") ==
                ERROR_SUCCESS,
            "Default component setup failed");
    setup.Commit();
  }
  {
    input_method_icon::RegistryTransaction restore;
    unsigned count = 0;
    Require(input_method_icon::UpdateProfiles(root, 0, L"", restore, &count) ==
                    ERROR_SUCCESS &&
                count == 1,
            "Restore default failed");
    restore.Commit();
  }
  Require(input_method_icon::ReadRegistryString(root, key.c_str(), L"IconFile",
                                                0) == L"installed-weasel.dll",
          "Restore did not use installed component");
  ::RegCloseKey(root);
  ::RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
  std::cout << "Branding resource round-trip, 6 display sizes, malformed ICO "
               "rejection, "
               "profile update/rollback, default restore and metadata "
               "preservation passed.\n";
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << " (Win32 " << ::GetLastError() << ")\n";
    return 1;
  }
}
