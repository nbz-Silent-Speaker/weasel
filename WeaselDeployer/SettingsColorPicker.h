#pragma once

#include "SettingsNavigation.h"
#include <windowsx.h>
#include <functional>

namespace settings_color {
class Picker {
 public:
  using Changed = std::function<void(Color)>;
  HWND window() const { return window_; }
  HWND Create(HWND parent,
              HFONT font,
              int x,
              int y,
              int width,
              UINT dpi,
              Changed changed) {
    font_ = font;
    dpi_ = dpi;
    changed_ = std::move(changed);
    WNDCLASSW type{};
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.lpfnWndProc = Proc;
    type.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    type.lpszClassName = L"Weasel.SettingsColorPicker";
    ::RegisterClassW(&type);
    return ::CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y,
                             width, Scale(328), parent, nullptr, type.hInstance,
                             this);
  }
  void Set(Color color, bool editable) {
    editable_ = editable;
    color_ = color;
    UpdateHsv();
    for (int id : {kLabel, kPlane, kHue})
      ::ShowWindow(::GetDlgItem(window_, id), editable ? SW_SHOW : SW_HIDE);
    for (int id : {kCode, kValue, kValue + 1, kValue + 2, kValue + 3})
      ::SendDlgItemMessageW(window_, id, EM_SETREADONLY, !editable, 0);
    Sync();
  }

