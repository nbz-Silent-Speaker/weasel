#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace weasel {
// GDI maps the selected face directly, as in the font settings preview. Using
// an alpha mask keeps GDI+'s existing transform/clip and translucent backdrop,
// without paying GDI+ Font's measured cold-start cost on first paint.
class AppearancePreviewText {
 public:
  explicit AppearancePreviewText(LOGFONTW logical) {
    logical.lfQuality = ANTIALIASED_QUALITY;
    font_ = ::CreateFontIndirectW(&logical);
    dc_ = ::CreateCompatibleDC(nullptr);
    if (font_ && dc_) {
      previous_font_ = ::SelectObject(dc_, font_);
      valid_ = ::GetTextMetricsW(dc_, &metrics_) != FALSE;
    }
  }
  AppearancePreviewText(const AppearancePreviewText&) = delete;
  AppearancePreviewText& operator=(const AppearancePreviewText&) = delete;
  ~AppearancePreviewText() {
    if (previous_font_)
      ::SelectObject(dc_, previous_font_);
    if (dc_)
      ::DeleteDC(dc_);
    if (font_)
      ::DeleteObject(font_);
  }
  bool valid() const { return valid_; }
  float height() const { return static_cast<float>(metrics_.tmHeight); }
  float Measure(const std::wstring& text) const {
    SIZE size{};
    ::GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()),
                            &size);
    return static_cast<float>(size.cx);
  }
  bool Draw(Gdiplus::Graphics& canvas,
            const std::wstring& text,
            const Gdiplus::RectF& bounds,
            COLORREF color,
            bool centered = true) const {
    const int width = static_cast<int>(std::ceil(bounds.Width));
    const int height = static_cast<int>(std::ceil(bounds.Height));
    if (!valid_ || width <= 0 || height <= 0 || width > 8192 || height > 8192)
      return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP mask =
        ::CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!mask)
      return false;
    const HGDIOBJ previous = ::SelectObject(dc_, mask);
    ::PatBlt(dc_, 0, 0, width, height, BLACKNESS);
    ::SetTextColor(dc_, RGB(255, 255, 255));
    ::SetBkColor(dc_, RGB(0, 0, 0));
    ::SetBkMode(dc_, TRANSPARENT);
    RECT rect{0, 0, width, height};
    const bool drawn =
        ::DrawTextW(dc_, text.c_str(), static_cast<int>(text.size()), &rect,
                    DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS |
                        (centered ? DT_VCENTER : DT_TOP)) != 0;
    // Complete GDI's batch before accessing the DIB memory directly.
    ::GdiFlush();
    auto* rgba = static_cast<DWORD*>(pixels);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
      const DWORD alpha = rgba[i] & 255;
      rgba[i] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
                ((GetGValue(color) * alpha / 255) << 8) |
                (GetBValue(color) * alpha / 255);
    }
    bool copied = false;
    {
      Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppPARGB,
                            static_cast<BYTE*>(pixels));
      copied = canvas.DrawImage(&image, bounds, 0, 0, width, height,
                                Gdiplus::UnitPixel) == Gdiplus::Ok;
      canvas.Flush(Gdiplus::FlushIntentionSync);
    }
    ::SelectObject(dc_, previous);
    ::DeleteObject(mask);
    return drawn && copied;
  }

 private:
  HFONT font_ = nullptr;
  HDC dc_ = nullptr;
  HGDIOBJ previous_font_ = nullptr;
  TEXTMETRICW metrics_{};
  bool valid_ = false;
};
}  // namespace weasel
