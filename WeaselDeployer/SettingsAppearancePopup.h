#pragma once

#include <windowsx.h>
#include "SettingsColorPicker.h"

namespace settings_theme {
class AppearancePopup {
 public:
  HWND window() const { return window_; }
  void Open(HWND owner, HWND anchor) {
    if (::IsWindow(window_)) {
      ::SetForegroundWindow(window_);
      return;
    }
    owner_ = owner;
    using GetWindowDpi = UINT(WINAPI*)(HWND);
    const auto get_dpi = reinterpret_cast<GetWindowDpi>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    dpi_ = get_dpi ? get_dpi(owner) : 96;
    font_ = reinterpret_cast<HFONT>(::SendMessageW(anchor, WM_GETFONT, 0, 0));
    WNDCLASSW type{};
    type.lpfnWndProc = Proc;
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    type.lpszClassName = L"Weasel.SettingsAppearance";
    ::RegisterClassW(&type);
    RECT r{};
    ::GetWindowRect(anchor, &r);
    MONITORINFO monitor{sizeof(monitor)};
    ::GetMonitorInfoW(::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST),
                      &monitor);
    const int width = Scale(460), height = Scale(542);
    const int x = std::clamp(int(r.right + Scale(10)), int(monitor.rcWork.left),
                             (std::max)(int(monitor.rcWork.left),
                                        int(monitor.rcWork.right) - width));
    RECT owner_bounds{};
    ::GetWindowRect(owner, &owner_bounds);
    const int preferred_top =
        (std::max)(int(r.bottom - height), int(owner_bounds.top) + Scale(32));
    const int y = std::clamp(preferred_top, int(monitor.rcWork.top),
                             (std::max)(int(monitor.rcWork.top),
                                        int(monitor.rcWork.bottom) - height));
    window_ = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, type.lpszClassName,
        L"设置界面颜色", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, x, y, width,
        height, owner, nullptr, type.hInstance, this);
    if (window_) {
      settings_navigation::ConfigureComboListWindow(window_);
      Sync(true);
      ::ShowWindow(window_, SW_SHOWNORMAL);
      ::SetFocus(::GetDlgItem(window_, kMode));
    }
  }
  void SystemChanged() {
    if (::IsWindow(window_))
      Sync(true);
  }
  bool Translate(MSG& message) {
    if (!::IsWindow(window_) ||
        (message.hwnd != window_ && !::IsChild(window_, message.hwnd)))
      return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
      // Let a dropped combo consume Escape before closing the popup.
      HWND combo = ::GetDlgItem(picker_.window(), 40104);
      if (!::SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) {
        ::SendMessageW(window_, WM_CLOSE, 0, 0);
        return true;
      }
    }
    return ::IsDialogMessageW(window_, &message) != FALSE;
  }

 private:
  static constexpr int kMode = 40200, kAccent = 40210, kClose = 40220;
  HWND window_ = nullptr, owner_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;
  bool dirty_ = false;
  settings_color::Picker picker_;
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
  void Init() {
    Add(L"STATIC", L"设置界面颜色", SS_CENTERIMAGE, 0, 20, 12, 330, 28);
    Add(L"BUTTON", L"×", BS_PUSHBUTTON | WS_TABSTOP, kClose, 408, 12, 30, 28);
    settings_navigation::StyleActionButton(window_, kClose);
    Add(L"STATIC", L"界面模式", 0, 0, 20, 54, 200, 22);
    Add(L"STATIC", L"强调色", 0, 0, 20, 124, 200, 22);
    const wchar_t* labels[2][3] = {{L"跟随系统", L"始终浅色", L"始终深色"},
                                   {L"跟随 Windows", L"自定义", L"恢复默认"}};
    for (int row = 0; row < 2; ++row)
      for (int col = 0; col < 3; ++col) {
        const int id = (row ? kAccent : kMode) + col;
        Add(L"BUTTON", labels[row][col],
            BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP, id, 20 + col * 140,
            row ? 148 : 78, 140, 30);
        settings_navigation::StyleSegmentedToggle(
            window_, WORD(id),
            col == 0   ? settings_navigation::ToggleState::Segment::Left
            : col == 2 ? settings_navigation::ToggleState::Segment::Right
                       : settings_navigation::ToggleState::Segment::Middle);
      }
    picker_.Create(window_, font_, Scale(20), Scale(192), Scale(420), dpi_,
                   [this](settings_color::Color color) {
                     Current().custom = color;
                     Apply(false);
                   });
  }
  void Sync(bool reset_picker) {
    const auto& p = Current();
    for (int i = 0; i < 3; ++i) {
      ::SendDlgItemMessageW(
          window_, kMode + i, BM_SETCHECK,
          i == static_cast<int>(p.mode) ? BST_CHECKED : BST_UNCHECKED, 0);
      ::SendDlgItemMessageW(
          window_, kAccent + i, BM_SETCHECK,
          i == static_cast<int>(p.accent) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (reset_picker)
      picker_.Set(Color(Colors().accent), p.accent == Accent::Custom);
    ::EnumChildWindows(
        window_,
        [](HWND child, LPARAM) {
          ApplyNative(child);
          return TRUE;
        },
        0);
    ::RedrawWindow(window_, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }
  void Apply(bool reset_picker) {
    if (reset_picker) {
      Update(owner_);
      Sync(true);
    } else {
      // Hue/plane dragging changes only the accent.  Re-theming every native
      // control and erasing every hosted page for each mouse move briefly
      // exposed the page background between child paints.  Keep the live
      // preview, but invalidate the already themed controls without erasing.
      Update(owner_, UpdateKind::AccentOnly);
      ::RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    dirty_ = true;
    ::SetTimer(window_, 1, 250, nullptr);
  }
  bool Persist() {
    ::KillTimer(window_, 1);
    if (!dirty_)
      return true;
    const LSTATUS result = Save();
    if (result != ERROR_SUCCESS) {
      const std::wstring error =
          L"无法保存设置界面颜色（错误 " + std::to_wstring(result) + L"）。";
      ::MessageBoxW(window_, error.c_str(), L"小狼毫设置",
                    MB_OK | MB_ICONERROR);
      return false;
    }
    dirty_ = false;
    return true;
  }
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<AppearancePopup*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<AppearancePopup*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      self->window_ = window;
      ::SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(window, message, w, l);
    switch (message) {
      case WM_CREATE:
        self->Init();
        return 0;
      case WM_GETFONT:
        return reinterpret_cast<LRESULT>(self->font_);
      case WM_COMMAND: {
        const int id = LOWORD(w);
        if (id == kClose) {
          ::SendMessageW(window, WM_CLOSE, 0, 0);
          return 0;
        }
        if (HIWORD(w) != BN_CLICKED)
          break;
        if (id >= kMode && id < kMode + 3)
          Current().mode = static_cast<Mode>(id - kMode);
        else if (id >= kAccent && id < kAccent + 3)
          Current().accent = static_cast<Accent>(id - kAccent);
        else
          break;
        self->Apply(true);
        return 0;
      }
      case WM_TIMER:
        self->Persist();
        return 0;
      case WM_CLOSE:
        if (self->Persist())
          ::DestroyWindow(window);
        return 0;
      case WM_DESTROY:
        self->Persist();
        return 0;
      case WM_NCDESTROY:
        self->window_ = nullptr;
        break;
      case WM_ERASEBKGND: {
        RECT r{};
        ::GetClientRect(window, &r);
        ::FillRect(reinterpret_cast<HDC>(w), &r, GetBrush(COLOR_WINDOW));
        return 1;
      }
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
        ::SetTextColor(reinterpret_cast<HDC>(w), GetColor(COLOR_WINDOWTEXT));
        ::SetBkColor(reinterpret_cast<HDC>(w), GetColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetBrush(COLOR_WINDOW));
    }
    return ::DefWindowProcW(window, message, w, l);
  }
};
inline AppearancePopup& Popup() {
  static AppearancePopup popup;
  return popup;
}
}  // namespace settings_theme
