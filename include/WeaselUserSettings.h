#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace weasel {

// UI preferences are separate from the user's Rime skin and dictionary files.
// Always use the same registry view in the server and both frontend bitnesses.
inline constexpr wchar_t kUserSettingsKey[] =
    L"Software\\Rime\\Weasel\\UserSettings";
inline constexpr wchar_t kPreviewUserSettingsKey[] =
    L"Software\\Rime\\Weasel\\PreviewUserSettings";
inline constexpr wchar_t kAcrylicEnabledSetting[] = L"AcrylicEnabled";
inline constexpr wchar_t kAppearanceThemeModeSetting[] = L"AppearanceThemeMode";
inline constexpr wchar_t kStatusIconChineseSetting[] = L"StatusIconChinese";
inline constexpr wchar_t kStatusIconEnglishSetting[] = L"StatusIconEnglish";
inline constexpr wchar_t kStatusIconCapsSetting[] = L"StatusIconCaps";
// Legacy four-state values are read once and cleared when the three-state
// settings are saved.
inline constexpr wchar_t kStatusIconChineseCapsSetting[] =
    L"StatusIconChineseCaps";
inline constexpr wchar_t kStatusIconEnglishCapsSetting[] =
    L"StatusIconEnglishCaps";
inline constexpr wchar_t kStatusIconCapsBadgeSetting[] = L"StatusIconCapsBadge";
inline constexpr wchar_t kStatusIconCapsModeSetting[] = L"StatusIconCapsMode";
inline constexpr wchar_t kSchemaStatusIconsSubkey[] = L"SchemaStatusIcons";
inline constexpr wchar_t kSchemaStatusIconChineseSetting[] = L"Chinese";
inline constexpr wchar_t kSchemaStatusIconAsciiSetting[] = L"Ascii";
inline constexpr wchar_t kSchemaStatusIconCapsSetting[] = L"Caps";
inline constexpr wchar_t kStatusIconUseGlobalMarker[] = L"|use-global|";
inline constexpr wchar_t kFontSettingsEnabledSetting[] = L"FontSettingsEnabled";

inline bool IsSettingsPreviewMode() {
  wchar_t value[2] = {};
  return ::GetEnvironmentVariableW(L"WEASEL_SETTINGS_PREVIEW", value,
                                   _countof(value)) != 0 &&
         value[0] != L'0';
}

class UserSettingsStore {
 public:
  UserSettingsStore()
      : root_(HKEY_CURRENT_USER),
        key_(IsSettingsPreviewMode() ? kPreviewUserSettingsKey
                                     : kUserSettingsKey) {}
  explicit UserSettingsStore(HKEY root, const wchar_t* key = kUserSettingsKey)
      : root_(root), key_(key) {}
  explicit UserSettingsStore(std::wstring key)
      : root_(HKEY_CURRENT_USER), key_(std::move(key)) {}

  bool ReadBool(const wchar_t* name, bool fallback) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegOpenKeyExW(root_, key_.c_str(), 0,
                                     KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
      return fallback;
    if (result != ERROR_SUCCESS)
      return false;
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    result = ::RegQueryValueExW(key, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&value), &bytes);
    ::RegCloseKey(key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
      return fallback;
    // An unreadable or malformed preference must not enable optional material.
    return result == ERROR_SUCCESS && type == REG_DWORD &&
           bytes == sizeof(value) && value == 1;
  }

  LSTATUS WriteBool(const wchar_t* name, bool value) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegCreateKeyExW(root_, key_.c_str(), 0, nullptr, 0,
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

  std::wstring ReadString(const wchar_t* name,
                          const std::wstring& fallback = {}) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegOpenKeyExW(root_, key_.c_str(), 0,
                                     KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (result != ERROR_SUCCESS)
      return fallback;
    DWORD type = 0;
    DWORD bytes = 0;
    result = ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t)) {
      ::RegCloseKey(key);
      return fallback;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    result = ::RegQueryValueExW(key, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(value.data()), &bytes);
    ::RegCloseKey(key);
    if (result != ERROR_SUCCESS)
      return fallback;
    while (!value.empty() && value.back() == L'\0')
      value.pop_back();
    return value;
  }

