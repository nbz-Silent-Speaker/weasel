#pragma once

#include "EdgeClipDiagnostic.h"

namespace weasel_acrylic {

inline constexpr wchar_t kAlignedClipEnabled[] =
    L"WeaselAcrylicAlignedClipEnabled";
inline constexpr wchar_t kAlignedClipWidth[] = L"WeaselAcrylicAlignedClipWidth";
inline constexpr wchar_t kAlignedClipHeight[] =
    L"WeaselAcrylicAlignedClipHeight";
inline constexpr wchar_t kAlignedClipRadius[] =
    L"WeaselAcrylicAlignedClipRadius";

// Shared server leases and packaged hosts retain their existing material route.
// Explicit R22 experiments also keep their original selection semantics.
constexpr bool UseAlignedAcrylicClip(int runtimeRoute,
                                     bool server,
                                     bool diagnostic) noexcept {
  return runtimeRoute == 1 && !server && !diagnostic;
}

// Physical pixels after layout scaling. Native GDI/WUC coverage was checked for
// every radius in this interval, both dimension parities and the minimum sizes.
// Clamped corners, other pen widths and translucent borders retain the full
// clip.
constexpr bool SupportsAlignedAcrylicClip(int contentWidth,
                                          int contentHeight,
                                          int borderPixels,
                                          unsigned borderAlpha,
                                          int foregroundRadius,
                                          int envelope) noexcept {
  return borderPixels == 1 && borderAlpha == 255 && envelope == 1 &&
         foregroundRadius >= 3 && foregroundRadius <= 17 &&
         contentWidth > 2 * foregroundRadius &&
         contentHeight > 2 * foregroundRadius;
}

constexpr EdgeClipGeometry SelectAlignedAcrylicClip(
    int width,
    int height,
    int radius,
    int qualifiedWidth,
    int qualifiedHeight,
    int qualifiedRadius) noexcept {
  if (width == qualifiedWidth && height == qualifiedHeight &&
      radius == qualifiedRadius && radius >= 4 && radius <= 18 &&
      width >= 2 * radius && height >= 2 * radius)
    return {1, 1, width - 1, height - 1, radius};
  return {0, 0, width, height, radius};
}

}  // namespace weasel_acrylic
