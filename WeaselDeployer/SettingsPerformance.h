#pragma once

#include <windows.h>
#include <fstream>
#include <WeaselUserSettings.h>

namespace settings_performance {
inline LONGLONG Now() {
  LARGE_INTEGER value{};
  ::QueryPerformanceCounter(&value);
  return value.QuadPart;
}

// Opt-in diagnostics for isolated previews; never write to the real profile.
inline void Record(const char* stage, LONGLONG started) {
  wchar_t path[32768]{};
  if (!weasel::IsSettingsPreviewMode() ||
      !::GetEnvironmentVariableW(L"WEASEL_SETTINGS_TIMING_FILE", path,
                                 _countof(path)))
    return;
  LARGE_INTEGER frequency{};
  ::QueryPerformanceFrequency(&frequency);
  const double elapsed = 1000.0 * (Now() - started) / frequency.QuadPart;
  std::ofstream output(path, std::ios::app);
  output << stage << ": " << elapsed << " ms\n";
}
}  // namespace settings_performance
