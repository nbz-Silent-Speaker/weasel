#pragma once

#include "AppearancePreview.h"
#include <WeaselUtility.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace weasel {
// Two disposable bitmaps, never configuration. Compare the entire render input,
// not a hash, so a stale or truncated cache cannot pass as the current preview.
class AppearancePreviewCache {
 public:
  static UINT Dpi(HWND window) {
    using GetDpi = UINT(WINAPI*)(HWND);
    const auto get_dpi = reinterpret_cast<GetDpi>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (get_dpi)
      return get_dpi(window);
    HDC dc = ::GetDC(window);
    const UINT dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
      ::ReleaseDC(window, dc);
    return dpi;
  }
  static std::string Key(const AppearancePreview& p,
                         HFONT font,
                         UINT dpi,
                         int width,
                         int height) {
    std::string key;
    const auto add = [&key](const auto& value) {
      key.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    const auto text = [&key, &add](const std::wstring& value) {
      const uint32_t size = static_cast<uint32_t>(value.size());
      add(size);
      key.append(reinterpret_cast<const char*>(value.data()),
                 value.size() * sizeof(wchar_t));
    };
    add(dpi);
    add(width);
    add(height);
    add(p.dark);
    add(p.acrylic);
    add(p.horizontal);
    add(p.radius);
    add(p.highlight_radius);
    add(p.border_width);
    add(p.min_width);
    add(p.max_width);
    add(p.margin_x);
    add(p.margin_y);
    add(p.spacing);
    add(p.candidate_spacing);
    add(p.hilite_spacing);
    add(p.hilite_padding_x);
    add(p.hilite_padding_y);
    add(p.font_point);
    add(p.label_font_point);
    add(p.background);
    add(p.border);
    add(p.text);
    add(p.label);
    add(p.highlight);
    add(p.highlighted_text);
    add(p.highlighted_label);
    add(p.mark);
    text(p.font_face);
    text(p.label_font_face);
    text(p.title);
    for (const auto& candidate : p.candidates)
      text(candidate);
    LOGFONTW logical{};
    ::GetObjectW(font, sizeof(logical), &logical);
    add(logical);
    const COLORREF background = p.page_background;
    add(background);
    // Rebuilds and installed-font changes invalidate saved pixels as well.
    wchar_t executable[32768]{};
    ::GetModuleFileNameW(nullptr, executable, _countof(executable));
    WIN32_FILE_ATTRIBUTE_DATA file{};
    ::GetFileAttributesExW(executable, GetFileExInfoStandard, &file);
    add(file.ftLastWriteTime);
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
      HKEY fonts = nullptr;
      FILETIME changed{};
      if (::RegOpenKeyExW(
              root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
              0, KEY_READ, &fonts) == ERROR_SUCCESS) {
        ::RegQueryInfoKeyW(fonts, nullptr, nullptr, nullptr, nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr, nullptr,
                           &changed);
        ::RegCloseKey(fonts);
      }
      add(changed);
    }
    text(WeaselUserDataPath().wstring());
    return key;
  }

  static HBITMAP Load(size_t index, const std::string& key, int w, int h) {
    if (!ValidSize(w, h))
      return nullptr;
    std::ifstream input(Path(index), std::ios::binary);
    uint32_t header[4]{};
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!input || header[0] != kVersion || header[1] != w || header[2] != h ||
        header[3] != key.size() || key.size() > 65536)
      return nullptr;
    std::string stored(key.size(), '\0');
    input.read(stored.data(), stored.size());
    if (!input || stored != key)
      return nullptr;
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    uint64_t checksum = 0;
    input.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
    std::vector<char> pixels(bytes);
    input.read(pixels.data(), pixels.size());
    if (!input || input.peek() != std::char_traits<char>::eof() ||
        checksum != Checksum(pixels))
      return nullptr;
    auto info = Info(w, h);
    void* bits = nullptr;
    HBITMAP bitmap =
        ::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap)
      std::memcpy(bits, pixels.data(), pixels.size());
    return bitmap;
  }

  static void Save(size_t index,
                   const std::string& key,
                   HBITMAP bitmap,
                   HDC dc,
                   int w,
                   int h) {
    if (!ValidSize(w, h) || key.size() > 65536)
      return;
    auto info = Info(w, h);
    std::vector<char> pixels(static_cast<size_t>(w) * h * 4);
    if (::GetDIBits(dc, bitmap, 0, h, pixels.data(), &info, DIB_RGB_COLORS) !=
        h)
      return;
    const auto path = Path(index);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
      return;
    const auto temporary = path.wstring() + L"." +
                           std::to_wstring(::GetCurrentProcessId()) + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const uint32_t header[] = {kVersion, static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h),
                               static_cast<uint32_t>(key.size())};
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    output.write(key.data(), key.size());
    const uint64_t checksum = Checksum(pixels);
    output.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    output.write(pixels.data(), pixels.size());
    output.close();
    if (!output || !::MoveFileExW(temporary.c_str(), path.c_str(),
                                  MOVEFILE_REPLACE_EXISTING))
      ::DeleteFileW(temporary.c_str());
  }

 private:
  static constexpr uint32_t kVersion = 0x41505002;
  static uint64_t Checksum(const std::vector<char>& pixels) {
    uint64_t value = 14695981039346656037ull;
    for (unsigned char pixel : pixels) {
      value ^= pixel;
      value *= 1099511628211ull;
    }
    return value;
  }
  static bool ValidSize(int w, int h) {
    return w > 0 && h > 0 && w <= 4096 && h <= 4096;
  }
  static BITMAPINFO Info(int w, int h) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
  }
  static std::filesystem::path Path(size_t index) {
    // Preview testing is isolated from the installed program's cache.
    auto root = IsSettingsPreviewMode() ? WeaselUserDataPath() / L"build"
                                        : WeaselLogPath();
    return root / L"appearance-preview" / (index ? L"dark.bin" : L"light.bin");
  }
};
}  // namespace weasel