  LSTATUS WriteString(const wchar_t* name, const std::wstring& value) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegCreateKeyExW(root_, key_.c_str(), 0, nullptr, 0,
                                       KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
                                       &key, nullptr);
    if (result != ERROR_SUCCESS)
      return result;
    if (value.empty()) {
      result = ::RegDeleteValueW(key, name);
      if (result == ERROR_FILE_NOT_FOUND)
        result = ERROR_SUCCESS;
    } else {
      const DWORD bytes =
          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
      result =
          ::RegSetValueExW(key, name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    }
    ::RegCloseKey(key);
    return result;
  }

  DWORD ReadDword(const wchar_t* name, DWORD fallback) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegOpenKeyExW(root_, key_.c_str(), 0,
                                     KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (result != ERROR_SUCCESS)
      return fallback;
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    result = ::RegQueryValueExW(key, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&value), &bytes);
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD &&
                   bytes == sizeof(value)
               ? value
               : fallback;
  }

  LSTATUS WriteDword(const wchar_t* name, DWORD value) const {
    HKEY key = nullptr;
    LSTATUS result = ::RegCreateKeyExW(root_, key_.c_str(), 0, nullptr, 0,
                                       KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
                                       &key, nullptr);
    if (result != ERROR_SUCCESS)
      return result;
    result =
        ::RegSetValueExW(key, name, 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&value), sizeof(value));
    ::RegCloseKey(key);
    return result;
  }

 private:
  HKEY root_;
  std::wstring key_;
};

inline std::wstring SchemaStatusIconKey(const std::wstring& schema_id) {
  static constexpr wchar_t digits[] = L"0123456789abcdef";
  std::wstring encoded;
  encoded.reserve(schema_id.size() * 4);
  for (wchar_t character : schema_id) {
    const unsigned value = static_cast<unsigned>(character);
    encoded.push_back(digits[(value >> 12) & 0xf]);
    encoded.push_back(digits[(value >> 8) & 0xf]);
    encoded.push_back(digits[(value >> 4) & 0xf]);
    encoded.push_back(digits[value & 0xf]);
  }
  const wchar_t* root =
      IsSettingsPreviewMode() ? kPreviewUserSettingsKey : kUserSettingsKey;
  return std::wstring(root) + L"\\" + kSchemaStatusIconsSubkey + L"\\" +
         encoded;
}

struct StatusIconSettings {
  std::wstring chinese;
  std::wstring english;
  std::wstring caps;

  static StatusIconSettings Load() {
    const UserSettingsStore store;
    StatusIconSettings settings;
    settings.chinese = store.ReadString(kStatusIconChineseSetting);
    settings.english = store.ReadString(kStatusIconEnglishSetting);
    settings.caps = store.ReadString(kStatusIconCapsSetting);
    if (settings.caps.empty() &&
        store.ReadDword(kStatusIconCapsModeSetting, 0) == 1) {
      settings.caps = store.ReadString(kStatusIconEnglishCapsSetting);
      if (settings.caps.empty())
        settings.caps = store.ReadString(kStatusIconChineseCapsSetting);
    }
    return settings;
  }

  LSTATUS Save() const {
    const UserSettingsStore store;
    for (const auto& value : {
             std::pair{kStatusIconChineseSetting, chinese},
             std::pair{kStatusIconEnglishSetting, english},
             std::pair{kStatusIconCapsSetting, caps},
         }) {
      const LSTATUS result = store.WriteString(value.first, value.second);
      if (result != ERROR_SUCCESS)
        return result;
    }
    // Prevent cleared three-state defaults from being repopulated by an old
    // four-state configuration on the next load.
    LSTATUS result = store.WriteString(kStatusIconChineseCapsSetting, L"");
    if (result != ERROR_SUCCESS)
      return result;
    return store.WriteString(kStatusIconEnglishCapsSetting, L"");
  }

  bool operator==(const StatusIconSettings& other) const {
    return chinese == other.chinese && english == other.english &&
           caps == other.caps;
  }

  bool operator!=(const StatusIconSettings& other) const {
    return !(*this == other);
  }
};

struct SchemaStatusIconSettings {
  std::wstring chinese;
  std::wstring ascii;
  std::wstring caps;

  static SchemaStatusIconSettings Load(const std::wstring& schema_id) {
    const UserSettingsStore store(SchemaStatusIconKey(schema_id));
    SchemaStatusIconSettings settings;
    settings.chinese = store.ReadString(kSchemaStatusIconChineseSetting);
    settings.ascii = store.ReadString(kSchemaStatusIconAsciiSetting);
    settings.caps = store.ReadString(kSchemaStatusIconCapsSetting);
    return settings;
  }

