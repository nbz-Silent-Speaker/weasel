#pragma once

namespace weasel_acrylic {

constexpr bool RequestPackagedSystemCompositionFallback(bool inServer,
                                                        unsigned clientKind) {
  return !inServer && clientKind == 0;
}

constexpr bool AllowPackagedSystemCompositionFallback(int runtimeRoute,
                                                      bool searchHost,
                                                      bool requested) {
  return runtimeRoute == 2 && (searchHost || requested);
}

}  // namespace weasel_acrylic
