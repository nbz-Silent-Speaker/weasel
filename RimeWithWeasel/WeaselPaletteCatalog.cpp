#include <WeaselPaletteCatalog.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>

// These three methods are exported by rime.dll but are not exposed by the C
// API. Declaring only their ABI surface here lets us address literal patch-map
// keys containing '/', without depending on librime's generated private
// build_config.h or exposing private headers to consumers of this module.
namespace rime {
class ConfigItem;
class ConfigMap {
 public:
  RIME_DLL std::shared_ptr<ConfigItem> Get(const std::string& key) const;
};
class Config {
 public:
  RIME_DLL std::shared_ptr<ConfigMap> GetMap(const std::string& path);
  RIME_DLL bool SetItem(const std::string& path,
                        std::shared_ptr<ConfigItem> item);
};
}  // namespace rime

namespace weasel {
namespace {

bool CopyItem(RimeApi* api,
              RimeConfig* from,
              const std::string& path,
              RimeConfig* to,
              const std::string& destination) {
  RimeConfig item{};
  const bool ok = api->config_get_item(from, path.c_str(), &item) &&
                  api->config_set_item(to, destination.c_str(), &item);
  if (item.ptr)
    api->config_close(&item);
  return ok;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

bool HasMap(RimeApi* api, RimeConfig* config, const std::string& path) {
  RimeConfigIterator item{};
  if (!api->config_begin_map(&item, config, path.c_str()))
    return false;
  api->config_end(&item);
  return true;
}

}  // namespace

std::string PaletteId(const std::string& selection) {
  for (const auto* prefix : {"base:", "custom:"}) {
    const std::string tag(prefix);
    if (selection.compare(0, tag.size(), tag) == 0)
      return selection.substr(tag.size());
  }
  return selection;
}

std::string PaletteSource(const std::string& selection) {
  if (selection.compare(0, 5, "base:") == 0)
    return "base";
  if (selection.compare(0, 7, "custom:") == 0)
    return "custom";
  return {};
}

const char* AppearanceColorKey(size_t index) {
  const char* keys[] = {"style/color_scheme_acrylic",
                        "style/color_scheme_acrylic_dark", "style/color_scheme",
                        "style/color_scheme_dark"};
  return index < std::size(keys) ? keys[index] : "";
}

std::string PaletteValue(RimeApi* api,
                         RimeConfig* config,
                         const std::string& path) {
  const auto value = api->config_get_cstring(config, path.c_str());
  return value ? value : "";
}

PaletteCatalog::~PaletteCatalog() {
  Clear();
}

void PaletteCatalog::Clear() {
  if (api_) {
    for (auto* config : {&base_, &custom_, &raw_custom_}) {
      if (config->ptr)
        api_->config_close(config);
      config->ptr = nullptr;
    }
  }
  schemes_.clear();
  groups_.clear();
}

bool PaletteCatalog::LoadFiles(RimeApi* api,
                               const std::filesystem::path& shared,
                               const std::filesystem::path& user) {
  return Load(api, ReadFile(shared / L"weasel.yaml"),
              ReadFile(user / L"weasel.yaml"),
              ReadFile(user / L"weasel.custom.yaml"));
}

bool PaletteCatalog::Load(RimeApi* api,
                          const std::string& shared,
                          const std::string& user,
                          const std::string& custom) {
  Clear();
  if (!api)
    return false;
  api_ = api;
  RimeConfig local{};
  const bool ok =
      api_->config_load_string(&base_,
                               shared.empty() ? "{}" : shared.c_str()) &&
      api_->config_load_string(&local, user.empty() ? "{}" : user.c_str()) &&
      api_->config_load_string(&raw_custom_,
                               custom.empty() ? "{}" : custom.c_str()) &&
      api_->config_init(&custom_);
  if (ok) {
    MergeMap(&local, "preset_color_schemes", &base_, "preset_color_schemes");
    MergeMap(&local, "color_scheme_groups", &base_, "color_scheme_groups");
    if (HasMap(api_, &local, "style"))
      CopyItem(api_, &local, "style", &base_, "style");
    for (const auto* table : {"preset_color_schemes", "color_scheme_groups"}) {
      MergeMap(&raw_custom_, table, &custom_, table);
      ApplyPatches(&custom_, table);
    }
    Build();
  }
  if (local.ptr)
    api_->config_close(&local);
  if (!ok)
    Clear();
  return ok && !schemes_.empty();
}

void PaletteCatalog::ReadStyle(RimeConfig* config) {
  if (HasMap(api_, &base_, "style"))
    CopyItem(api_, &base_, "style", config, "style");
  ApplyPatches(config, "style");
}

RimeConfig* PaletteCatalog::Config(const std::string& selection) {
  if (!Find(selection))
    return nullptr;
  return PaletteSource(selection) == "custom" ? &custom_ : &base_;
}

const PaletteScheme* PaletteCatalog::Find(const std::string& selection) const {
  const auto item = std::find_if(
      schemes_.begin(), schemes_.end(),
      [&](const auto& scheme) { return scheme.color_scheme_id == selection; });
  return item == schemes_.end() ? nullptr : &*item;
}

std::string PaletteCatalog::Resolve(const std::string& id,
                                    const std::string& source) const {
  if (id.empty())
    return {};
  // Keep an explicit source even when that file no longer defines the ID.
  // The settings page can then show the saved value as unavailable without
  // silently binding it to a same-named scheme from the other source.
  if (source == "base" || source == "custom")
    return source + ":" + id;
  if (Find("custom:" + id))
    return "custom:" + id;
  if (Find("base:" + id))
    return "base:" + id;
  return id;
}

std::string PaletteCatalog::ExplicitSelection(RimeConfig* config,
                                              size_t index) const {
  const std::string key = AppearanceColorKey(index);
  const auto selection = PaletteValue(api_, config, key + "_source") + ":" +
                         PaletteValue(api_, config, key);
  return Find(selection) ? selection : std::string();
}

bool PaletteCatalog::ValidId(const std::string& id) {
  return !id.empty() && id.find('/') == std::string::npos &&
         id.front() != '@' && id.compare(0, 2, "__") != 0 && id != "+" &&
         id != "=";
}

void PaletteCatalog::MergeMap(RimeConfig* from,
                              const std::string& path,
                              RimeConfig* to,
                              const std::string& destination) {
  RimeConfigIterator iter{};
  if (!api_->config_begin_map(&iter, from, path.c_str()))
    return;
  while (api_->config_next(&iter)) {
    if (ValidId(iter.key))
      CopyItem(api_, from, iter.path, to, destination + "/" + iter.key);
  }
  api_->config_end(&iter);
}

void PaletteCatalog::ApplyPatches(RimeConfig* target,
                                  const std::string& table) {
  auto* raw = reinterpret_cast<rime::Config*>(raw_custom_.ptr);
  auto* destination = reinterpret_cast<rime::Config*>(target->ptr);
  auto patchMap = raw ? raw->GetMap("patch") : nullptr;
  if (!patchMap || !destination)
    return;
  const auto mergeItem = [&](const auto& item) {
    RimeConfig scratch{};
    if (!item || !api_->config_init(&scratch))
      return;
    auto* config = reinterpret_cast<rime::Config*>(scratch.ptr);
    if (config->SetItem("items", item))
      MergeMap(&scratch, "items", target, table);
    api_->config_close(&scratch);
  };
  // C API paths split on '/', while custom patch keys intentionally contain
  // '/'. Read the exact map value through librime's exported map accessor.
  if (auto item = patchMap->Get(table))
    destination->SetItem(table, item);
  RimeConfigIterator patch{};
  if (!api_->config_begin_map(&patch, &raw_custom_, "patch"))
    return;
  const auto prefix = table + "/";
  while (api_->config_next(&patch)) {
    const std::string key(patch.key);
    if (key.compare(0, prefix.size(), prefix) != 0)
      continue;
    const auto rest = key.substr(prefix.size());
    if (rest == "+") {
      mergeItem(patchMap->Get(key));
      continue;
    }
    if (rest == "=") {
      if (auto item = patchMap->Get(key))
        destination->SetItem(table, item);
      continue;
    }
    if (ValidId(rest.substr(0, rest.find('/')))) {
      if (auto item = patchMap->Get(key))
        destination->SetItem(key, item);
    }
  }
  api_->config_end(&patch);
}

void PaletteCatalog::Build() {
  for (bool custom : {false, true}) {
    auto* config = custom ? &custom_ : &base_;
    const std::string source = custom ? "custom:" : "base:";
    std::map<std::string, size_t> indices;
    RimeConfigIterator iter{};
    if (api_->config_begin_map(&iter, config, "preset_color_schemes")) {
      while (api_->config_next(&iter)) {
        const std::string id(iter.key), path(iter.path);
        if (!ValidId(id) || !HasMap(api_, config, path))
          continue;
        auto name = PaletteValue(api_, config, path + "/name");
        auto variant = PaletteValue(api_, config, path + "/variant");
        indices[id] = schemes_.size();
        schemes_.push_back({source + id, name.empty() ? id : name,
                            PaletteValue(api_, config, path + "/author"),
                            custom, variant});
      }
      api_->config_end(&iter);
    }
    std::map<std::string, unsigned> explicitThemes;
    std::set<std::pair<std::string, std::string>> pairs;
    const auto add = [&](const std::string& name, const std::string& light,
                         const std::string& dark, bool explicitPair) {
      if (light == dark || !indices.count(light) || !indices.count(dark))
        return;
      if (explicitPair) {
        explicitThemes[light] |= 1;
        explicitThemes[dark] |= 2;
      }
      if (pairs.insert({light, dark}).second)
        groups_.push_back({name, source + light, source + dark, custom});
    };
    if (api_->config_begin_map(&iter, config, "color_scheme_groups")) {
      while (api_->config_next(&iter)) {
        const std::string path(iter.path);
        auto name = PaletteValue(api_, config, path + "/name");
        add(name.empty() ? iter.key : name,
            PaletteValue(api_, config, path + "/light"),
            PaletteValue(api_, config, path + "/dark"), true);
      }
      api_->config_end(&iter);
    }
    const auto suffix = [](const std::string& id, const std::string& end) {
      return id.size() > end.size() &&
             id.compare(id.size() - end.size(), end.size(), end) == 0;
    };
    for (const auto& entry : indices) {
      auto& scheme = schemes_[entry.second];
      const auto roles = explicitThemes[entry.first];
      if (roles) {
        scheme.theme = roles == 1   ? PaletteTheme::Light
                       : roles == 2 ? PaletteTheme::Dark
                                    : PaletteTheme::Unspecified;
      } else if (scheme.variant == "light" || scheme.variant == "dark") {
        scheme.theme =
            scheme.variant == "dark" ? PaletteTheme::Dark : PaletteTheme::Light;
      } else if (suffix(entry.first, "_light") ||
                 suffix(entry.first, "_dark")) {
        scheme.theme = suffix(entry.first, "_dark") ? PaletteTheme::Dark
                                                    : PaletteTheme::Light;
      }
    }
    for (const auto& entry : indices) {
      if (!suffix(entry.first, "_light"))
        continue;
      const auto stem = entry.first.substr(0, entry.first.size() - 6);
      const auto night = indices.find(stem + "_dark");
      if (night != indices.end() &&
          schemes_[entry.second].theme == PaletteTheme::Light &&
          schemes_[night->second].theme == PaletteTheme::Dark)
        add(stem == "win11" ? "Win11" : stem, entry.first, night->first, false);
    }
  }
}

}  // namespace weasel
