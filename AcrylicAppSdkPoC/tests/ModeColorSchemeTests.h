#pragma once

#include "../../include/WeaselColorScheme.h"
#include <filesystem>

// Load the same architecture of librime that get-rime.ps1 puts in the package.
// These tests parse real YAML through librime; they do not touch user settings.
struct ModeSchemeConfig {
  HMODULE module = nullptr;
  RimeApi* api = nullptr;
  RimeConfig config = {nullptr};

  static std::filesystem::path RepositoryRoot() {
    wchar_t executable[32768] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length == 32768)
      return {};
    auto root = std::filesystem::path(executable);
    for (int i = 0; i < 5; ++i)
      root = root.parent_path();
    return root;
  }

  bool Open() {
    auto root = RepositoryRoot();
    if (root.empty())
      return false;
    auto library = root / L"output";
#ifndef _WIN64
    library /= L"Win32";
#endif
    library /= L"rime.dll";
    module = ::LoadLibraryExW(
        library.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
      return false;
    auto getApi = reinterpret_cast<RimeApi*(__cdecl*)()>(
        ::GetProcAddress(module, "rime_get_api"));
    if (!getApi)
      return false;
    api = getApi();
    return api && RIME_API_AVAILABLE(api, config_load_string);
  }

  ~ModeSchemeConfig() {
    if (api && config.ptr)
      api->config_close(&config);
    if (module)
      ::FreeLibrary(module);
  }
};
