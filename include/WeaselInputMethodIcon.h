#pragma once

#include <WeaselUserSettings.h>

namespace weasel {
// Windows owns the branding slot. Its registered resource is machine-wide,
// independent of the per-user Chinese/ASCII/Caps mode icons.
inline constexpr wchar_t kInputMethodIconKey[] =
    L"Software\\Rime\\Weasel\\InputMethodIcon";
inline constexpr wchar_t kInputMethodProfileKey[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\"
    L"{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}\\LanguageProfile";

struct InputMethodIconSettings {
  std::wstring source;

  static InputMethodIconSettings Load() {
    const UserSettingsStore store(
        IsSettingsPreviewMode() ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE,
        IsSettingsPreviewMode()
            ? L"Software\\Rime\\Weasel\\PreviewUserSettings\\InputMethodIcon"
            : kInputMethodIconKey);
    return {store.ReadString(L"Source")};
  }
  bool operator==(const InputMethodIconSettings& other) const {
    return source == other.source;
  }
  bool operator!=(const InputMethodIconSettings& other) const {
    return !(*this == other);
  }
};

// Used at installation/repair too, so re-registering TSF preserves the choice.
inline std::wstring RegisteredInputMethodIconModule() {
  const UserSettingsStore store(HKEY_LOCAL_MACHINE, kInputMethodIconKey);
  const auto path = store.ReadString(L"Module");
  if (path.empty())
    return {};
  HMODULE module =
      ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
  if (!module)
    return {};
  const bool valid =
      ::FindResourceW(module, MAKEINTRESOURCEW(1), RT_GROUP_ICON) != nullptr;
  ::FreeLibrary(module);
  return valid ? path : std::wstring();
}
}  // namespace weasel
