#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

namespace weasel {

// Only the settings illustration uses this painter. The live candidate window
// and its composition clipping geometry are unchanged.
struct AppearancePreview {
  bool acrylic = true;
  bool dark = false;
  float radius = 11;
  float highlight_radius = 8;
  float border_width = 1;
  COLORREF background, border, text, label, highlight, highlighted_text,
      highlighted_label, mark;
  std::array<std::wstring, 3> candidates;
};

inline Gdiplus::Color PreviewColor(COLORREF rgb, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}

inline void PreviewRoundRect(Gdiplus::GraphicsPath& path,
                             const Gdiplus::RectF& rect,
                             float radius) {
  const float diameter = (std::min)((std::max)(0.0f, radius * 2),
                                    (std::min)(rect.Width, rect.Height));
  if (diameter <= 0) {
    path.AddRectangle(rect);
    return;
  }
  const float right = rect.GetRight() - diameter;
  const float bottom = rect.GetBottom() - diameter;
  path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
  path.AddArc(right, rect.Y, diameter, diameter, 270, 90);
  path.AddArc(right, bottom, diameter, diameter, 0, 90);
  path.AddArc(rect.X, bottom, diameter, diameter, 90, 90);
  path.CloseFigure();
}

inline void DrawAppearancePreview(HDC dc,
                                  const RECT& bounds,
                                  HFONT font,
                                  const AppearancePreview& style) {
  using namespace Gdiplus;
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  if (width <= 0 || height <= 0)
    return;
  Bitmap buffer(width, height, PixelFormat32bppPARGB);
  Graphics canvas(&buffer);
  canvas.SetSmoothingMode(SmoothingModeAntiAlias);
  canvas.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  // A fixed smooth scene illustrates the backdrop without capturing desktop
  // contents. Normal mode covers it with a fully opaque background.
  const RectF scene(0, 0, static_cast<REAL>(width), static_cast<REAL>(height));
  LinearGradientBrush scenery(
      scene, style.dark ? Color(255, 40, 79, 99) : Color(255, 190, 220, 229),
      style.dark ? Color(255, 87, 59, 83) : Color(255, 232, 207, 220), 35.0f);
  canvas.FillRectangle(&scenery, scene);
  const float scale = height / 192.0f;
  const RectF panel(14 * scale, 14 * scale, width - 28 * scale,
                    height - 28 * scale);
  GraphicsPath outline;
  PreviewRoundRect(outline, panel, style.radius * scale);
  SolidBrush background(
      PreviewColor(style.background, style.acrylic ? 208 : 255));
  canvas.FillPath(&background, &outline);
  const auto contentState = canvas.Save();
  canvas.SetClip(&outline);
  const RectF selected(panel.X + 5 * scale, panel.Y + 34 * scale,
                       panel.Width - 10 * scale, 39 * scale);
  GraphicsPath highlight;
  PreviewRoundRect(highlight, selected, style.highlight_radius * scale);
  SolidBrush hilite(PreviewColor(style.highlight));
  canvas.FillPath(&hilite, &highlight);
  GraphicsPath marker;
  PreviewRoundRect(
      marker,
      RectF(selected.X + scale, selected.Y + 11 * scale, 4 * scale, 17 * scale),
      2 * scale);
  SolidBrush mark(PreviewColor(style.mark));
  canvas.FillPath(&mark, &marker);
  LOGFONTW logical{};
  ::GetObjectW(font, sizeof(logical), &logical);
  logical.lfHeight = -static_cast<LONG>(17 * scale);
  Font candidateFont(dc, &logical);
  logical.lfHeight = -static_cast<LONG>(14 * scale);
  Font labelFont(dc, &logical);
  StringFormat format;
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetFormatFlags(StringFormatFlagsNoWrap);
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  SolidBrush text(PreviewColor(style.text));
  canvas.DrawString(L"ni hao", -1, &labelFont,
                    RectF(panel.X + 14 * scale, panel.Y + 4 * scale,
                          panel.Width - 28 * scale, 28 * scale),
                    &format, &text);
  for (size_t i = 0; i < style.candidates.size(); ++i) {
    const float top = selected.Y + static_cast<float>(i) * 40 * scale;
    SolidBrush number(PreviewColor(i ? style.label : style.highlighted_label));
    SolidBrush word(PreviewColor(i ? style.text : style.highlighted_text));
    const wchar_t* labels[] = {L"1.", L"2.", L"3."};
    canvas.DrawString(labels[i], -1, &labelFont,
                      RectF(panel.X + 15 * scale, top, 26 * scale, 39 * scale),
                      &format, &number);
    canvas.DrawString(
        style.candidates[i].c_str(), -1, &candidateFont,
        RectF(panel.X + 44 * scale, top, panel.Width - 54 * scale, 39 * scale),
        &format, &word);
  }
  canvas.Restore(contentState);
  if (style.border_width > 0) {
    Pen border(PreviewColor(style.border),
               (std::min)(style.border_width, 4.0f) * scale);
    canvas.DrawPath(&border, &outline);
  }
  canvas.Flush(FlushIntentionSync);
  Graphics output(dc);
  // The buffer already matches the control's physical pixels. Specifying its
  // size avoids GDI+ scaling it a second time on a different-DPI display.
  output.DrawImage(&buffer, Rect(static_cast<INT>(bounds.left),
                                 static_cast<INT>(bounds.top), width, height));
}

}  // namespace weasel
