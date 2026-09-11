#pragma once

#include <string>
#include <vector>
#include <rime_levers_api.h>
#include <WeaselColorScheme.h>

struct ColorSchemeInfo {
  std::string color_scheme_id;
  std::string name;
  std::string author;
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

  RimeCustomSettings* settings() { return settings_; }

 private:
  RimeLeversApi* api_;
  RimeCustomSettings* settings_;
  weasel::ColorSchemeTarget target_;
};
