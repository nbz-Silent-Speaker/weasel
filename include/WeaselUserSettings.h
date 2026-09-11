#pragma once

#include <windows.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace weasel {

// UI preferences are separate from the user's Rime skin and dictionary files.
// Always use the same registry view in the server and both frontend bitnesses.
inline constexpr wchar_t kUserSettingsKey[] =
    L"Software\\Rime\\Weasel\\UserSettings";
inline constexpr wchar_t kAcrylicEnabledSetting[] = L"AcrylicEnabled";

class UserSettingsStore {
 public:
  explicit UserSettingsStore(HKEY root = HKEY_CURRENT_USER,
                             const wchar_t* key = kUserSettingsKey)
      : root_(root), key_(key) {}

  bool ReadBool(const wchar_t* name, bool fallback) const {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    const LSTATUS result = ::RegGetValueW(
        root_, key_, name, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr,
        &value, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
      return fallback;
    // An unreadable or malformed preference must not enable optional material.
    return result == ERROR_SUCCESS && value <= 1 && value != 0;
  }

  LSTATUS WriteBool(const wchar_t* name, bool value) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegCreateKeyExW(root_, key_, 0, nullptr, 0,
                                       KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
                                       &key, nullptr);
    if (result != ERROR_SUCCESS)
      return result;
    const DWORD encoded = value ? 1 : 0;
    result = ::RegSetValueExW(key, name, 0, REG_DWORD,
                              reinterpret_cast<const BYTE*>(&encoded),
                              sizeof(encoded));
    ::RegCloseKey(key);
    return result;
  }

 private:
  HKEY root_;
  const wchar_t* key_;
};

struct UserSettings {
  // Preserve this Acrylic branch's existing appearance when no choice is saved.
  bool acrylic = true;

  static UserSettings Load() {
    UserSettings settings;
    settings.acrylic =
        UserSettingsStore().ReadBool(kAcrylicEnabledSetting, settings.acrylic);
    return settings;
  }
};

inline UINT UserSettingsChangedMessage() {
  static const UINT message =
      ::RegisterWindowMessageW(L"Weasel.UserSettingsChanged.v1");
  return message;
}

inline void NotifyUserSettingsChanged() {
  const UINT message = UserSettingsChangedMessage();
  if (message)
    ::PostMessageW(HWND_BROADCAST, message, 0, 0);
}

}  // namespace weasel
