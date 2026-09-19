#pragma once

namespace weasel_acrylic {

inline constexpr wchar_t kEdgeClipEnvironment[] =
    L"WEASEL_R22_EDGE_CLIP_DIAGNOSTIC";
inline constexpr wchar_t kEdgeClipEnabled[] = L"WeaselAcrylicEdgeClipEnabled";
inline constexpr wchar_t kEdgeClipSample[] = L"WeaselAcrylicEdgeClipSample";
inline constexpr wchar_t kEdgeClipApplied[] = L"WeaselAcrylicEdgeClipApplied";

// Only the measured CI59 foreground/host geometry is eligible. This is not a
// general odd-width border correction for other DPI, styles or layouts.
constexpr bool IsMeasuredEdgeClipSample(int dpi,
                                        int contentWidth,
                                        int contentHeight,
                                        int borderPixels,
                                        unsigned borderAlpha,
                                        int foregroundRadius,
                                        int envelope,
                                        bool light,
                                        bool vertical) noexcept {
  return dpi == 144 && contentWidth == 195 && contentHeight == 266 &&
         borderPixels == 1 && borderAlpha == 255 && foregroundRadius == 16 &&
         envelope == 1 && light && vertical;
}

struct EdgeClipGeometry {
  int x;
  int y;
  int width;
  int height;
  int radius;
};

constexpr EdgeClipGeometry SelectEdgeClipGeometry(bool requested,
                                                  bool sample,
                                                  int dpi,
                                                  int width,
                                                  int height,
                                                  int radius,
                                                  bool light) noexcept {
  if (requested && sample && dpi == 144 && width == 197 && height == 268 &&
      radius == 17 && light)
    return {1, 1, 196, 267, 17};
  return {0, 0, width, height, radius};
}

constexpr bool EdgeClipReadbackMatches(const EdgeClipGeometry& expected,
                                       float x,
                                       float y,
                                       float width,
                                       float height,
                                       float radiusX,
                                       float radiusY) noexcept {
  return x == expected.x && y == expected.y && width == expected.width &&
         height == expected.height && radiusX == expected.radius &&
         radiusY == expected.radius;
}

}  // namespace weasel_acrylic
