#include "stdafx.h"
#include <filesystem>
#include <string>
#include <WeaselUtility.h>

namespace {
fs::path EnvironmentPath(const wchar_t* name) {
  const DWORD length = ::GetEnvironmentVariableW(name, nullptr, 0);
  if (!length)
    return {};
  std::wstring value(length, L'\0');
  const DWORD copied = ::GetEnvironmentVariableW(name, value.data(), length);
  if (!copied || copied >= length)
    return {};
  value.resize(copied);
  return fs::path(value);
}

fs::path ConfiguredUserDataPath() {
  WCHAR _path[MAX_PATH] = {0};
  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  LSTATUS ret = RegOpenKey(HKEY_CURRENT_USER, KEY, &hKey);
  if (ret == ERROR_SUCCESS) {
    DWORD len = sizeof(_path);
    DWORD type = 0;
    DWORD data = 0;
    ret =
        RegQueryValueEx(hKey, L"RimeUserDir", NULL, &type, (LPBYTE)_path, &len);
    RegCloseKey(hKey);
    if (ret == ERROR_SUCCESS && type == REG_SZ && _path[0]) {
      return fs::path(_path);
    }
  }
  // default location
  ExpandEnvironmentStringsW(L"%AppData%\\Rime", _path, _countof(_path));
  return fs::path(_path);
}
}  // namespace

fs::path WeaselUserDataPath() {
  const auto preview_path = EnvironmentPath(L"WEASEL_PREVIEW_USER_DIR");
  return preview_path.empty() ? ConfiguredUserDataPath() : preview_path;
}

fs::path WeaselDisplayUserDataPath() {
  const auto display_path = EnvironmentPath(L"WEASEL_PREVIEW_DISPLAY_USER_DIR");
  return display_path.empty() ? ConfiguredUserDataPath() : display_path;
}

fs::path WeaselSharedDataPath() {
  const auto preview_path = EnvironmentPath(L"WEASEL_PREVIEW_SHARED_DIR");
  if (!preview_path.empty())
    return preview_path;
  wchar_t _path[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, _path, _countof(_path));
  return fs::path(_path).remove_filename().append("data");
}

std::string GetCustomResource(const char* name, const char* type) {
  const HINSTANCE module = 0;  // main executable
  HRSRC hRes = FindResourceA(module, name, type);
  if (hRes) {
    HGLOBAL hData = LoadResource(module, hRes);
    if (hData) {
      const char* data = (const char*)::LockResource(hData);
      size_t size = ::SizeofResource(module, hRes);

      if (data && size) {
        if (data[size - 1] == '\0')  // null-terminated string
          size--;
        return std::string(data, size);
      }
    }
  }

  return std::string();
}
