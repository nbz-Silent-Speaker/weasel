#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

// Color conversion is independent of HWNDs, preferences and Rime. Future
// candidate-color editors can use the same picker without inheriting UI policy.
namespace settings_color {
struct Color {
  int r = 10, g = 157, b = 161;
  bool operator==(const Color& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
};
enum class Model { Hex, Rgb, Hsv, Hsl, Cmyk };
inline int Byte(double value) {
  return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}
inline Color Hsv(double h, double s, double v) {
  h = std::fmod(h + 360, 360) / 60;
  s /= 100;
  v /= 100;
  const double c = v * s, x = c * (1 - std::abs(std::fmod(h, 2) - 1));
  std::array<double, 3> rgb{};
  switch (static_cast<int>(h)) {
    case 0:
      rgb = {c, x, 0};
      break;
    case 1:
      rgb = {x, c, 0};
      break;
    case 2:
      rgb = {0, c, x};
      break;
    case 3:
      rgb = {0, x, c};
      break;
    case 4:
      rgb = {x, 0, c};
      break;
    default:
      rgb = {c, 0, x};
      break;
  }
  return {Byte(rgb[0] + v - c), Byte(rgb[1] + v - c), Byte(rgb[2] + v - c)};
}
inline std::array<double, 4> Values(Color color, Model model) {
  const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
  const double hi = (std::max)({r, g, b}), lo = (std::min)({r, g, b});
  const double delta = hi - lo, light = (hi + lo) / 2;
  double hue = 0;
  if (delta) {
    hue = hi == r   ? (g - b) / delta
          : hi == g ? (b - r) / delta + 2
                    : (r - g) / delta + 4;
    hue = std::fmod(hue * 60 + 360, 360);
  }
  if (model == Model::Hsv)
    return {hue, hi ? delta / hi * 100 : 0, hi * 100, 0};
  if (model == Model::Hsl)
    return {hue,
            delta ? std::clamp(delta / (1 - std::abs(2 * light - 1)) * 100, 0.0,
                               100.0)
                  : 0,
            light * 100, 0};
  if (model == Model::Cmyk)
    return {hi ? (hi - r) / hi * 100 : 0, hi ? (hi - g) / hi * 100 : 0,
            hi ? (hi - b) / hi * 100 : 0, (1 - hi) * 100};
  return {double(color.r), double(color.g), double(color.b), 0};
}
inline std::optional<Color> FromValues(Model model, std::array<double, 4> v) {
  const int count = model == Model::Cmyk ? 4 : 3;
  for (int i = 0; i < count; ++i) {
    const double limit = model == Model::Rgb                ? 255
                         : (i == 0 && model != Model::Cmyk) ? 360
                                                            : 100;
    if (!std::isfinite(v[i]) || v[i] < 0 || v[i] > limit)
      return std::nullopt;
  }
  if (model == Model::Rgb)
    return Color{int(std::lround(v[0])), int(std::lround(v[1])),
                 int(std::lround(v[2]))};
  if (model == Model::Hsv)
    return Hsv(v[0], v[1], v[2]);
  if (model == Model::Hsl) {
    double light = v[2] / 100, sat = v[1] / 100;
    double value = light + sat * (std::min)(light, 1 - light);
    return Hsv(v[0], value ? 200 * (1 - light / value) : 0, value * 100);
  }
  if (model == Model::Cmyk)
    return Color{Byte((1 - v[0] / 100) * (1 - v[3] / 100)),
                 Byte((1 - v[1] / 100) * (1 - v[3] / 100)),
                 Byte((1 - v[2] / 100) * (1 - v[3] / 100))};
  return std::nullopt;
}
inline std::wstring Hex(Color color) {
  std::wostringstream out;
  out << L'#' << std::uppercase << std::hex << std::setfill(L'0')
      << std::setw(2) << color.r << std::setw(2) << color.g << std::setw(2)
      << color.b;
  return out.str();
}
inline bool DarkText(Color color) {
  const auto linear = [](int channel) {
    double v = channel / 255.0;
    return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
  };
  return .2126 * linear(color.r) + .7152 * linear(color.g) +
             .0722 * linear(color.b) >
         .30;
}
inline std::optional<double> Number(std::wstring text) {
  static const std::wregex pattern(LR"(^\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)\s*$)");
  if (!std::regex_match(text, pattern))
    return std::nullopt;
  try {
    return std::stod(text);
  } catch (...) {
    return std::nullopt;
  }
}
inline std::optional<Color> Parse(std::wstring text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](wchar_t c) { return std::iswspace(c) != 0; }),
             text.end());
  if (text.empty())
    return std::nullopt;
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
  });
  const bool rime = text.rfind(L"0x", 0) == 0;
  std::wstring hex =
      rime ? text.substr(2) : text.substr(text[0] == L'#' ? 1 : 0);
  const bool digits =
      !hex.empty() && std::all_of(hex.begin(), hex.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
      });
  if (digits &&
      (hex.size() == 6 || hex.size() == 8 || (!rime && hex.size() == 3))) {
    if (hex.size() == 3) {
      std::wstring expanded;
      for (auto c : hex) {
        expanded += c;
        expanded += c;
      }
      hex = expanded;
    }
    if (hex.size() == 8) {
      if ((rime ? hex.substr(0, 2) : hex.substr(6)) != L"ff")
        return std::nullopt;  // Settings accents are opaque.
      hex = rime ? hex.substr(2) : hex.substr(0, 6);
    }
    unsigned long value = std::stoul(hex, nullptr, 16);
    return rime ? Color{int(value & 255), int((value >> 8) & 255),
                        int((value >> 16) & 255)}
                : Color{int((value >> 16) & 255), int((value >> 8) & 255),
                        int(value & 255)};
  }
  static const std::wregex function(
      LR"(^(rgb|rgba|hsv|hsl|hsla|cmyk)\((.*)\)$)");
  std::wsmatch match;
  if (!std::regex_match(text, match, function))
    return std::nullopt;
  const auto name = match[1].str();
  const Model model = name.substr(0, 3) == L"rgb"   ? Model::Rgb
                      : name == L"hsv"              ? Model::Hsv
                      : name.substr(0, 3) == L"hsl" ? Model::Hsl
                                                    : Model::Cmyk;
  const int count =
      model == Model::Cmyk || name == L"rgba" || name == L"hsla" ? 4 : 3;
  std::wistringstream fields(match[2].str());
  std::array<double, 4> values{};
  std::wstring field;
  for (int i = 0; i < count; ++i) {
    if (!std::getline(fields, field, L','))
      return std::nullopt;
    bool percent = !field.empty() && field.back() == L'%';
    if (percent)
      field.pop_back();
    const auto value = Number(field);
    if (!value)
      return std::nullopt;
    values[i] = *value;
    if (i == 3 && model != Model::Cmyk) {
      if (*value != (percent ? 100 : 1))
        return std::nullopt;
    } else if (model == Model::Rgb && percent) {
      values[i] *= 2.55;
    } else if (i == 0 && model != Model::Cmyk && percent) {
      return std::nullopt;
    }
  }
  if (std::getline(fields, field, L',') || match[2].str().back() == L',')
    return std::nullopt;
  return FromValues(model, values);
}
}  // namespace settings_color
