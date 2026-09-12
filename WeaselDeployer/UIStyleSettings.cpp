#include "stdafx.h"
#include <WeaselUtility.h>
#include "UIStyleSettings.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <cstdint>
#include <algorithm>
#include <ctime>

UIStyleSettings::UIStyleSettings(weasel::ColorSchemeTarget target)
    : target_(target) {
  api_ = (RimeLeversApi*)rime_get_api()->find_module("levers")->get_api();
  settings_ = api_->custom_settings_init("weasel", "Weasel::UIStyleSettings");
}

UIStyleSettings::~UIStyleSettings() {
  for (auto config : {&original_, &custom_}) {
    if (config->ptr)
      rime_get_api()->config_close(config);
  }
  api_->custom_settings_destroy(settings_);
}

namespace {
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string Value(RimeConfig* config, const std::string& key) {
  auto value = rime_get_api()->config_get_cstring(config, key.c_str());
  return value ? value : "";
}

bool CopyItem(RimeConfig* from,
              const std::string& source,
              RimeConfig* to,
              const std::string& destination) {
  auto api = rime_get_api();
  RimeConfig item{};
  bool ok = api->config_get_item(from, source.c_str(), &item) &&
            api->config_set_item(to, destination.c_str(), &item);
  if (item.ptr)
    api->config_close(&item);
  return ok;
}
}  // namespace

bool UIStyleSettings::LoadAppearance() {
  auto rime = rime_get_api();
  for (auto* config : {&original_, &custom_}) {
    if (config->ptr)
      rime->config_close(config);
    config->ptr = nullptr;
  }
  const auto user = WeaselUserDataPath();
  original_bytes_ = ReadFile(user / L"weasel.custom.yaml");
  shared_bytes_ = ReadFile(WeaselSharedDataPath() / L"weasel.yaml");
  base_bytes_ = ReadFile(user / L"weasel.yaml");
  // Refresh both the raw catalog and levers' save snapshot every time. An
  // undeployed manual edit is readable; save guards all three source files.
  const bool loaded = api_->load_settings(settings_) != False;
  RimeConfig borrowed{};
  if ((!loaded && !original_bytes_.empty()) ||
      !rime->config_load_string(
          &custom_, original_bytes_.empty() ? "{}" : original_bytes_.c_str()) ||
      !api_->settings_get_config(settings_, &borrowed) ||
      !rime->config_init(&original_) ||
      !palettes_.Load(rime, shared_bytes_, base_bytes_, original_bytes_))
    return false;
  if (!CopyItem(&borrowed, "", &original_, ""))
    return false;
  palettes_.ReadStyle(&original_);
  return ReadFile(user / L"weasel.custom.yaml") == original_bytes_ &&
         ReadFile(user / L"weasel.yaml") == base_bytes_ &&
         ReadFile(WeaselSharedDataPath() / L"weasel.yaml") == shared_bytes_;
}

std::array<std::string, 4> UIStyleSettings::ActiveAppearance() {
  std::array<std::string, 4> result;
  for (size_t i = 0; i < result.size(); ++i) {
    const auto key = std::string(weasel::AppearanceColorKey(i));
    auto id = Value(&original_, key);
    auto source = Value(&original_, key + "_source");
    if (i >= 2) {
      const auto legacy =
          Value(&original_, i == 3 ? "style/color_scheme_normal_dark"
                                   : "style/color_scheme_normal");
      if (!legacy.empty()) {
        id = legacy;
        source.clear();
      }
    }
    result[i] = palettes_.Resolve(id, source);
  }
  return result;
}

bool UIStyleSettings::SaveAppearance(const std::array<std::string, 4>& colors) {
  auto rime = rime_get_api();
  configuration_changed_ = false;
  const auto user = WeaselUserDataPath();
  const auto path = user / L"weasel.custom.yaml";
  const auto unchanged = [&]() {
    return ReadFile(path) == original_bytes_ &&
           ReadFile(user / L"weasel.yaml") == base_bytes_ &&
           ReadFile(WeaselSharedDataPath() / L"weasel.yaml") == shared_bytes_;
  };
  if (!unchanged())
    return false;
  const auto before = ActiveAppearance();
  if (colors == before)
    return true;
  // An existing unavailable or cross-theme value is preserved unless edited.
  for (size_t i = 0; i < colors.size(); ++i) {
    if (colors[i] != before[i] && !palettes_.Find(colors[i]))
      return false;
  }
  // Reload levers before each save attempt so a previous failed attempt cannot
  // leave pending mutations behind. Keep all scheme definitions untouched.
  if (!api_->load_settings(settings_) && !original_bytes_.empty())
    return false;
  for (size_t i = 0; i < colors.size(); ++i) {
    if (colors[i] == before[i])
      continue;
    const std::string key = weasel::AppearanceColorKey(i);
    if (!api_->customize_string(settings_, key.c_str(),
                                weasel::PaletteId(colors[i]).c_str()) ||
        !api_->customize_string(settings_, (key + "_source").c_str(),
                                weasel::PaletteSource(colors[i]).c_str()))
      return false;
    if (i >= 2) {
      const char* legacy =
          i == 3 ? "color_scheme_normal_dark" : "color_scheme_normal";
      if (!api_->customize_item(
              settings_, (std::string("style/") + legacy).c_str(), nullptr))
        return false;
    }
  }
  RimeConfig style{};
  rime->config_get_item(&custom_, "patch/style", &style);
  if (style.ptr) {
    bool modified = false;
    for (size_t i = 2; i < colors.size(); ++i) {
      if (colors[i] != before[i]) {
        rime->config_clear(&style, i == 3 ? "color_scheme_normal_dark"
                                          : "color_scheme_normal");
        modified = true;
      }
    }
    const bool ok =
        !modified || api_->customize_item(settings_, "style", &style);
    rime->config_close(&style);
    if (!ok)
      return false;
  }
  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  const auto backup = path.wstring() + L".before-appearance-" +
                      std::to_wstring(stamp) + L".bak";
  int recorded = 0;
  if (rime->config_get_int(&original_, "__build_info/timestamps/weasel.custom",
                           &recorded) &&
      recorded == std::time(nullptr))
    ::Sleep(1100);
  if (!unchanged())
    return false;
  const bool existed = std::filesystem::exists(path);
  if (existed && !::CopyFileW(path.c_str(), backup.c_str(), TRUE))
    return false;
  const bool saved = api_->save_settings(settings_) != False;
  RimeConfig verify{};
  const auto updated = ReadFile(path);
  const bool valid = saved && !updated.empty() && updated != original_bytes_ &&
                     rime->config_load_string(&verify, updated.c_str());
  if (verify.ptr)
    rime->config_close(&verify);
  if (!valid) {
    if (existed)
      ::CopyFileW(backup.c_str(), path.c_str(), FALSE);
    else if (!updated.empty())
      ::DeleteFileW(path.c_str());
    return false;
  }
  original_bytes_ = updated;
  configuration_changed_ = true;
  return true;
}

