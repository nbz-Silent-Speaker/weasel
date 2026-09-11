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
  for (auto config : {&catalog_, &original_, &custom_}) {
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
  for (auto config : {&catalog_, &original_, &custom_}) {
    if (config->ptr)
      rime->config_close(config);
    config->ptr = nullptr;
  }
  schemes_.clear();
  groups_.clear();
  custom_ids_.clear();
  const auto path = WeaselUserDataPath() / L"weasel.custom.yaml";
  original_bytes_ = ReadFile(path);
  // Do not silently save over manual edits that have not been deployed yet.
  std::error_code timeError;
  const auto buildPath = WeaselUserDataPath() / L"build" / L"weasel.yaml";
  if (std::filesystem::exists(buildPath, timeError) &&
      !original_bytes_.empty()) {
    const auto customTime = std::filesystem::last_write_time(path, timeError);
    if (timeError)
      return false;
    const auto buildTime =
        std::filesystem::last_write_time(buildPath, timeError);
    if (timeError || customTime > buildTime)
      return false;
  }
  RimeConfig borrowed{};
  if (!rime->config_load_string(&custom_, original_bytes_.c_str()) ||
      !api_->settings_get_config(settings_, &borrowed) ||
      !rime->config_init(&original_) ||
      !CopyItem(&borrowed, "", &original_, "") || !rime->config_init(&catalog_))
    return false;

  // Read the raw base as well as deployed data: a historical whole-map patch
  // may have hidden the base schemes. Opening the dialog never redeploys.
  RimeConfig shared{}, base{};
  const auto sharedText = ReadFile(WeaselSharedDataPath() / L"weasel.yaml");
  auto baseText = ReadFile(WeaselUserDataPath() / L"weasel.yaml");
  if (baseText.empty())
    baseText = sharedText;
  bool ok = rime->config_load_string(&shared, sharedText.c_str()) &&
            rime->config_load_string(&base, baseText.c_str());
  const auto merge = [&](RimeConfig* source, const char* prefix, bool custom) {
    RimeConfigIterator iter{};
    if (!rime->config_begin_map(&iter, source, prefix))
      return;
    while (rime->config_next(&iter)) {
      const std::string id(iter.key);
      if (id.find('/') != std::string::npos)
        continue;
      CopyItem(source, iter.path, &catalog_, "preset_color_schemes/" + id);
      if (custom)
        custom_ids_.insert(id);
    }
    rime->config_end(&iter);
  };
  if (ok) {
    merge(&shared, "preset_color_schemes", false);
    merge(&base, "preset_color_schemes", false);
    merge(&custom_, "patch/preset_color_schemes", true);
    merge(&original_, "preset_color_schemes", false);
    RimeConfigIterator patch{};
    if (rime->config_begin_map(&patch, &custom_, "patch")) {
      while (rime->config_next(&patch)) {
        const std::string key(patch.key), prefix("preset_color_schemes/");
        if (key.compare(0, prefix.size(), prefix) == 0) {
          auto id = key.substr(prefix.size());
          id = id.substr(0, id.find('/'));
          if (id != "+" && id != "=")
            custom_ids_.insert(id);
        }
      }
      rime->config_end(&patch);
    }
    RimeConfigIterator iter{};
    if (rime->config_begin_map(&iter, &catalog_, "preset_color_schemes")) {
      while (rime->config_next(&iter)) {
        const std::string id(iter.key), prefix(iter.path);
        RimeConfigIterator scheme{};
        if (!rime->config_begin_map(&scheme, &catalog_, prefix.c_str()))
          continue;
        rime->config_end(&scheme);
        auto name = Value(&catalog_, prefix + "/name");
        schemes_.push_back({id, name.empty() ? id : name,
                            Value(&catalog_, prefix + "/author"),
                            custom_ids_.count(id) != 0,
                            Value(&catalog_, prefix + "/variant")});
      }
      rime->config_end(&iter);
    }
    // Pairing is explicit metadata, never a suffix or brightness heuristic.
    std::map<std::string, ColorSchemeGroup> pairs;
    for (auto source : {&shared, &base, &original_}) {
      if (!rime->config_begin_map(&iter, source, "color_scheme_groups"))
        continue;
      while (rime->config_next(&iter)) {
        const std::string prefix(iter.path);
        auto name = Value(source, prefix + "/name");
        weasel::ColorSchemePair pair;
        if (weasel::ReadColorSchemePair(rime, source, &catalog_, prefix, &pair))
          pairs[iter.key] = {
              name.empty() ? iter.key : name, pair.light, pair.dark,
              custom_ids_.count(pair.light) || custom_ids_.count(pair.dark)};
        else
          pairs.erase(iter.key);
      }
      rime->config_end(&iter);
    }
    for (const auto& pair : pairs)
      groups_.push_back(pair.second);
  }
  if (shared.ptr)
    rime->config_close(&shared);
  if (base.ptr)
    rime->config_close(&base);
  return ok && !schemes_.empty();
}

std::array<std::string, 4> UIStyleSettings::ActiveAppearance() {
  std::array<std::string, 4> result;
  const char* keys[] = {"style/color_scheme_acrylic",
                        "style/color_scheme_acrylic_dark", "style/color_scheme",
                        "style/color_scheme_dark"};
  for (size_t i = 0; i < result.size(); ++i) {
    result[i] = Value(&original_, keys[i]);
    if (i >= 2) {
      const auto legacy =
          weasel::ModeColorScheme(rime_get_api(), &original_, false, i == 3);
      if (!legacy.empty())
        result[i] = legacy;
    }
  }
  return result;
}