 private:
  static constexpr int kLabel = 40100, kPlane = 40101, kHue = 40102,
                       kCode = 40103, kModel = 40104, kSwatch = 40105,
                       kError = 40106, kValue = 40110, kValueLabel = 40120;
  HWND window_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;
  bool editable_ = true, syncing_ = false;
  Color color_;
  double hue_ = 0, saturation_ = 0, value_ = 0;
  Model model_ = Model::Hex;
  Changed changed_;
  int Scale(int value) const { return ::MulDiv(value, dpi_, 96); }
  HWND Add(const wchar_t* type,
           const wchar_t* text,
           DWORD style,
           int id,
           int x,
           int y,
           int w,
           int h) {
    return settings_navigation::Create(window_, type, text, style, WORD(id),
                                       Scale(x), Scale(y), Scale(w), Scale(h));
  }
  static std::wstring Text(HWND window) {
    wchar_t text[256]{};
    ::GetWindowTextW(window, text, _countof(text));
    return text;
  }
  void StyleEdit(int id) {
    HWND edit = ::GetDlgItem(window_, id);
    ::SetPropW(edit, L"Weasel.SettingsCustomEdit", reinterpret_cast<HANDLE>(1));
    ::SetWindowTheme(edit, L"", L"");
    ::SetWindowLongPtrW(edit, GWL_STYLE,
                        ::GetWindowLongPtrW(edit, GWL_STYLE) & ~WS_BORDER);
    ::SetWindowSubclass(edit, EditProc, 52, reinterpret_cast<DWORD_PTR>(this));
    ::SetWindowPos(edit, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_FRAMECHANGED);
  }
  void PaintEditFrame(HWND edit) {
    HDC dc = ::GetWindowDC(edit);
    if (!dc)
      return;
    RECT bounds{};
    ::GetWindowRect(edit, &bounds);
    ::OffsetRect(&bounds, -bounds.left, -bounds.top);
    const int saved = ::SaveDC(dc);
    ::ExcludeClipRect(dc, Scale(8), Scale(6), bounds.right - Scale(8),
                      bounds.bottom - Scale(6));
    ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
    {
      Gdiplus::Graphics graphics(dc);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      Gdiplus::GraphicsPath path;
      settings_navigation::AddControlPath(
          path,
          Gdiplus::RectF(.5f, .5f, float(bounds.right - 1),
                         float(bounds.bottom - 1)),
          float(Scale(settings_navigation::kControlCornerRadiusPx)), true,
          true);
      const COLORREF border =
          ::GetFocus() == edit
              ? settings_theme::GetColor(COLOR_HIGHLIGHT)
              : settings_navigation::Mix(
                    settings_theme::GetColor(COLOR_3DSHADOW),
                    settings_theme::GetColor(COLOR_WINDOW), 76);
      Gdiplus::Pen pen(settings_navigation::GdiPlusColor(border), 1.0f);
      graphics.DrawPath(&pen, &path);
    }
    ::RestoreDC(dc, saved);
    ::ReleaseDC(edit, dc);
  }
  static LRESULT CALLBACK EditProc(HWND edit,
                                   UINT message,
                                   WPARAM w,
                                   LPARAM l,
                                   UINT_PTR,
                                   DWORD_PTR data) {
    auto* self = reinterpret_cast<Picker*>(data);
    if (message == WM_NCCALCSIZE) {
      auto* r = w ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0]
                  : reinterpret_cast<RECT*>(l);
      ::InflateRect(r, -self->Scale(8), -self->Scale(6));
      return 0;
    }
    if (message == WM_NCPAINT) {
      self->PaintEditFrame(edit);
      return 0;
    }
    const LRESULT result = ::DefSubclassProc(edit, message, w, l);
    if (message == WM_SIZE) {
      RECT r{};
      ::GetWindowRect(edit, &r);
      HRGN region =
          ::CreateRoundRectRgn(0, 0, r.right - r.left + 1, r.bottom - r.top + 1,
                               self->Scale(10), self->Scale(10));
      if (region && !::SetWindowRgn(edit, region, TRUE))
        ::DeleteObject(region);
    }
    if (message == WM_PAINT || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS)
      self->PaintEditFrame(edit);
    if (message == WM_NCDESTROY) {
      ::RemovePropW(edit, L"Weasel.SettingsCustomEdit");
      ::RemoveWindowSubclass(edit, EditProc, 52);
    }
    return result;
  }
  void Init() {
    ::SendMessageW(window_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    Add(L"STATIC", L"选取颜色", 0, kLabel, 0, 0, 200, 24);
    HWND plane = Add(L"STATIC", L"饱和度与亮度", WS_TABSTOP | SS_NOTIFY, kPlane,
                     0, 28, 380, 156);
    HWND hue =
        Add(L"STATIC", L"色相", WS_TABSTOP | SS_NOTIFY, kHue, 396, 28, 24, 156);
    for (HWND item : {plane, hue})
      ::SetWindowSubclass(item, PlaneProc, 51,
                          reinterpret_cast<DWORD_PTR>(this));
    Add(L"STATIC", L"", SS_OWNERDRAW, kSwatch, 0, 200, 36, 32);
    Add(L"STATIC", L"颜色代码", SS_CENTERIMAGE, 0, 48, 200, 60, 32);
    Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kCode, 112, 200, 148, 32);
    StyleEdit(kCode);
    Add(L"STATIC", L"颜色类型", SS_CENTERIMAGE, 0, 270, 200, 60, 32);
    Add(L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
            CBS_HASSTRINGS,
        kModel, 333, 200, 85, 190);
    for (const auto* name : {L"HEX", L"RGB", L"HSV", L"HSL", L"CMYK"})
      ::SendDlgItemMessageW(window_, kModel, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(name));
    ::SendDlgItemMessageW(window_, kModel, CB_SETCURSEL,
                          static_cast<WPARAM>(model_), 0);
    ::SendDlgItemMessageW(window_, kModel, CB_SETITEMHEIGHT, -1, Scale(26));
    ::SendDlgItemMessageW(window_, kModel, CB_SETITEMHEIGHT, 0, Scale(28));
    settings_navigation::StyleCombo(window_, kModel);
    for (int i = 0; i < 4; ++i) {
      Add(L"STATIC", L"", 0, kValueLabel + i, i * 107, 244, 99, 20);
      Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kValue + i, i * 107, 268,
          99, 30);
      StyleEdit(kValue + i);
      ::SendDlgItemMessageW(window_, kValue + i, EM_SETLIMITTEXT, 32, 0);
    }
    ::SendDlgItemMessageW(window_, kCode, EM_SETLIMITTEXT, 128, 0);
    Add(L"STATIC", L"", 0, kError, 0, 306, 420, 20);
    UpdateHsv();
    Sync();
  }
  void UpdateHsv() {
    const auto hsv = Values(color_, Model::Hsv);
    // Retain hue for black/gray: dragging out of zero saturation is
    // predictable.
    if (hsv[1] > 0)
      hue_ = hsv[0];
    saturation_ = hsv[1];
    value_ = hsv[2];
  }
  void Sync(HWND editing = nullptr) {
    syncing_ = true;
    const auto set = [&](int id, const std::wstring& text) {
      HWND control = ::GetDlgItem(window_, id);
      if (control != editing && Text(control) != text)
        ::SetWindowTextW(control, text.c_str());
    };
    set(kCode, Hex(color_));
    const auto values = Values(color_, model_);
    // The universal code field already covers HEX. Keep the component row
    // empty for HEX and use it only for models with separate components.
    const int count = model_ == Model::Hex ? 0 : model_ == Model::Cmyk ? 4 : 3;
    const wchar_t* names[][4] = {{L"HEX", L"", L"", L""},
                                 {L"R", L"G", L"B", L""},
                                 {L"H °", L"S %", L"V %", L""},
                                 {L"H °", L"S %", L"L %", L""},
                                 {L"C %", L"M %", L"Y %", L"K %"}};
    constexpr int component_gap = 8;
    // Leave a small logical margin so independently rounded DPI coordinates do
    // not push the last field one pixel beyond the panel at fractional scales.
    const int component_width =
        count ? (418 - (count - 1) * component_gap) / count : 99;
    for (int i = 0; i < 4; ++i) {
      HWND field = ::GetDlgItem(window_, kValue + i);
      HWND label = ::GetDlgItem(window_, kValueLabel + i);
      ::ShowWindow(field, i < count ? SW_SHOW : SW_HIDE);
      ::ShowWindow(label, i < count ? SW_SHOW : SW_HIDE);
      const int component_x = i * (component_width + component_gap);
      ::SetWindowPos(label, nullptr, Scale(component_x), Scale(244),
                     Scale(component_width), Scale(20),
                     SWP_NOZORDER | SWP_NOACTIVATE);
      ::SetWindowPos(field, nullptr, Scale(component_x), Scale(268),
                     Scale(component_width), Scale(30),
                     SWP_NOZORDER | SWP_NOACTIVATE);
      set(kValueLabel + i, names[static_cast<int>(model_)][i]);
      std::wostringstream number;
      number << std::fixed << std::setprecision(model_ == Model::Rgb ? 0 : 2)
             << values[i];
      set(kValue + i, number.str());
    }
    set(kError, L"");
    for (int id : {kPlane, kHue, kSwatch})
      ::InvalidateRect(::GetDlgItem(window_, id), nullptr, FALSE);
    syncing_ = false;
  }
  void Edit(HWND field) {
    if (!editable_ || syncing_)
      return;
    std::optional<Color> parsed;
    if (::GetDlgCtrlID(field) == kCode || model_ == Model::Hex)
      parsed = Parse(Text(field));
    else {
      std::array<double, 4> values{};
      const int count = model_ == Model::Cmyk ? 4 : 3;
      for (int i = 0; i < count; ++i) {
        auto value = Number(Text(::GetDlgItem(window_, kValue + i)));
        if (!value) {
          Invalid();
          return;
        }
        values[i] = *value;
      }
      parsed = FromValues(model_, values);
    }
    if (!parsed) {
      Invalid();
      return;
    }
    color_ = *parsed;
    UpdateHsv();
    Sync(field);
    changed_(color_);
  }
  void Invalid() {
    ::SetDlgItemTextW(window_, kError, L"请输入有效的不透明颜色或范围内的数值");
  }
  void Point(HWND control, LPARAM position) {
    if (!editable_)
      return;
    RECT r{};
    ::GetClientRect(control, &r);
    double x = std::clamp(
        double(GET_X_LPARAM(position)) / (std::max)(1L, r.right - 1), 0.0, 1.0);
    double y = std::clamp(
        double(GET_Y_LPARAM(position)) / (std::max)(1L, r.bottom - 1), 0.0,
        1.0);
    if (::GetDlgCtrlID(control) == kHue)
      hue_ = y * 360;
    else {
      saturation_ = x * 100;
      value_ = (1 - y) * 100;
    }
    color_ = Hsv(hue_, saturation_, value_);
    Sync();
    changed_(color_);
  }
  void PaintPlane(HWND control) {
    settings_navigation::PaintBuffered(control, [&](HDC dc, const RECT& r) {
      const bool hue = ::GetDlgCtrlID(control) == kHue;
      constexpr int width = 160, height = 120;
      std::array<DWORD, width * height> pixels{};
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          Color c = hue ? Hsv(y * 360.0 / (height - 1), 100, 100)
                        : Hsv(hue_, x * 100.0 / (width - 1),
                              (height - 1 - y) * 100.0 / (height - 1));
          pixels[y * width + x] = (c.r << 16) | (c.g << 8) | c.b;
        }
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = width;
      info.bmiHeader.biHeight = -height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      ::StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, width, height,
                      pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
      int x = int(saturation_ / 100 * (r.right - 1)),
          y = int((1 - value_ / 100) * (r.bottom - 1));
      if (hue) {
        x = r.right / 2;
        y = int(hue_ / 360 * (r.bottom - 1));
      }
      HGDIOBJ brush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
      for (int border = 0; border < 2; ++border) {
        HPEN pen = ::CreatePen(PS_SOLID, Scale(1),
                               border ? RGB(255, 255, 255) : RGB(0, 0, 0));
        auto old = ::SelectObject(dc, pen);
        int radius = Scale(6 - border);
        if (hue)
          ::Rectangle(dc, 0, y - Scale(3) + border, r.right,
                      y + Scale(3) - border);
        else
          ::Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
        ::SelectObject(dc, old);
        ::DeleteObject(pen);
      }
      ::SelectObject(dc, brush);
      if (::GetFocus() == control) {
        RECT focus = r;
        ::InflateRect(&focus, -2, -2);
        ::DrawFocusRect(dc, &focus);
      }
    });
  }
  static LRESULT CALLBACK PlaneProc(HWND window,
                                    UINT message,
                                    WPARAM w,
                                    LPARAM l,
                                    UINT_PTR,
                                    DWORD_PTR data) {
    auto* self = reinterpret_cast<Picker*>(data);
    switch (message) {
      case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
      case WM_LBUTTONDOWN:
        ::SetFocus(window);
        ::SetCapture(window);
        self->Point(window, l);
        return 0;
      case WM_MOUSEMOVE:
        if (::GetCapture() == window)
          self->Point(window, l);
        return 0;
      case WM_LBUTTONUP:
        if (::GetCapture() == window) {
          self->Point(window, l);
          ::ReleaseCapture();
        }
        return 0;
      case WM_KEYDOWN: {
        if (!self->editable_)
          return 0;
        if (w != VK_LEFT && w != VK_RIGHT && w != VK_UP && w != VK_DOWN)
          break;
        const bool hue = ::GetDlgCtrlID(window) == kHue;
        if (hue)
          self->hue_ = std::clamp(
              self->hue_ + (w == VK_UP || w == VK_LEFT ? -1 : 1), 0.0, 360.0);
        else if (w == VK_LEFT || w == VK_RIGHT)
          self->saturation_ = std::clamp(
              self->saturation_ + (w == VK_LEFT ? -1 : 1), 0.0, 100.0);
        else
          self->value_ =
              std::clamp(self->value_ + (w == VK_DOWN ? -1 : 1), 0.0, 100.0);
        self->color_ = Hsv(self->hue_, self->saturation_, self->value_);
        self->Sync();
        self->changed_(self->color_);
        return 0;
      }
      case WM_SETFOCUS:
      case WM_KILLFOCUS:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT:
        self->PaintPlane(window);
        return 0;
      case WM_NCDESTROY:
        ::RemoveWindowSubclass(window, PlaneProc, 51);
        break;
    }
    return ::DefSubclassProc(window, message, w, l);
  }
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self =
        reinterpret_cast<Picker*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Picker*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      self->window_ = window;
      ::SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(window, message, w, l);
    if (message == WM_CREATE) {
      self->Init();
      return 0;
    }
    if (message == WM_GETFONT)
      return reinterpret_cast<LRESULT>(self->font_);
    if (message == WM_COMMAND) {
      if (LOWORD(w) == kModel && HIWORD(w) == CBN_SELCHANGE) {
        self->model_ = static_cast<Model>(
            ::SendDlgItemMessageW(window, kModel, CB_GETCURSEL, 0, 0));
        self->Sync();
        return 0;
      }
      if (HIWORD(w) == EN_CHANGE) {
        self->Edit(reinterpret_cast<HWND>(l));
        return 0;
      }
    }
    if (message == WM_MEASUREITEM) {
      reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = self->Scale(28);
      return TRUE;
    }
    if (message == WM_DRAWITEM) {
      const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(l);
      if (draw.CtlID == kModel)
        settings_navigation::DrawComboItem(draw);
      else if (draw.CtlID == kSwatch) {
        HBRUSH brush = ::CreateSolidBrush(settings_theme::Ref(self->color_));
        ::FillRect(draw.hDC, &draw.rcItem, brush);
        ::DeleteObject(brush);
      }
      return TRUE;
    }
    if (message == WM_ERASEBKGND) {
      RECT r{};
      ::GetClientRect(window, &r);
      ::FillRect(reinterpret_cast<HDC>(w), &r,
                 settings_theme::GetBrush(COLOR_WINDOW));
      return 1;
    }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC ||
        message == WM_CTLCOLORLISTBOX) {
      HDC dc = reinterpret_cast<HDC>(w);
      ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
      ::SetBkColor(dc, settings_theme::GetColor(COLOR_WINDOW));
      return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
    }
    return ::DefWindowProcW(window, message, w, l);
  }
};
}  // namespace settings_color