  LSTATUS Save(const std::wstring& schema_id) const {
    const UserSettingsStore store(SchemaStatusIconKey(schema_id));
    for (const auto& value : {
             std::pair{kSchemaStatusIconChineseSetting, chinese},
             std::pair{kSchemaStatusIconAsciiSetting, ascii},
             std::pair{kSchemaStatusIconCapsSetting, caps},
         }) {
      const LSTATUS result = store.WriteString(value.first, value.second);
      if (result != ERROR_SUCCESS)
        return result;
    }
    return ERROR_SUCCESS;
  }

  bool operator==(const SchemaStatusIconSettings& other) const {
    return chinese == other.chinese && ascii == other.ascii &&
           caps == other.caps;
  }
  bool operator!=(const SchemaStatusIconSettings& other) const {
    return !(*this == other);
  }
};

inline bool StatusIconUsesGlobal(const std::wstring& value) {
  return value == kStatusIconUseGlobalMarker;
}

enum class FontRole : size_t { Preedit = 0, Candidate, Label, Comment, Count };
enum class FontLanguage : size_t { Chinese = 0, Latin, Count };
enum class FontShape : DWORD { Regular = 0, Bold, Italic };

struct FontChoice {
  std::wstring family;
  DWORD point = 11;
  FontShape shape = FontShape::Regular;

  bool operator==(const FontChoice& other) const {
    return family == other.family && point == other.point &&
           shape == other.shape;
  }
  bool operator!=(const FontChoice& other) const { return !(*this == other); }
};

struct FontSettings {
  static constexpr size_t kRoleCount = static_cast<size_t>(FontRole::Count);
  static constexpr size_t kLanguageCount =
      static_cast<size_t>(FontLanguage::Count);
  static constexpr size_t kChoiceCount = kRoleCount * kLanguageCount;

  bool enabled = false;
  std::array<FontChoice, kChoiceCount> choices{};

  static constexpr size_t Index(FontRole role, FontLanguage language) {
    return static_cast<size_t>(role) * kLanguageCount +
           static_cast<size_t>(language);
  }

  FontChoice& At(FontRole role, FontLanguage language) {
    return choices[Index(role, language)];
  }
  const FontChoice& At(FontRole role, FontLanguage language) const {
    return choices[Index(role, language)];
  }

  static FontSettings Defaults() {
    FontSettings settings;
    constexpr DWORD points[kRoleCount] = {11, 11, 9, 10};
    for (size_t role = 0; role < kRoleCount; ++role) {
      settings.choices[role * kLanguageCount] = {
          L"Microsoft YaHei", points[role], FontShape::Regular};
      settings.choices[role * kLanguageCount + 1] = {L"Segoe UI", points[role],
                                                     FontShape::Regular};
    }
    return settings;
  }

  static FontSettings Load() {
    static constexpr const wchar_t* kFamilyNames[kChoiceCount] = {
        L"FontPreeditChineseFamily",   L"FontPreeditLatinFamily",
        L"FontCandidateChineseFamily", L"FontCandidateLatinFamily",
        L"FontLabelChineseFamily",     L"FontLabelLatinFamily",
        L"FontCommentChineseFamily",   L"FontCommentLatinFamily",
    };
    static constexpr const wchar_t* kPointNames[kChoiceCount] = {
        L"FontPreeditChinesePoint",   L"FontPreeditLatinPoint",
        L"FontCandidateChinesePoint", L"FontCandidateLatinPoint",
        L"FontLabelChinesePoint",     L"FontLabelLatinPoint",
        L"FontCommentChinesePoint",   L"FontCommentLatinPoint",
    };
    static constexpr const wchar_t* kShapeNames[kChoiceCount] = {
        L"FontPreeditChineseShape",   L"FontPreeditLatinShape",
        L"FontCandidateChineseShape", L"FontCandidateLatinShape",
        L"FontLabelChineseShape",     L"FontLabelLatinShape",
        L"FontCommentChineseShape",   L"FontCommentLatinShape",
    };
    FontSettings settings = Defaults();
    const UserSettingsStore store;
    settings.enabled = store.ReadBool(kFontSettingsEnabledSetting, false);
    if (!settings.enabled)
      return settings;
    for (size_t i = 0; i < kChoiceCount; ++i) {
      settings.choices[i].family =
          store.ReadString(kFamilyNames[i], settings.choices[i].family);
      settings.choices[i].point = (std::max)(
          6ul, (std::min)(72ul, store.ReadDword(kPointNames[i],
                                                settings.choices[i].point)));
      const DWORD shape = store.ReadDword(kShapeNames[i], 0);
      settings.choices[i].shape = shape <= static_cast<DWORD>(FontShape::Italic)
                                      ? static_cast<FontShape>(shape)
                                      : FontShape::Regular;
    }
    return settings;
  }

