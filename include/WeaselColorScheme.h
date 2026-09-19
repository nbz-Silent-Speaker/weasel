#pragma once

#include <rime_api.h>
#include <string>

namespace weasel {

struct ColorSchemePair {
  std::string light;
  std::string dark;
};

inline bool ReadColorSchemePair(RimeApi* api,
                                RimeConfig* groups,
                                RimeConfig* schemes,
                                const std::string& path,
                                ColorSchemePair* pair) {
  if (!pair)
    return false;
  ColorSchemePair value;
  auto read = [&](const char* side, std::string& id) {
    const char* text =
        api->config_get_cstring(groups, (path + "/" + side).c_str());
    if (!text || !*text)
      return false;
    id = text;
    RimeConfigIterator item{};
    if (!api->config_begin_map(&item, schemes,
                               ("preset_color_schemes/" + id).c_str()))
      return false;
    api->config_end(&item);
    return true;
  };
  if (!read("light", value.light) || !read("dark", value.dark))
    return false;
  *pair = value;
  return true;
}

enum class ColorSchemeTarget {
  Default,
  Acrylic,
  AcrylicDark,
  Normal,
  NormalDark
};

inline const char* ColorSchemeConfigKey(ColorSchemeTarget target) {
  switch (target) {
    case ColorSchemeTarget::Acrylic:
      return "style/color_scheme_acrylic";
    case ColorSchemeTarget::AcrylicDark:
      return "style/color_scheme_acrylic_dark";
    case ColorSchemeTarget::Normal:
      return "style/color_scheme";
    case ColorSchemeTarget::NormalDark:
      return "style/color_scheme_dark";
    default:
      return "style/color_scheme";
  }
}

// Empty or unavailable overrides preserve the already resolved legacy palette.
// Resolve only in weasel, so an explicit global choice is stable across
// schemas.
inline std::string ModeColorScheme(RimeApi* api,
                                   RimeConfig* config,
                                   bool acrylic,
                                   bool dark) {
  const auto target =
      acrylic
          ? (dark ? ColorSchemeTarget::AcrylicDark : ColorSchemeTarget::Acrylic)
          : (dark ? ColorSchemeTarget::NormalDark : ColorSchemeTarget::Normal);
  // Older installations used explicit normal overrides. Read them until the
  // settings page migrates them; new normal choices use the original keys and
  // retain the original schema precedence.
  const char* key = acrylic ? ColorSchemeConfigKey(target)
                            : (dark ? "style/color_scheme_normal_dark"
                                    : "style/color_scheme_normal");
  const char* value = api->config_get_cstring(config, key);
  if (!value || !*value)
    return {};
  const std::string name(value);
  RimeConfigIterator preset = {0};
  if (!api->config_begin_map(&preset, config,
                             ("preset_color_schemes/" + name).c_str()))
    return {};
  api->config_end(&preset);
  return name;
}

}  // namespace weasel
