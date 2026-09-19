#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../AppearancePreviewCache.h"

namespace {
std::filesystem::path profile;
void Require(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
std::vector<char> Pixels(HBITMAP bitmap, HDC dc, int w, int h) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  std::vector<char> pixels(static_cast<size_t>(w) * h * 4);
  Require(
      ::GetDIBits(dc, bitmap, 0, h, pixels.data(), &info, DIB_RGB_COLORS) == h,
      "GetDIBits failed");
  return pixels;
}
void SavePreview(const std::filesystem::path& path,
                 HBITMAP bitmap,
                 HDC dc,
                 int w,
                 int h) {
  const auto pixels = Pixels(bitmap, dc, w, h);
  BITMAPFILEHEADER file{};
  file.bfType = 0x4d42;
  file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
  file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = w;
  info.biHeight = -h;
  info.biPlanes = 1;
  info.biBitCount = 32;
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(&file), sizeof(file));
  output.write(reinterpret_cast<const char*>(&info), sizeof(info));
  output.write(pixels.data(), pixels.size());
}
}  // namespace

std::filesystem::path WeaselUserDataPath() {
  return profile;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2)
    return 2;
  profile = std::filesystem::absolute(argv[1]);
  ::SetEnvironmentVariableW(L"WEASEL_SETTINGS_PREVIEW", L"1");
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR token = 0;
  Require(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok,
          "GDI+ startup failed");
  HDC screen = ::GetDC(nullptr);
  HDC dc = ::CreateCompatibleDC(screen);
  int cases = 0;
  for (UINT dpi : {96u, 144u, 192u}) {
    LOGFONTW logical{};
    logical.lfHeight = -::MulDiv(10, dpi, 72);
    wcscpy_s(logical.lfFaceName, L"Segoe UI");
    HFONT font = ::CreateFontIndirectW(&logical);
    for (bool acrylic : {false, true}) {
      for (bool dark : {false, true}) {
        for (bool horizontal : {false, true}) {
          weasel::AppearancePreview style{};
          style.dpi = dpi;
          style.acrylic = acrylic;
          style.dark = dark;
          style.horizontal = horizontal;
          style.background = dark ? RGB(44, 44, 44) : RGB(249, 249, 249);
          style.border = RGB(100, 100, 100);
          style.text = style.highlighted_text = dark ? RGB(240, 240, 240) : 0;
          style.label = style.highlighted_label = RGB(110, 110, 110);
          style.highlight = RGB(100, 180, 220);
          style.mark = RGB(0, 100, 200);
          style.title = dark ? L"Preview - Dark" : L"Preview - Light";
          style.candidates = {L"你好", L"拟好", L"你", L"泥好", L"你号"};
          const int w = ::MulDiv(360, dpi, 96);
          const int h = ::MulDiv(260, dpi, 96);
          HBITMAP bitmap = ::CreateCompatibleBitmap(screen, w, h);
          const auto old = ::SelectObject(dc, bitmap);
          Require(
              weasel::DrawAppearancePreview(dc, RECT{0, 0, w, h}, font, style),
              "Render failed");
          ::SelectObject(dc, old);
          const auto key =
              weasel::AppearancePreviewCache::Key(style, font, dpi, w, h);
          weasel::AppearancePreviewCache::Save(dark, key, bitmap, dc, w, h);
          SavePreview(profile / (std::to_wstring(dpi) +
                                 (acrylic ? L"-acrylic" : L"-opaque") +
                                 (dark ? L"-dark" : L"-light") +
                                 (horizontal ? L"-horizontal.bmp" : L".bmp")),
                      bitmap, dc, w, h);
          HBITMAP cached =
              weasel::AppearancePreviewCache::Load(dark, key, w, h);
          Require(cached != nullptr, "Valid cache rejected");
          Require(Pixels(bitmap, dc, w, h) == Pixels(cached, dc, w, h),
                  "Cached pixels differ from fresh render");
          ::DeleteObject(cached);
          auto changed = style;
          changed.font_point++;
          const auto font_key =
              weasel::AppearancePreviewCache::Key(changed, font, dpi, w, h);
          Require(!weasel::AppearancePreviewCache::Load(dark, font_key, w, h),
                  "Font change reused stale pixels");
          changed = style;
          changed.background ^= 1;
          Require(
              !weasel::AppearancePreviewCache::Load(
                  dark,
                  weasel::AppearancePreviewCache::Key(changed, font, dpi, w, h),
                  w, h),
              "Color change reused stale pixels");
          Require(!weasel::AppearancePreviewCache::Load(dark, key, w + 1, h),
                  "Size change reused stale pixels");
          changed = style;
          changed.page_background ^= 1;
          Require(weasel::AppearancePreviewCache::Key(changed, font, dpi, w,
                                                      h) != key,
                  "Settings theme change reused stale preview corners");
          Require(!weasel::AppearancePreviewCache::Load(
                      dark,
                      weasel::AppearancePreviewCache::Key(style, font, dpi + 1,
                                                          w, h),
                      w, h),
                  "DPI change reused stale pixels");
          const auto file = profile / L"build" / L"appearance-preview" /
                            (dark ? L"dark.bin" : L"light.bin");
          {
            std::fstream corrupt(
                file, std::ios::binary | std::ios::in | std::ios::out);
            corrupt.seekg(-1, std::ios::end);
            char last{};
            corrupt.read(&last, 1);
            last ^= 1;
            corrupt.seekp(-1, std::ios::end);
            corrupt.write(&last, 1);
          }
          Require(!weasel::AppearancePreviewCache::Load(dark, key, w, h),
                  "Corrupt pixels accepted");
          std::filesystem::resize_file(file, 9);
          Require(!weasel::AppearancePreviewCache::Load(dark, key, w, h),
                  "Truncated cache accepted");
          ::DeleteObject(bitmap);
          ++cases;
        }
      }
    }
    ::DeleteObject(font);
  }
  ::DeleteDC(dc);
  ::ReleaseDC(nullptr, screen);
  Gdiplus::GdiplusShutdown(token);
  std::cout << cases
            << " render/cache cases passed (96/144/192 DPI, "
               "light/dark, opaque/acrylic, vertical/horizontal); stale and "
               "corrupt caches rejected.\n";
  return 0;
}