  LSTATUS Save() const {
    static constexpr const wchar_t* kFamilyNames[kChoiceCount] = {
        L"FontPreeditChineseFamily",   L"FontPreeditLatinFamily",
        L"FontCandidateChineseFamily", L"FontCandidateLatinFamily",
        L"FontLabelChineseFamily",     L"FontLabelLatinFamily",
        L"FontCommentChineseFamily",   L"FontCommentLatinFamily",
    };
    static constexpr const wchar_t* kPointNames[kChoiceCount] = {
        L"FontPreeditChinesePoint",   L"FontPreeditLatinPoint",
        L"FontCandidateChinesePoint", L"FontCandidateLatinPoint",
        L"FontLabelChinesePoint",     L"FontLabelLatinPoint",
        L"FontCommentChinesePoint",   L"FontCommentLatinPoint",
    };
    static constexpr const wchar_t* kShapeNames[kChoiceCount] = {
        L"FontPreeditChineseShape",   L"FontPreeditLatinShape",
        L"FontCandidateChineseShape", L"FontCandidateLatinShape",
        L"FontLabelChineseShape",     L"FontLabelLatinShape",
        L"FontCommentChineseShape",   L"FontCommentLatinShape",
    };
    const UserSettingsStore store;
    for (size_t i = 0; i < kChoiceCount; ++i) {
      LSTATUS result = store.WriteString(kFamilyNames[i], choices[i].family);
      if (result != ERROR_SUCCESS)
        return result;
      result = store.WriteDword(kPointNames[i], choices[i].point);
      if (result != ERROR_SUCCESS)
        return result;
      result = store.WriteDword(kShapeNames[i],
                                static_cast<DWORD>(choices[i].shape));
      if (result != ERROR_SUCCESS)
        return result;
    }
    return store.WriteBool(kFontSettingsEnabledSetting, enabled);
  }

  bool operator==(const FontSettings& other) const {
    return enabled == other.enabled && choices == other.choices;
  }
  bool operator!=(const FontSettings& other) const { return !(*this == other); }
};

enum class AppearanceThemeMode : DWORD {
  FollowSystem = 0,
  Light = 1,
  Dark = 2,
};

inline bool ResolveAppearanceDarkMode(AppearanceThemeMode mode,
                                      bool system_dark) {
  if (mode == AppearanceThemeMode::Light)
    return false;
  if (mode == AppearanceThemeMode::Dark)
    return true;
  return system_dark;
}

struct UserSettings {
  // Preserve this Acrylic branch's existing appearance when no choice is saved.
  bool acrylic = true;
  AppearanceThemeMode appearance_theme_mode = AppearanceThemeMode::FollowSystem;

  static UserSettings Load() {
    UserSettings settings;
    settings.acrylic =
        UserSettingsStore().ReadBool(kAcrylicEnabledSetting, settings.acrylic);
    const DWORD theme_mode = UserSettingsStore().ReadDword(
        kAppearanceThemeModeSetting,
        static_cast<DWORD>(settings.appearance_theme_mode));
    if (theme_mode <= static_cast<DWORD>(AppearanceThemeMode::Dark))
      settings.appearance_theme_mode =
          static_cast<AppearanceThemeMode>(theme_mode);
    return settings;
  }
};

inline UINT UserSettingsChangedMessage() {
  static const UINT message =
      ::RegisterWindowMessageW(L"Weasel.UserSettingsChanged.v1");
  return message;
}

inline void NotifyUserSettingsChanged() {
  if (IsSettingsPreviewMode())
    return;
  const UINT message = UserSettingsChangedMessage();
  if (message)
    ::PostMessageW(HWND_BROADCAST, message, 0, 0);
}

}  // namespace weasel
