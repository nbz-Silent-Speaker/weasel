#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

namespace weasel {

// Only the settings illustration uses this painter. The live candidate window
// and its composition clipping geometry are unchanged.
struct AppearancePreview {
  bool acrylic = true;
  bool dark = false;
  bool horizontal = false;
  float radius = 11;
  float highlight_radius = 8;
  float border_width = 1;
  int min_width = 130;
  int max_width = 0;
  int margin_x = 11;
  int margin_y = 7;
  int spacing = 5;
  int candidate_spacing = 6;
  int hilite_spacing = 5;
  int hilite_padding_x = 8;
  int hilite_padding_y = 4;
  int font_point = 11;
  int label_font_point = 9;
  std::wstring font_face = L"Microsoft YaHei";
  std::wstring label_font_face = L"Microsoft YaHei";
  std::wstring title;
  COLORREF background, border, text, label, highlight, highlighted_text,
      highlighted_label, mark;
  std::array<std::wstring, 4> candidates;
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
  const float dpi_scale =
      static_cast<float>(::GetDeviceCaps(dc, LOGPIXELSX)) / 96.0f;
  const auto pixels = [dpi_scale](int value) {
    return static_cast<float>(value) * dpi_scale;
  };
  LOGFONTW candidate_logical{};
  ::GetObjectW(font, sizeof(candidate_logical), &candidate_logical);
  candidate_logical.lfHeight = -::MulDiv((std::max)(1, style.font_point),
                                         ::GetDeviceCaps(dc, LOGPIXELSY), 72);
  wcsncpy_s(candidate_logical.lfFaceName, style.font_face.c_str(), _TRUNCATE);
  Font candidate_font(dc, &candidate_logical);
  LOGFONTW label_logical = candidate_logical;
  label_logical.lfHeight = -::MulDiv((std::max)(1, style.label_font_point),
                                     ::GetDeviceCaps(dc, LOGPIXELSY), 72);
  wcsncpy_s(label_logical.lfFaceName, style.label_font_face.c_str(), _TRUNCATE);
  Font label_font(dc, &label_logical);
  const auto measure = [&](const std::wstring& text, const Font& measure_font) {
    RectF measured;
    canvas.MeasureString(text.c_str(), static_cast<INT>(text.size()),
                         &measure_font, PointF(0, 0), &measured);
    return measured.Width;
  };
  const float candidate_height = candidate_font.GetHeight(&canvas);
  const float label_height = label_font.GetHeight(&canvas);
  const float row_height = (std::max)(candidate_height, label_height) +
                           pixels(style.hilite_padding_y) * 2;
  const float margin_x =
      pixels((std::max)(style.margin_x < 0 ? -style.margin_x : style.margin_x,
                        style.hilite_padding_x));
  const float margin_y =
      pixels((std::max)(style.margin_y < 0 ? -style.margin_y : style.margin_y,
                        style.hilite_padding_y));
  const float label_gap = pixels(style.hilite_spacing);
  const float candidate_gap = pixels(style.candidate_spacing);
  const std::array<std::wstring, 4> labels{L"1.", L"2.", L"3.", L"4."};
  std::array<float, 4> entry_widths{};
  float widest_entry = 0;
  for (size_t i = 0; i < style.candidates.size(); ++i) {
    entry_widths[i] = measure(labels[i], label_font) + label_gap +
                      measure(style.candidates[i], candidate_font);
    widest_entry = (std::max)(widest_entry, entry_widths[i]);
  }
  const float preedit_height = candidate_height;
  const float preedit_width = measure(L"ni hao", candidate_font);
  float content_width = widest_entry;
  if (style.horizontal) {
    content_width = 0;
    for (size_t i = 0; i < entry_widths.size(); ++i) {
      content_width += entry_widths[i] + pixels(style.hilite_padding_x) * 2;
      if (i + 1 < entry_widths.size())
        content_width += candidate_gap;
    }
  }
  float panel_width =
      (std::max)(pixels(style.min_width),
                 (std::max)(content_width, preedit_width) + margin_x * 2);
  if (style.max_width > 0)
    panel_width = (std::min)(panel_width, pixels(style.max_width));
  const float rows_height =
      style.horizontal ? row_height
                       : row_height * style.candidates.size() +
                             candidate_gap * (style.candidates.size() - 1);
  const float panel_height =
      margin_y * 2 + preedit_height + pixels(style.spacing) + rows_height;
  const float available_width = static_cast<float>(width) - pixels(16);
  const float available_height = static_cast<float>(height) - pixels(12);
  const float zoom =
      (std::min)(1.0f, (std::min)(available_width / panel_width,
                                  available_height / panel_height));
  const float origin_x = (width - panel_width * zoom) / 2.0f;
  const float origin_y = (height - panel_height * zoom) / 2.0f;
  LOGFONTW title_logical{};
  ::GetObjectW(font, sizeof(title_logical), &title_logical);
  title_logical.lfWeight = FW_SEMIBOLD;
  Font title_font(dc, &title_logical);
  SolidBrush title_brush(
      PreviewColor(style.dark ? RGB(245, 245, 245) : RGB(24, 24, 24)));
  StringFormat title_format;
  title_format.SetFormatFlags(StringFormatFlagsNoWrap);
  title_format.SetTrimming(StringTrimmingEllipsisCharacter);
  canvas.DrawString(style.title.c_str(), -1, &title_font,
                    PointF(pixels(12), origin_y), &title_format, &title_brush);
  const auto preview_state = canvas.Save();
  canvas.TranslateTransform(origin_x, origin_y);
  canvas.ScaleTransform(zoom, zoom);
  const RectF panel(0, 0, panel_width, panel_height);
  GraphicsPath outline;
  PreviewRoundRect(outline, panel, pixels(static_cast<int>(style.radius)));
  SolidBrush background(
      PreviewColor(style.background, style.acrylic ? 190 : 255));
  canvas.FillPath(&background, &outline);
  const auto contentState = canvas.Save();
  canvas.SetClip(&outline);
  StringFormat format;
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetFormatFlags(StringFormatFlagsNoWrap);
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  SolidBrush text(PreviewColor(style.text));
  canvas.DrawString(
      L"ni hao", -1, &candidate_font,
      RectF(margin_x, margin_y, panel_width - margin_x * 2, preedit_height),
      &format, &text);
  const float candidates_top =
      margin_y + preedit_height + pixels(style.spacing);
  float candidate_left = margin_x;
  for (size_t i = 0; i < style.candidates.size(); ++i) {
    const float top = style.horizontal
                          ? candidates_top
                          : candidates_top + static_cast<float>(i) *
                                                 (row_height + candidate_gap);
    const float item_width =
        style.horizontal ? entry_widths[i] + pixels(style.hilite_padding_x) * 2
                         : panel_width - margin_x * 2;
    const RectF item(candidate_left, top, item_width, row_height);
    if (i == 0) {
      GraphicsPath highlight_path;
      PreviewRoundRect(highlight_path, item,
                       pixels(static_cast<int>(style.highlight_radius)));
      SolidBrush hilite(PreviewColor(style.highlight));
      canvas.FillPath(&hilite, &highlight_path);
      const float marker_width = (std::max)(pixels(3), 2.0f);
      GraphicsPath marker_path;
      PreviewRoundRect(
          marker_path,
          RectF(item.X + pixels(2), item.Y + pixels(style.hilite_padding_y),
                marker_width, item.Height - pixels(style.hilite_padding_y) * 2),
          marker_width / 2);
      SolidBrush marker(PreviewColor(style.mark));
      canvas.FillPath(&marker, &marker_path);
    }
    SolidBrush number(PreviewColor(i ? style.label : style.highlighted_label));
    SolidBrush word(PreviewColor(i ? style.text : style.highlighted_text));
    const float text_left = item.X + pixels(style.hilite_padding_x);
    const float label_width = measure(labels[i], label_font);
    canvas.DrawString(labels[i].c_str(), -1, &label_font,
                      RectF(text_left, top, label_width, row_height), &format,
                      &number);
    canvas.DrawString(
        style.candidates[i].c_str(), -1, &candidate_font,
        RectF(text_left + label_width + label_gap, top,
              (std::max)(1.0f, item.Width - pixels(style.hilite_padding_x) * 2 -
                                   label_width - label_gap),
              row_height),
        &format, &word);
    if (style.horizontal)
      candidate_left += item_width + candidate_gap;
  }
  canvas.Restore(contentState);
  if (style.border_width > 0) {
    Pen border(PreviewColor(style.border),
               pixels(static_cast<int>((std::min)(style.border_width, 4.0f))));
    canvas.DrawPath(&border, &outline);
  }
  canvas.Restore(preview_state);
  canvas.Flush(FlushIntentionSync);
  Graphics output(dc);
  // The buffer already matches the control's physical pixels. Specifying its
  // size avoids GDI+ scaling it a second time on a different-DPI display.
  output.DrawImage(&buffer, Rect(static_cast<INT>(bounds.left),
                                 static_cast<INT>(bounds.top), width, height));
}

}  // namespace weasel
