#pragma once

#include <string>
#include <vector>
#include <array>
#include <set>
#include <rime_levers_api.h>
#include <WeaselColorScheme.h>

struct ColorSchemeInfo {
  std::string color_scheme_id;
  std::string name;
  std::string author;
  bool custom = false;
  std::string variant;
};

struct ColorSchemeGroup {
  std::string name;
  std::string light;
  std::string dark;
  bool custom = false;
};

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
  const std::vector<ColorSchemeInfo>& schemes() const { return schemes_; }
  const std::vector<ColorSchemeGroup>& groups() const { return groups_; }
  COLORREF PreviewColor(const std::string& id,
                        const char* key,
                        COLORREF fallback);
  int PreviewLayoutInt(const char* key, int fallback);

  RimeCustomSettings* settings() { return settings_; }

 private:
  RimeLeversApi* api_;
  RimeCustomSettings* settings_;
  weasel::ColorSchemeTarget target_;
  RimeConfig catalog_{nullptr};
  RimeConfig original_{nullptr};
  RimeConfig custom_{nullptr};
  std::vector<ColorSchemeInfo> schemes_;
  std::vector<ColorSchemeGroup> groups_;
  std::set<std::string> custom_ids_;
  std::string original_bytes_;
  bool configuration_changed_ = false;
};
