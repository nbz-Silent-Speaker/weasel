#pragma once

#include <rime_api.h>
#include <string>

namespace weasel {

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
      return "style/color_scheme_normal";
    case ColorSchemeTarget::NormalDark:
      return "style/color_scheme_normal_dark";
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
  const char* value =
      api->config_get_cstring(config, ColorSchemeConfigKey(target));
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
