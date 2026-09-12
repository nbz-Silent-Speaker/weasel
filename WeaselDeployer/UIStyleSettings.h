#pragma once

#include <string>
#include <vector>
#include <array>
#include <set>
#include <rime_levers_api.h>
#include <WeaselColorScheme.h>
#include <WeaselPaletteCatalog.h>

using ColorSchemeInfo = weasel::PaletteScheme;
using ColorSchemeGroup = weasel::PaletteGroup;

class UIStyleSettings {
 public:
  explicit UIStyleSettings(
      weasel::ColorSchemeTarget target = weasel::ColorSchemeTarget::Default);
  ~UIStyleSettings();
  UIStyleSettings(const UIStyleSettings&) = delete;
  UIStyleSettings& operator=(const UIStyleSettings&) = delete;
  weasel::ColorSchemeTarget target() const { return target_; }

  bool GetPresetColorSchemes(std::vector<ColorSchemeInfo>* result);
  std::string GetColorSchemePreview(const std::string& color_scheme_id);
  std::string GetActiveColorScheme();
  bool SelectColorScheme(const std::string& color_scheme_id);
  bool LoadAppearance();
  std::array<std::string, 4> ActiveAppearance();
  bool SaveAppearance(const std::array<std::string, 4>& colors);
  bool configuration_changed() const { return configuration_changed_; }
  const std::vector<ColorSchemeInfo>& schemes() const {
    return palettes_.schemes();
  }
  const std::vector<ColorSchemeGroup>& groups() const {
    return palettes_.groups();
  }
  COLORREF PreviewColor(const std::string& id,
                        const char* key,
                        COLORREF fallback);
  int PreviewLayoutInt(const char* key, int fallback);

  RimeCustomSettings* settings() { return settings_; }

 private:
  RimeLeversApi* api_;
  RimeCustomSettings* settings_;
  weasel::ColorSchemeTarget target_;
  weasel::PaletteCatalog palettes_;
  RimeConfig original_{nullptr};
  RimeConfig custom_{nullptr};
  std::string original_bytes_;
  std::string shared_bytes_;
  std::string base_bytes_;
  bool configuration_changed_ = false;
};