bool UIStyleSettings::SaveAppearance(const std::array<std::string, 4>& colors) {
  auto rime = rime_get_api();
  configuration_changed_ = false;
  const auto path = WeaselUserDataPath() / L"weasel.custom.yaml";
  if (ReadFile(path) != original_bytes_)
    return false;
  RimeConfigIterator wholeMap{};
  const bool replacing =
      rime->config_begin_map(&wholeMap, &custom_, "patch/preset_color_schemes");
  if (replacing)
    rime->config_end(&wholeMap);
  const bool legacy =
      !Value(&original_, "style/color_scheme_normal").empty() ||
      !Value(&original_, "style/color_scheme_normal_dark").empty();
  if (colors == ActiveAppearance() && !replacing && !legacy)
    return true;
  for (const auto& id : colors) {
    if (!id.empty() &&
        !std::any_of(schemes_.begin(), schemes_.end(),
                     [&](const auto& s) { return s.color_scheme_id == id; }))
      return false;
  }
  // Drop the replacing map, retain each custom scheme as an individual patch.
  // librime omits null map entries when saving, so these remove obsolete keys.
  RimeConfigIterator iter{};
  std::set<std::string> existingPatches;
  if (rime->config_begin_map(&iter, &custom_, "patch")) {
    while (rime->config_next(&iter))
      existingPatches.insert(iter.key);
    rime->config_end(&iter);
  }
  if (rime->config_begin_map(&iter, &custom_, "patch/preset_color_schemes")) {
    while (rime->config_next(&iter)) {
      const std::string id(iter.key);
      // Keep unsupported literal paths/directives intact on disk rather than
      // accidentally dropping them during normalization.
      if (id.empty() || id.find('/') != std::string::npos ||
          id.front() == '@' || id.compare(0, 2, "__") == 0) {
        rime->config_end(&iter);
        return false;
      }
      RimeConfig item{};
      const std::string key = std::string("preset_color_schemes/") + iter.key;
      // An existing whole-scheme path already overrides the replacing map;
      // leave that patch intact. Other raw fields and includes are preserved.
      if (existingPatches.count(key))
        continue;
      bool ok = rime->config_get_item(&custom_, iter.path, &item) &&
                api_->customize_item(settings_, key.c_str(), &item);
      if (item.ptr)
        rime->config_close(&item);
      if (!ok) {
        rime->config_end(&iter);
        return false;
      }
    }
    rime->config_end(&iter);
    if (!api_->customize_item(settings_, "preset_color_schemes", nullptr))
      return false;
  }
  // A scheme recovered from the shipped base may not be in an older user base.
  // Materialize only a selected missing scheme so it is usable after
  // deployment.
  for (const auto& id : colors) {
    if (id.empty())
      continue;
    const std::string key = "preset_color_schemes/" + id;
    RimeConfigIterator check{};
    if (rime->config_begin_map(&check, &original_, key.c_str())) {
      rime->config_end(&check);
    } else {
      RimeConfig item{};
      bool ok = rime->config_get_item(&catalog_, key.c_str(), &item) &&
                api_->customize_item(settings_, key.c_str(), &item);
      if (item.ptr)
        rime->config_close(&item);
      if (!ok)
        return false;
    }
  }
  const char* keys[] = {"style/color_scheme_acrylic",
                        "style/color_scheme_acrylic_dark", "style/color_scheme",
                        "style/color_scheme_dark"};
  for (size_t i = 0; i < colors.size(); ++i) {
    const bool ok =
        i >= 2 && colors[i].empty()
            ? api_->customize_item(settings_, keys[i], nullptr)
            : api_->customize_string(settings_, keys[i], colors[i].c_str());
    if (!ok)
      return false;
  }
  if (!api_->customize_item(settings_, "style/color_scheme_normal", nullptr) ||
      !api_->customize_item(settings_, "style/color_scheme_normal_dark",
                            nullptr))
    return false;
  RimeConfig style{};
  rime->config_get_item(&custom_, "patch/style", &style);
  RimeConfigIterator check{};
  if (rime->config_begin_map(&check, &style, "")) {
    rime->config_end(&check);
    rime->config_clear(&style, "color_scheme_normal");
    rime->config_clear(&style, "color_scheme_normal_dark");
    api_->customize_item(settings_, "style", &style);
  }
  if (style.ptr)
    rime->config_close(&style);
  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  const auto backup = path.wstring() + L".before-appearance-" +
                      std::to_wstring(stamp) + L".bak";
  // librime compares dependency timestamps in whole seconds. Two Apply
  // operations in one second would otherwise report success without rebuilding.
  int recorded = 0;
  if (rime->config_get_int(&original_, "__build_info/timestamps/weasel.custom",
                           &recorded) &&
      recorded == std::time(nullptr))
    ::Sleep(1100);
  if (ReadFile(path) != original_bytes_)
    return false;
  if (!::CopyFileW(path.c_str(), backup.c_str(), TRUE))
    return false;
  const bool saved = api_->save_settings(settings_) != False;
  RimeConfig verify{};
  const auto updated = ReadFile(path);
  const bool valid = saved && !updated.empty() && updated != original_bytes_ &&
                     rime->config_load_string(&verify, updated.c_str());
  if (verify.ptr)
    rime->config_close(&verify);
  if (!valid) {
    ::CopyFileW(backup.c_str(), path.c_str(), FALSE);
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
  const std::string prefix = "preset_color_schemes/" + id + "/";
  const auto text = Value(&catalog_, prefix + key);
  if (text.empty())
    return fallback;
  try {
    const auto color = std::stoull(text, nullptr, 0);
    const auto format = Value(&catalog_, prefix + "color_format");
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
