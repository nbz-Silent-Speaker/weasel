#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <WeaselUserSettings.h>

namespace weasel {

// A mode switch changes the editor, while both modes keep their pending colors.
// Only a successful Apply replaces the saved snapshot.
class AppearanceDraft {
 public:
  using Colors = std::array<std::string, 4>;

  void Load(const Colors& colors,
            bool acrylic,
            AppearanceThemeMode theme_mode) {
    colors_ = saved_colors_ = colors;
    acrylic_ = saved_acrylic_ = acrylic;
    theme_mode_ = saved_theme_mode_ = theme_mode;
  }
  const Colors& colors() const { return colors_; }
  bool acrylic() const { return acrylic_; }
  bool saved_acrylic() const { return saved_acrylic_; }
  AppearanceThemeMode theme_mode() const { return theme_mode_; }
  AppearanceThemeMode saved_theme_mode() const { return saved_theme_mode_; }
  void SetAcrylic(bool acrylic) { acrylic_ = acrylic; }
  void SetThemeMode(AppearanceThemeMode mode) { theme_mode_ = mode; }
  std::size_t offset() const { return acrylic_ ? 0 : 2; }
  const std::string& current(bool dark) const {
    return colors_[offset() + (dark ? 1 : 0)];
  }
  void SelectPair(const std::string& light, const std::string& dark) {
    colors_[offset()] = light;
    colors_[offset() + 1] = dark;
  }
  void SelectSingle(bool dark, const std::string& scheme) {
    colors_[offset() + (dark ? 1 : 0)] = scheme;
  }
  void Reset() {
    acrylic_ = true;
    theme_mode_ = AppearanceThemeMode::FollowSystem;
    colors_ = {"base:Fluent_light", "base:Fluent_dark", "base:Fluent_light",
               "base:Fluent_dark"};
  }
  bool changed() const {
    return acrylic_ != saved_acrylic_ || theme_mode_ != saved_theme_mode_ ||
           colors_ != saved_colors_;
  }

 private:
  Colors colors_{};
  Colors saved_colors_{};
  bool acrylic_ = true;
  bool saved_acrylic_ = true;
  AppearanceThemeMode theme_mode_ = AppearanceThemeMode::FollowSystem;
  AppearanceThemeMode saved_theme_mode_ = AppearanceThemeMode::FollowSystem;
};

}  // namespace weasel
