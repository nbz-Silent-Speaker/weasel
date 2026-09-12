#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace weasel {

// A mode switch changes the editor, while both modes keep their pending colors.
// Only a successful Apply replaces the saved snapshot.
class AppearanceDraft {
 public:
  using Colors = std::array<std::string, 4>;

  void Load(const Colors& colors, bool acrylic) {
    colors_ = saved_colors_ = colors;
    acrylic_ = saved_acrylic_ = acrylic;
  }
  const Colors& colors() const { return colors_; }
  bool acrylic() const { return acrylic_; }
  bool saved_acrylic() const { return saved_acrylic_; }
  void SetAcrylic(bool acrylic) { acrylic_ = acrylic; }
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
  void ResetCurrent() { SelectPair("base:Fluent_light", "base:Fluent_dark"); }
  bool changed() const {
    return acrylic_ != saved_acrylic_ || colors_ != saved_colors_;
  }

 private:
  Colors colors_{};
  Colors saved_colors_{};
  bool acrylic_ = true;
  bool saved_acrylic_ = true;
};

}  // namespace weasel
