#include "../RootClipDiagnosticState.h"

using weasel_acrylic::RootClipDiagnosticState;

constexpr bool DefaultAndExplicitRestore() {
  RootClipDiagnosticState state;
  if (state.SelectWidth(0, 0, 197, 268, 17, true) != 197 || state.half)
    return false;
  if (state.SelectWidth(6, 100, 197, 268, 17, true) != 98)
    return false;
  return state.SelectWidth(9, 200, 197, 268, 17, true) == 197 && !state.half &&
         state.request == 9 && state.reason == 0;
}

constexpr bool ExpiryCannotBeRenewedByPolling() {
  RootClipDiagnosticState state;
  state.SelectWidth(6, 100, 197, 268, 17, true);
  if (state.SelectWidth(6, 5099, 197, 268, 17, true) != 98)
    return false;
  if (state.SelectWidth(6, 5100, 197, 268, 17, true) != 197 ||
      state.reason != 1)
    return false;
  if (state.SelectWidth(6, 9000, 197, 268, 17, true) != 197)
    return false;
  return state.SelectWidth(10, 9001, 197, 268, 17, true) == 98 &&
         state.deadline == 14001;
}

constexpr bool HiddenWindowCannotRearmItself() {
  RootClipDiagnosticState state;
  state.SelectWidth(6, 100, 197, 268, 17, true);
  if (state.SelectWidth(6, 200, 197, 268, 17, false) != 197 ||
      state.reason != 2)
    return false;
  return state.SelectWidth(6, 300, 197, 268, 17, true) == 197;
}

constexpr bool LeaseSurvivesThe32BitTickBoundary() {
  RootClipDiagnosticState state;
  constexpr unsigned long long start = 0xfffffff0ULL;
  state.SelectWidth(6, start, 197, 268, 17, true);
  return state.SelectWidth(6, start + 4999, 197, 268, 17, true) == 98 &&
         state.SelectWidth(6, start + 5000, 197, 268, 17, true) == 197 &&
         state.reason == 1;
}

constexpr bool GeometryChangesRestoreWithoutChangingTheNewSize() {
  for (int dimension = 0; dimension != 3; ++dimension) {
    RootClipDiagnosticState state;
    state.SelectWidth(6, 100, 197, 268, 17, true);
    const int width = dimension == 0 ? 199 : 197;
    const int height = dimension == 1 ? 270 : 268;
    const int radius = dimension == 2 ? 18 : 17;
    if (state.SelectWidth(6, 200, width, height, radius, true) != width ||
        state.reason != 3 || state.half)
      return false;
  }
  return true;
}

constexpr bool InvalidRequestsAlwaysRestore() {
  constexpr unsigned int invalid[] = {0, 1, 2,           3,
                                      4, 7, 0x80000006U, 0xffffffffU};
  for (const auto request : invalid) {
    RootClipDiagnosticState state;
    state.SelectWidth(6, 100, 197, 268, 17, true);
    if (state.SelectWidth(request, 200, 197, 268, 17, true) != 197 ||
        state.half || state.reason != 4)
      return false;
  }
  return true;
}

constexpr bool RadiusAndMarkerRoomAreNeverSilentlyClamped() {
  constexpr int cases[][3] = {{95, 268, 17},
                              {197, 95, 17},
                              {197, 268, 50},
                              {400, 100, 51},
                              {197, 268, -1}};
  for (const auto& dimensions : cases) {
    RootClipDiagnosticState state;
    if (state.SelectWidth(6, 100, dimensions[0], dimensions[1], dimensions[2],
                          true) != dimensions[0] ||
        state.half || state.reason != 4)
      return false;
  }
  return true;
}

static_assert(DefaultAndExplicitRestore());
static_assert(ExpiryCannotBeRenewedByPolling());
static_assert(HiddenWindowCannotRearmItself());
static_assert(LeaseSurvivesThe32BitTickBoundary());
static_assert(GeometryChangesRestoreWithoutChangingTheNewSize());
static_assert(InvalidRequestsAlwaysRestore());
static_assert(RadiusAndMarkerRoomAreNeverSilentlyClamped());
