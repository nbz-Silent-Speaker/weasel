#pragma once

namespace weasel_acrylic {

// R22 test-only lease. One packed property is atomic on both x86 and x64:
// (nonzero serial << 2) | 1 = full, | 2 = half. Re-reading a request never
// renews its lease. Full width is the default and every rejection is full.
struct RootClipDiagnosticState {
  unsigned int request = 0;
  // 0 normal, 1 timeout, 2 hidden, 3 resize, 4 invalid.
  unsigned int reason = 0;
  bool half = false;
  unsigned long long deadline = 0;
  int width = 0;
  int height = 0;
  int radius = 0;

  constexpr int SelectWidth(unsigned int next,
                            unsigned long long now,
                            int w,
                            int h,
                            int r,
                            bool visible) {
    if (half && (now >= deadline || !visible || w != width || h != height ||
                 r != radius)) {
      reason = now >= deadline ? 1 : (!visible ? 2 : 3);
      half = false;
    }
    if (next != request) {
      request = next;
      half = false;
      reason = 0;
      const unsigned int mode = next & 3;
      if (next > 0x7fffffffU || (next >> 2) == 0 || (mode != 1 && mode != 2)) {
        reason = 4;
      } else if (mode == 2) {
        // Preserve the radius exactly; never let the half-width geometry
        // silently clamp it. Small windows cannot host the fixed test marker.
        if (!visible || w < 96 || h < 96 || r < 0 || r > w / 4 || r > h / 2) {
          reason = 4;
        } else {
          half = true;
          deadline = now + 5000;
          width = w;
          height = h;
          radius = r;
        }
      }
    }
    return half ? w / 2 : w;
  }
};

}  // namespace weasel_acrylic