int UIStyleSettings::PreviewLayoutInt(const char* key, int fallback) {
  int value = fallback;
  const auto path = std::string("style/layout/") + key;
  rime_get_api()->config_get_int(&original_, path.c_str(), &value);
  return (std::max)(0, value);
}

COLORREF UIStyleSettings::PreviewColor(const std::string& id,
                                       const char* key,
                                       COLORREF fallback) {
  auto* config = palettes_.Config(id);
  if (!config)
    return fallback;
  const std::string prefix =
      "preset_color_schemes/" + weasel::PaletteId(id) + "/";
  const auto text = Value(config, prefix + key);
  if (text.empty())
    return fallback;
  try {
    const auto color = std::stoull(text, nullptr, 0);
    const auto format = Value(config, prefix + "color_format");
    if (format == "rgba")
      return RGB((color >> 24) & 255, (color >> 16) & 255, (color >> 8) & 255);
    if (format == "argb")
      return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
    return static_cast<COLORREF>(color & 0xffffff);
  } catch (...) {
    return fallback;
  }
}

bool UIStyleSettings::GetPresetColorSchemes(
    std::vector<ColorSchemeInfo>* result) {
  if (!result)
    return false;
  result->clear();
  RimeConfig config = {0};
  api_->settings_get_config(settings_, &config);
  RimeApi* rime = rime_get_api();
  RimeConfigIterator preset = {0};
  if (!rime->config_begin_map(&preset, &config, "preset_color_schemes")) {
    return false;
  }
  while (rime->config_next(&preset)) {
    RimeConfigIterator scheme = {0};
    if (!rime->config_begin_map(&scheme, &config, preset.path))
      continue;
    rime->config_end(&scheme);
    std::string name_key(preset.path);
    name_key += "/name";
    const char* name = rime->config_get_cstring(&config, name_key.c_str());
    std::string author_key(preset.path);
    author_key += "/author";
    const char* author = rime->config_get_cstring(&config, author_key.c_str());
    ColorSchemeInfo info;
    info.color_scheme_id = preset.key;
    info.name = name ? name : preset.key;
    if (author)
      info.author = author;
    result->push_back(info);
  }
  rime->config_end(&preset);
  return true;
}

// check if a file exists
static inline bool IfFileExist(std::string filename) {
  DWORD dwAttrib = GetFileAttributes(acptow(filename).c_str());
  return (INVALID_FILE_ATTRIBUTES != dwAttrib &&
          0 == (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

// get preview image from user dir first, then shared_dir
std::string UIStyleSettings::GetColorSchemePreview(
    const std::string& color_scheme_id) {
  if (color_scheme_id.empty())
    return {};
  std::string shared_dir = rime_get_api()->get_shared_data_dir();
  std::string user_dir = rime_get_api()->get_user_data_dir();
  std::string filename =
      user_dir + "\\preview\\color_scheme_" + color_scheme_id + ".png";
  if (IfFileExist(filename))
    return filename;
  else
    return (shared_dir + "\\preview\\color_scheme_" + color_scheme_id + ".png");
}

std::string UIStyleSettings::GetActiveColorScheme() {
  RimeConfig config = {0};
  api_->settings_get_config(settings_, &config);
  const char* value = rime_get_api()->config_get_cstring(
      &config, weasel::ColorSchemeConfigKey(target_));
  if (!value)
    return std::string();
  return std::string(value);
}

bool UIStyleSettings::SelectColorScheme(const std::string& color_scheme_id) {
  return !!api_->customize_string(settings_,
                                  weasel::ColorSchemeConfigKey(target_),
                                  color_scheme_id.c_str());
}
