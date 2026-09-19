#pragma once

#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include "SettingsTheme.h"

namespace settings_navigation {

enum class Page { Input, Appearance, Fonts, StatusIcons };
inline constexpr size_t kPageCount = 4;
inline thread_local std::array<bool, kPageCount> unapplied_pages{};
inline thread_local unsigned hosted_page_creation_depth = 0;
inline constexpr WORD kInput = 30001;
inline constexpr WORD kAppearance = 30002;
inline constexpr WORD kFonts = 30003;
inline constexpr WORD kStatusIcons = 30004;
inline constexpr WORD kUserFolder = 30005;
inline constexpr UINT kHostNavigateMessage = WM_APP + 0x531;
inline constexpr UINT kHostCloseMessage = WM_APP + 0x532;
inline constexpr UINT kHostApplyMessage = WM_APP + 0x533;
inline constexpr UINT kHostStateChangedMessage = WM_APP + 0x534;
inline constexpr wchar_t kHostProperty[] = L"Weasel.SettingsHost";
inline constexpr wchar_t kComboAnimationProperty[] =
    L"Weasel.ComboAnimationSuppressed";

class HostedPageCreationScope {
 public:
  HostedPageCreationScope() { ++hosted_page_creation_depth; }
  HostedPageCreationScope(const HostedPageCreationScope&) = delete;
  HostedPageCreationScope& operator=(const HostedPageCreationScope&) = delete;
  ~HostedPageCreationScope() { --hosted_page_creation_depth; }
};

inline BOOL HostedPageInitResult() {
  // Returning TRUE from WM_INITDIALOG asks the dialog manager to set focus.
  // A modeless page is still a top-level window at this point, so that focus
  // operation can activate and expose it before the shared host exists.
  return hosted_page_creation_depth ? FALSE : TRUE;
}

#pragma pack(push, 2)
struct DialogTemplateExHeader {
  WORD version;
  WORD signature;
  DWORD help_id;
  DWORD extended_style;
  DWORD style;
  WORD item_count;
  short x;
  short y;
  short width;
  short height;
};
#pragma pack(pop)

template <class T>
class HostedDialogImpl : public ATL::CDialogImpl<T> {
 public:
  using ATL::CDialogImpl<T>::Create;

  HWND CreateHosted(HWND parent, LPARAM init_parameter = 0) {
    ATLASSERT(parent && ::IsWindow(parent));
    ATLASSERT(this->m_hWnd == nullptr);

    const HINSTANCE instance = _AtlBaseModule.GetResourceInstance();
    const HRSRC resource =
        ::FindResourceW(instance, MAKEINTRESOURCEW(T::IDD), RT_DIALOG);
    const DWORD size = resource ? ::SizeofResource(instance, resource) : 0;
    const HGLOBAL loaded = resource ? ::LoadResource(instance, resource) : 0;
    const void* source = loaded ? ::LockResource(loaded) : nullptr;
    if (!source || size < sizeof(DLGTEMPLATE))
      return nullptr;

    std::vector<unsigned char> template_data(size);
    std::memcpy(template_data.data(), source, size);
    DWORD* style = nullptr;
    DWORD* extended_style = nullptr;
    const auto* words = reinterpret_cast<const WORD*>(template_data.data());
    if (size >= sizeof(DialogTemplateExHeader) && words[1] == 0xffff) {
      auto* header =
          reinterpret_cast<DialogTemplateExHeader*>(template_data.data());
      style = &header->style;
      extended_style = &header->extended_style;
    } else {
      auto* header = reinterpret_cast<DLGTEMPLATE*>(template_data.data());
      style = &header->style;
      extended_style = &header->dwExtendedStyle;
    }
    *style &=
        ~(static_cast<DWORD>(WS_VISIBLE) | WS_POPUP | WS_CAPTION | WS_SYSMENU |
          WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | DS_MODALFRAME);
    *style |= WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | DS_CONTROL;
    *extended_style &=
        ~(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    *extended_style |= WS_EX_CONTROLPARENT;

    if (!this->m_thunk.Init(nullptr, nullptr)) {
      ::SetLastError(ERROR_OUTOFMEMORY);
      return nullptr;
    }
    _AtlWinModule.AddCreateWndData(
        &this->m_thunk.cd,
        static_cast<ATL::CDialogImplBaseT<ATL::CWindow>*>(this));
#ifdef _DEBUG
    this->m_bModal = false;
#endif
    const HWND window = ::CreateDialogIndirectParamW(
        instance, reinterpret_cast<const DLGTEMPLATE*>(template_data.data()),
        parent, T::StartDialogProc, init_parameter);
    ATLASSERT(this->m_hWnd == window);
    return window;
  }
};

// Every settings page is hosted in the same logical content frame.  Keep
// these values as the single source of truth so page changes cannot resize the
// outer window.
inline constexpr int kContentWidthDlu = 540;
inline constexpr int kContentHeightDlu = 286;
inline constexpr int kSidebarWidthDlu = 116;
inline constexpr int kPageInsetDlu = 14;
inline constexpr int kPageBodyWidthDlu = 512;
inline constexpr int kBottomActionLeftDlu = 14;
inline constexpr int kBottomActionTopDlu = 258;
inline constexpr int kFirstCardTopDlu = 14;
inline constexpr int kPageCardsBottomDlu = 248;
inline constexpr int kCardGapDlu = 6;
inline constexpr int kActionButtonWidthDlu = 80;
inline constexpr int kSecondaryButtonWidthDlu = 68;
inline constexpr int kTransientButtonWidthDlu = 60;
inline constexpr int kToggleGapDlu = 0;
inline constexpr int kButtonHeightDlu = 18;
inline constexpr int kComboWidthDlu = 80;
inline constexpr int kComboItemHeightDlu = 14;
inline constexpr int kComboVisibleItemLimit = 5;
inline constexpr int kCardRadiusDlu = 8;
inline constexpr int kControlCornerRadiusPx = 5;
inline constexpr int kSingleRowCardHeightDlu = 32;
inline constexpr int kCompactToggleHeightDlu = 14;
inline constexpr int kNavigationItemHeightDlu = 16;
inline constexpr int kNavigationItemGapDlu = 2;
inline constexpr int kSidebarSectionGapDlu = 6;

struct InstallOptions {
  WORD apply = 0;
  WORD close = IDCANCEL;
  std::wstring user_folder;
  std::vector<WORD> hide;
};

inline std::wstring LocalText(const wchar_t* simplified,
                              const wchar_t* traditional,
                              const wchar_t* english) {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE)
    return english;
  const WORD sublanguage = SUBLANGID(language);
  return sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
                 sublanguage == SUBLANG_CHINESE_SINGAPORE
             ? simplified
             : traditional;
}

inline Page PageFromCommand(WORD command) {
  if (command == kAppearance)
    return Page::Appearance;
  if (command == kFonts)
    return Page::Fonts;
  if (command == kStatusIcons)
    return Page::StatusIcons;
  return Page::Input;
}

inline size_t PageIndex(Page page) {
  return static_cast<size_t>(page);
}

inline bool HasUnappliedChanges(Page page) {
  return unapplied_pages[PageIndex(page)];
}

inline bool HasAnyUnappliedChanges() {
  return std::any_of(unapplied_pages.begin(), unapplied_pages.end(),
                     [](bool unapplied) { return unapplied; });
}

inline void NotifyHostStateChanged(HWND dialog) {
  if (!::GetPropW(dialog, kHostProperty))
    return;
  ::PostThreadMessageW(::GetCurrentThreadId(), kHostStateChangedMessage, 0,
                       reinterpret_cast<LPARAM>(dialog));
}

inline void SetUnappliedChanges(HWND dialog, Page page, bool unapplied) {
  if (unapplied_pages[PageIndex(page)] == unapplied)
    return;
  unapplied_pages[PageIndex(page)] = unapplied;
  for (WORD id = kInput; id <= kStatusIcons; ++id) {
    if (HWND item = ::GetDlgItem(dialog, id))
      ::InvalidateRect(item, nullptr, FALSE);
  }
  NotifyHostStateChanged(dialog);
}

inline void ClearUnappliedChanges() {
  unapplied_pages.fill(false);
}

inline bool IsPageResult(INT_PTR result) {
  return result >= kInput && result <= kStatusIcons;
}

inline void AttachHost(HWND dialog) {
  ::SetPropW(dialog, kHostProperty, reinterpret_cast<HANDLE>(1));
}

inline void DetachHost(HWND dialog) {
  ::RemovePropW(dialog, kHostProperty);
}

inline bool RequestNavigate(HWND dialog, WORD command) {
  if (!::GetPropW(dialog, kHostProperty))
    return false;
  return ::PostThreadMessageW(::GetCurrentThreadId(), kHostNavigateMessage,
                              command,
                              reinterpret_cast<LPARAM>(dialog)) != FALSE;
}

inline bool RequestClose(HWND dialog, INT_PTR result) {
  if (!::GetPropW(dialog, kHostProperty))
    return false;
  return ::PostThreadMessageW(::GetCurrentThreadId(), kHostCloseMessage,
                              static_cast<WPARAM>(result),
                              reinterpret_cast<LPARAM>(dialog)) != FALSE;
}

inline bool RequestApply(HWND dialog) {
  if (!::GetPropW(dialog, kHostProperty))
    return false;
  return ::PostThreadMessageW(::GetCurrentThreadId(), kHostApplyMessage, 0,
                              reinterpret_cast<LPARAM>(dialog)) != FALSE;
}

inline void DisableWindowTransitions(HWND dialog) {
  using SetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  static const SetWindowAttribute set_window_attribute = [] {
    HMODULE module = ::LoadLibraryW(L"dwmapi.dll");
    return module ? reinterpret_cast<SetWindowAttribute>(
                        ::GetProcAddress(module, "DwmSetWindowAttribute"))
                  : nullptr;
  }();
  if (!set_window_attribute)
    return;
  constexpr DWORD kTransitionsForcedDisabled = 3;
  const BOOL disabled = TRUE;
  set_window_attribute(dialog, kTransitionsForcedDisabled, &disabled,
                       sizeof(disabled));
}

inline void ConfigureComboListWindow(HWND window) {
  if (!window)
    return;

  // Configure the native drop-down while it is still hidden.  Reapplying DWM
  // attributes from WM_SHOWWINDOW/WM_WINDOWPOSCHANGING makes Windows compose
  // the empty popup frame first and its owner-drawn contents afterwards.
  DisableWindowTransitions(window);

  using SetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  static const SetWindowAttribute set_window_attribute = [] {
    HMODULE module = ::LoadLibraryW(L"dwmapi.dll");
    return module ? reinterpret_cast<SetWindowAttribute>(
                        ::GetProcAddress(module, "DwmSetWindowAttribute"))
                  : nullptr;
  }();
  if (!set_window_attribute)
    return;

  constexpr DWORD kWindowCornerPreference = 33;
  constexpr int kRoundCorners = 2;
  set_window_attribute(window, kWindowCornerPreference, &kRoundCorners,
                       sizeof(kRoundCorners));
}

inline void RestoreComboAnimation(HWND list) {
  if (!list || !::GetPropW(list, kComboAnimationProperty))
    return;
  ::RemovePropW(list, kComboAnimationProperty);
  ::SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, TRUE, nullptr, 0);
}

inline RECT MapDialogUnits(HWND dialog,
                           int left,
                           int top,
                           int width,
                           int height) {
  RECT value{left, top, left + width, top + height};
  ::MapDialogRect(dialog, &value);
  return value;
}

inline void MoveControl(HWND dialog,
                        WORD id,
                        int left,
                        int top,
                        int width,
                        int height) {
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  const RECT bounds = MapDialogUnits(dialog, left, top, width, height);
  ::SetWindowPos(control, nullptr, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

inline void ResizeContentFrame(HWND dialog) {
  RECT client{};
  RECT window{};
  ::GetClientRect(dialog, &client);
  ::GetWindowRect(dialog, &window);
  const RECT content =
      MapDialogUnits(dialog, 0, 0, kContentWidthDlu, kContentHeightDlu);
  const int frame_width =
      (window.right - window.left) - (client.right - client.left);
  const int frame_height =
      (window.bottom - window.top) - (client.bottom - client.top);
  ::SetWindowPos(dialog, nullptr, 0, 0, content.right + frame_width,
                 content.bottom + frame_height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Every settings page consists of the fixed content resource plus the shared
// navigation sidebar.  Loading and fully initialized pages must use this same
// outer size so replacing one with the other never clips or resizes the host.
inline void ResizeForSidebarFrame(HWND dialog) {
  ResizeContentFrame(dialog);
  const int sidebar_width =
      MapDialogUnits(dialog, 0, 0, kSidebarWidthDlu, 0).right;
  RECT window{};
  if (::GetWindowRect(dialog, &window)) {
    ::SetWindowPos(
        dialog, nullptr, 0, 0, window.right - window.left + sidebar_width,
        window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

inline COLORREF Mix(COLORREF foreground, COLORREF background, int alpha) {
  const auto mix = [alpha](int first, int second) {
    return (first * alpha + second * (255 - alpha)) / 255;
  };
  return RGB(mix(GetRValue(foreground), GetRValue(background)),
             mix(GetGValue(foreground), GetGValue(background)),
             mix(GetBValue(foreground), GetBValue(background)));
}

inline COLORREF SidebarSurface() {
  return Mix(settings_theme::GetColor(COLOR_HIGHLIGHT),
             settings_theme::GetColor(COLOR_BTNFACE), 7);
}

struct SidebarState {
  int width = 0;
  HBRUSH brush = nullptr;
};

inline LRESULT CALLBACK SidebarProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam,
                                    UINT_PTR,
                                    DWORD_PTR data) {
  auto* state = reinterpret_cast<SidebarState*>(data);
  if (message == WM_NOTIFY && lparam) {
    const auto* notification = reinterpret_cast<NMHDR*>(lparam);
    wchar_t class_name[32]{};
    ::GetClassNameW(notification->hwndFrom, class_name, _countof(class_name));
    if (notification->code == NM_CUSTOMDRAW &&
        _wcsicmp(class_name, L"SysLink") == 0) {
      auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lparam);
      if (draw->dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYITEMDRAW;
      if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
        ::SetTextColor(draw->hdc, settings_theme::GetColor(COLOR_HOTLIGHT));
        ::SetBkColor(draw->hdc, settings_theme::GetColor(COLOR_WINDOW));
        return CDRF_NEWFONT;
      }
    }
  }
  if (message == WM_COMMAND && LOWORD(wparam) == settings_theme::kNavigation) {
    ::SendMessageW(::GetAncestor(window, GA_ROOT), settings_theme::kOpen, 0,
                   lparam);
    return 0;
  }
  if (message == WM_CTLCOLORDLG)
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_BTNFACE));
  if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
    const auto dc = reinterpret_cast<HDC>(wparam);
    ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
    ::SetBkColor(dc, settings_theme::GetColor(COLOR_WINDOW));
  }
  if (message == WM_ERASEBKGND) {
    HDC dc = reinterpret_cast<HDC>(wparam);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    RECT sidebar = bounds;
    sidebar.right = (std::min)(sidebar.right, static_cast<LONG>(state->width));
    ::FillRect(dc, &sidebar, state->brush);
    bounds.left = sidebar.right;
    ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_BTNFACE));
    return 1;
  }
  if (message == WM_CTLCOLORSTATIC) {
    HWND control = reinterpret_cast<HWND>(lparam);
    RECT bounds{};
    ::GetWindowRect(control, &bounds);
    ::MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&bounds),
                      2);
    if (bounds.left < state->width) {
      HDC dc = reinterpret_cast<HDC>(wparam);
      ::SetBkColor(dc, SidebarSurface());
      return reinterpret_cast<LRESULT>(state->brush);
    }
  }
  if (message == WM_SYSCOLORCHANGE || message == settings_theme::kChanged) {
    const bool accent_only = message == settings_theme::kChanged &&
                             wparam == settings_theme::kAccentOnly;
    HBRUSH brush = ::CreateSolidBrush(SidebarSurface());
    if (brush) {
      ::DeleteObject(state->brush);
      state->brush = brush;
    }
    ::RedrawWindow(window, nullptr, nullptr,
                   settings_theme::RedrawFlags(
                       accent_only ? settings_theme::UpdateKind::AccentOnly
                                   : settings_theme::UpdateKind::Full));
  }
  if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, SidebarProc, 4);
    ::DeleteObject(state->brush);
    delete state;
  }
  const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
  if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
    for (int color : {COLOR_BTNFACE, COLOR_WINDOW}) {
      if (result == reinterpret_cast<LRESULT>(::GetSysColorBrush(color))) {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
        ::SetBkColor(dc, settings_theme::GetColor(color));
        return reinterpret_cast<LRESULT>(settings_theme::GetBrush(color));
      }
    }
  }
  return result;
}

struct NavState {
  bool active;
  bool hover;
  bool link;
  Page page;
};

inline LRESULT CALLBACK SidebarSeparatorProc(HWND window,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam,
                                             UINT_PTR,
                                             DWORD_PTR) {
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(window, &paint);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    const COLORREF surface = SidebarSurface();
    const COLORREF divider =
        Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 42);
    HBRUSH surface_brush = ::CreateSolidBrush(surface);
    HBRUSH divider_brush = ::CreateSolidBrush(divider);
    ::FillRect(dc, &bounds, surface_brush);
    const int center = (bounds.top + bounds.bottom) / 2;
    RECT line =
        bounds.bottom > bounds.right
            ? RECT{bounds.left, bounds.top, bounds.left + 1, bounds.bottom}
            : RECT{bounds.left, center, bounds.right, center + 1};
    ::FillRect(dc, &line, divider_brush);
    ::DeleteObject(divider_brush);
    ::DeleteObject(surface_brush);
    ::EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(window, SidebarSeparatorProc, 8);
  return ::DefSubclassProc(window, message, wparam, lparam);
}

struct ToggleState {
  enum class Segment { None, Left, Middle, Right };
  enum class Background { Window, ButtonFace, Sidebar };

  bool hover = false;
  bool neutral = false;
  Segment segment = Segment::None;
  Background background = Background::Window;
};

struct CheckboxState {
  bool hover = false;
  ToggleState::Background background = ToggleState::Background::Window;
};

struct SwitchState {
  bool hover = false;
};

struct ComboState {
  bool hover = false;
};

inline bool EnsureGdiPlus() {
  struct Runtime {
    Runtime() {
      Gdiplus::GdiplusStartupInput startup;
      if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok)
        token = 0;
    }
    ~Runtime() {
      if (token)
        Gdiplus::GdiplusShutdown(token);
    }
    ULONG_PTR token = 0;
  };
  static Runtime runtime;
  return runtime.token != 0;
}

inline Gdiplus::Color GdiPlusColor(COLORREF color, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color),
                        GetBValue(color));
}

inline COLORREF ControlBackground(ToggleState::Background background) {
  switch (background) {
    case ToggleState::Background::ButtonFace:
      return settings_theme::GetColor(COLOR_BTNFACE);
    case ToggleState::Background::Sidebar:
      return SidebarSurface();
    default:
      return settings_theme::GetColor(COLOR_WINDOW);
  }
}

inline HBRUSH CreateControlBackgroundBrush(ToggleState::Background background) {
  return ::CreateSolidBrush(ControlBackground(background));
}

inline void AddControlPath(Gdiplus::GraphicsPath& path,
                           const Gdiplus::RectF& bounds,
                           Gdiplus::REAL radius,
                           bool round_left = true,
                           bool round_right = true) {
  const Gdiplus::REAL diameter =
      (std::min)(radius * 2.0f, (std::min)(bounds.Width, bounds.Height));
  if (diameter <= 0.0f) {
    path.AddRectangle(bounds);
    return;
  }
  const Gdiplus::REAL right = bounds.GetRight();
  const Gdiplus::REAL bottom = bounds.GetBottom();
  path.StartFigure();
  if (round_left && round_right) {
    path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddLine(bounds.X + radius, bounds.Y, right - radius, bounds.Y);
    path.AddArc(right - diameter, bounds.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddLine(right, bounds.Y + radius, right, bottom - radius);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddLine(right - radius, bottom, bounds.X + radius, bottom);
    path.AddArc(bounds.X, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.AddLine(bounds.X, bottom - radius, bounds.X, bounds.Y + radius);
  } else if (round_left) {
    path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddLine(bounds.X + radius, bounds.Y, right, bounds.Y);
    path.AddLine(right, bounds.Y, right, bottom);
    path.AddLine(right, bottom, bounds.X + radius, bottom);
    path.AddArc(bounds.X, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.AddLine(bounds.X, bottom - radius, bounds.X, bounds.Y + radius);
  } else if (round_right) {
    path.AddLine(bounds.X, bounds.Y, right - radius, bounds.Y);
    path.AddArc(right - diameter, bounds.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddLine(right, bounds.Y + radius, right, bottom - radius);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddLine(right - radius, bottom, bounds.X, bottom);
    path.AddLine(bounds.X, bottom, bounds.X, bounds.Y);
  } else {
    path.AddRectangle(bounds);
  }
  path.CloseFigure();
}

inline void Round(HWND control, int radius = 12);

inline int ScaledLogicalPixels(HWND window, int logical_pixels) {
  HDC dc = ::GetDC(window);
  const int dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc)
    ::ReleaseDC(window, dc);
  return ::MulDiv(logical_pixels, dpi, 96);
}

inline int ControlCornerDiameter(HWND window) {
  return (std::max)(4, ScaledLogicalPixels(window, kControlCornerRadiusPx * 2));
}

inline void DrawFolderCard(HWND window,
                           HDC dc,
                           const RECT& bounds,
                           bool hover) {
  const COLORREF surface = SidebarSurface();
  const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
  const bool focused = ::GetFocus() == window;
  const bool highlighted = hover || focused;
  const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
  const Gdiplus::REAL stroke =
      static_cast<Gdiplus::REAL>((std::max)(1, ::MulDiv(1, dpi, 96)));
  Gdiplus::Graphics canvas(dc);
  canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  if (highlighted) {
    const Gdiplus::REAL inset = stroke / 2.0f + 0.5f;
    const Gdiplus::RectF frame(
        inset, inset,
        (std::max)(1.0f,
                   static_cast<Gdiplus::REAL>(bounds.right) - inset * 2.0f),
        (std::max)(1.0f,
                   static_cast<Gdiplus::REAL>(bounds.bottom) - inset * 2.0f));
    Gdiplus::GraphicsPath highlight_path;
    const Gdiplus::REAL radius =
        static_cast<Gdiplus::REAL>(::MulDiv(6, dpi, 96));
    AddControlPath(highlight_path, frame,
                   (std::min)(radius, frame.Height / 2.0f));
    Gdiplus::SolidBrush highlight_brush(
        GdiPlusColor(Mix(accent, surface, hover ? 18 : 11)));
    canvas.FillPath(&highlight_brush, &highlight_path);
    if (focused) {
      Gdiplus::Pen focus_pen(GdiPlusColor(Mix(accent, surface, 120)), stroke);
      canvas.DrawPath(&focus_pen, &highlight_path);
    }
  }

  const int left_inset = (std::max)(9, ::MulDiv(9, dpi, 96));
  const int icon_width = (std::max)(14, ::MulDiv(16, dpi, 96));
  const int icon_height = icon_width;
  const int icon_left = bounds.left + left_inset;
  const int icon_top = (bounds.bottom - icon_height) / 2;
  // Microsoft 365 "Folder Open" 48 px icon.  Keep the original SVG path so
  // the sidebar remains sharp at every DPI without a raster asset.
  const auto icon_point = [&](Gdiplus::REAL x, Gdiplus::REAL y) {
    return Gdiplus::PointF(
        static_cast<Gdiplus::REAL>(icon_left) +
            x * static_cast<Gdiplus::REAL>(icon_width) / 48.0f,
        static_cast<Gdiplus::REAL>(icon_top) +
            y * static_cast<Gdiplus::REAL>(icon_height) / 48.0f);
  };
  Gdiplus::GraphicsPath folder;
  folder.StartFigure();
  folder.AddBezier(icon_point(4.00012f, 12.4984f),
                   icon_point(4.00098f, 10.0138f), icon_point(6.01545f, 8.0f),
                   icon_point(8.50012f, 8.0f));
  folder.AddLine(icon_point(8.50012f, 8.0f), icon_point(16.4112f, 8.0f));
  folder.AddBezier(icon_point(16.4112f, 8.0f), icon_point(17.4622f, 8.0f),
                   icon_point(18.4802f, 8.36793f),
                   icon_point(19.2883f, 9.03995f));
  folder.AddLine(icon_point(19.2883f, 9.03995f),
                 icon_point(24.0503f, 12.9998f));
  folder.AddLine(icon_point(24.0503f, 12.9998f),
                 icon_point(35.5009f, 12.9998f));
  folder.AddBezier(
      icon_point(35.5009f, 12.9998f), icon_point(37.9862f, 12.9998f),
      icon_point(40.0009f, 15.0145f), icon_point(40.0009f, 17.4998f));
  folder.AddLine(icon_point(40.0009f, 17.4998f),
                 icon_point(40.0009f, 19.0039f));
  folder.AddLine(icon_point(40.0009f, 19.0039f),
                 icon_point(12.8431f, 19.0039f));
  folder.AddBezier(
      icon_point(12.8431f, 19.0039f), icon_point(10.7775f, 19.0039f),
      icon_point(8.9772f, 20.4101f), icon_point(8.47701f, 22.4142f));
  folder.AddLine(icon_point(8.47701f, 22.4142f),
                 icon_point(4.63297f, 37.8157f));
  folder.AddBezier(
      icon_point(4.63297f, 37.8157f), icon_point(4.22589f, 37.1388f),
      icon_point(3.9919f, 36.3459f), icon_point(3.99219f, 35.4984f));
  folder.AddLine(icon_point(3.99219f, 35.4984f),
                 icon_point(4.00012f, 12.4984f));
  folder.CloseFigure();
  folder.StartFigure();
  folder.AddBezier(icon_point(7.48993f, 38.7579f),
                   icon_point(7.33243f, 39.3889f), icon_point(7.80976f, 40.0f),
                   icon_point(8.46017f, 40.0f));
  folder.AddLine(icon_point(8.46017f, 40.0f), icon_point(36.9395f, 40.0f));
  folder.AddBezier(icon_point(36.9395f, 40.0f), icon_point(38.5455f, 40.0f),
                   icon_point(39.9454f, 38.9071f),
                   icon_point(40.335f, 37.3491f));
  folder.AddLine(icon_point(40.335f, 37.3491f), icon_point(43.8614f, 23.2465f));
  folder.AddBezier(
      icon_point(43.8614f, 23.2465f), icon_point(44.0192f, 22.6153f),
      icon_point(43.5418f, 22.0039f), icon_point(42.8912f, 22.0039f));
  folder.AddLine(icon_point(42.8912f, 22.0039f),
                 icon_point(12.8431f, 22.0039f));
  folder.AddBezier(
      icon_point(12.8431f, 22.0039f), icon_point(12.1546f, 22.0039f),
      icon_point(11.5545f, 22.4726f), icon_point(11.3877f, 23.1407f));
  folder.AddLine(icon_point(11.3877f, 23.1407f),
                 icon_point(7.48993f, 38.7579f));
  folder.CloseFigure();
  const COLORREF icon_color = highlighted ? accent : RGB(177, 179, 179);
  Gdiplus::SolidBrush icon_brush(GdiPlusColor(icon_color));
  canvas.FillPath(&icon_brush, &folder);

  const int arrow_right = bounds.right - left_inset;
  const int arrow_center_y = (bounds.top + bounds.bottom) / 2;
  const int arrow_width = (std::max)(3, ::MulDiv(3, dpi, 96));
  const int arrow_height = (std::max)(5, ::MulDiv(5, dpi, 96));
  const COLORREF arrow_color =
      highlighted ? accent
                  : Mix(settings_theme::GetColor(COLOR_BTNTEXT), surface, 116);
  Gdiplus::Pen arrow_pen(GdiPlusColor(arrow_color), stroke);
  arrow_pen.SetStartCap(Gdiplus::LineCapRound);
  arrow_pen.SetEndCap(Gdiplus::LineCapRound);
  canvas.DrawLine(&arrow_pen, arrow_right - arrow_width,
                  arrow_center_y - arrow_height / 2, arrow_right,
                  arrow_center_y);
  canvas.DrawLine(&arrow_pen, arrow_right, arrow_center_y,
                  arrow_right - arrow_width, arrow_center_y + arrow_height / 2);

  HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
  const HGDIOBJ old_font = font ? ::SelectObject(dc, font) : nullptr;
  ::SetBkMode(dc, TRANSPARENT);
  const int text_left =
      icon_left + icon_width + (std::max)(7, ::MulDiv(7, dpi, 96));
  const int text_right = arrow_right - (std::max)(9, ::MulDiv(9, dpi, 96));
  ::SetTextColor(dc, settings_theme::GetColor(COLOR_BTNTEXT));
  const std::wstring title =
      LocalText(L"用户文件夹", L"使用者資料夾", L"User folder");
  SIZE title_size{};
  ::GetTextExtentPoint32W(dc, title.c_str(), static_cast<int>(title.size()),
                          &title_size);
  const int title_right =
      (std::min)(text_right, text_left + static_cast<int>(title_size.cx));
  RECT title_bounds{text_left, bounds.top, title_right, bounds.bottom};
  ::DrawTextW(
      dc, title.c_str(), -1, &title_bounds,
      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  LOGFONTW secondary_spec{};
  HFONT secondary_font = nullptr;
  if (font && ::GetObjectW(font, sizeof(secondary_spec), &secondary_spec)) {
    secondary_spec.lfHeight = secondary_spec.lfHeight * 84 / 100;
    secondary_font = ::CreateFontIndirectW(&secondary_spec);
  }
  if (secondary_font)
    ::SelectObject(dc, secondary_font);
  ::SetTextColor(dc,
                 Mix(settings_theme::GetColor(COLOR_BTNTEXT), surface, 124));
  wchar_t path[MAX_PATH]{};
  ::GetWindowTextW(window, path, MAX_PATH);
  const int text_gap = (std::max)(8, ::MulDiv(8, dpi, 96));
  RECT path_bounds{title_right + text_gap, bounds.top, text_right,
                   bounds.bottom};
  ::DrawTextW(
      dc, path, -1, &path_bounds,
      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old_font)
    ::SelectObject(dc, old_font);
  if (secondary_font)
    ::DeleteObject(secondary_font);
}

inline LRESULT CALLBACK NavProc(HWND window,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam,
                                UINT_PTR,
                                DWORD_PTR data) {
  auto* state = reinterpret_cast<NavState*>(data);
  if (message == WM_MOUSEMOVE) {
    if (!state->hover) {
      state->hover = true;
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
      ::TrackMouseEvent(&tracking);
      ::InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  } else if (message == WM_MOUSELEAVE) {
    state->hover = false;
    ::InvalidateRect(window, nullptr, FALSE);
    return 0;
  } else if ((message == WM_SETFOCUS || message == WM_KILLFOCUS) &&
             state->link) {
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    ::InvalidateRect(window, nullptr, FALSE);
    return result;
  } else if (message == WM_ERASEBKGND) {
    return 1;
  } else if (message == WM_LBUTTONUP && state->link) {
    wchar_t folder[MAX_PATH]{};
    ::GetWindowTextW(window, folder, MAX_PATH);
    ::ShellExecuteW(::GetParent(window), L"open", folder, nullptr, nullptr,
                    SW_SHOWNORMAL);
    return 0;
  } else if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC target = ::BeginPaint(window, &paint);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    HDC buffer = target ? ::CreateCompatibleDC(target) : nullptr;
    HBITMAP bitmap = buffer && width > 0 && height > 0
                         ? ::CreateCompatibleBitmap(target, width, height)
                         : nullptr;
    const HGDIOBJ previous_bitmap =
        bitmap ? ::SelectObject(buffer, bitmap) : nullptr;
    HDC dc = bitmap ? buffer : target;
    const auto finish_paint = [&]() {
      if (bitmap)
        ::BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
      if (previous_bitmap)
        ::SelectObject(buffer, previous_bitmap);
      if (bitmap)
        ::DeleteObject(bitmap);
      if (buffer)
        ::DeleteDC(buffer);
      ::EndPaint(window, &paint);
    };
    const COLORREF surface = SidebarSurface();
    const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
    const COLORREF fill =
        state->active ? Mix(accent, surface, 38)
                      : (state->hover ? Mix(accent, surface, 16) : surface);
    HBRUSH surface_brush = ::CreateSolidBrush(surface);
    ::FillRect(dc, &bounds, surface_brush);
    ::DeleteObject(surface_brush);
    if (state->link) {
      DrawFolderCard(window, dc, bounds, state->hover);
      finish_paint();
      return 0;
    }
    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_NULL, 0, fill);
    const HGDIOBJ old_brush = ::SelectObject(dc, brush);
    const HGDIOBJ old_pen = ::SelectObject(dc, pen);
    const int diameter = ::MulDiv(12, ::GetDeviceCaps(dc, LOGPIXELSX), 96);
    ::RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom,
                diameter, diameter);
    ::SelectObject(dc, old_pen);
    ::SelectObject(dc, old_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
    if (state->active) {
      const int measured_inset =
          static_cast<int>((bounds.bottom - bounds.top) / 4);
      const int inset = measured_inset > 4 ? measured_inset : 4;
      const int measured_marker_width =
          ::MulDiv(4, ::GetDeviceCaps(dc, LOGPIXELSX), 96);
      const int marker_width =
          measured_marker_width > 3 ? measured_marker_width : 3;
      RECT marker{bounds.left + 2, bounds.top + inset,
                  bounds.left + 2 + marker_width, bounds.bottom - inset};
      HBRUSH marker_brush = ::CreateSolidBrush(accent);
      HRGN marker_region = ::CreateRoundRectRgn(
          marker.left, marker.top, marker.right, marker.bottom,
          marker.right - marker.left, marker.right - marker.left);
      if (marker_region) {
        ::FillRgn(dc, marker_region, marker_brush);
        ::DeleteObject(marker_region);
      }
      ::DeleteObject(marker_brush);
    }
    const bool unapplied = !state->link &&
                           ::GetDlgCtrlID(window) <= kStatusIcons &&
                           HasUnappliedChanges(state->page);
    if (unapplied) {
      const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
      const int dot_size = (std::max)(5, ::MulDiv(6, dpi, 96));
      const int right_inset = (std::max)(10, ::MulDiv(12, dpi, 96));
      const int center_y = (bounds.top + bounds.bottom) / 2;
      RECT dot{bounds.right - right_inset - dot_size, center_y - dot_size / 2,
               bounds.right - right_inset, center_y - dot_size / 2 + dot_size};
      HBRUSH dot_brush = ::CreateSolidBrush(accent);
      HPEN dot_pen = ::CreatePen(PS_NULL, 0, accent);
      const HGDIOBJ old_dot_brush = ::SelectObject(dc, dot_brush);
      const HGDIOBJ old_dot_pen = ::SelectObject(dc, dot_pen);
      ::Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
      ::SelectObject(dc, old_dot_pen);
      ::SelectObject(dc, old_dot_brush);
      ::DeleteObject(dot_pen);
      ::DeleteObject(dot_brush);
    }
    wchar_t text[256]{};
    ::GetWindowTextW(window, text, 256);
    HFONT font =
        reinterpret_cast<HFONT>(::SendMessage(window, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(dc, font) : nullptr;
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(
        dc, state->link ? accent : settings_theme::GetColor(COLOR_BTNTEXT));
    if (!state->link)
      bounds.left += 18;
    if (unapplied)
      bounds.right -=
          (std::max)(22, ::MulDiv(24, ::GetDeviceCaps(dc, LOGPIXELSX), 96));
    ::DrawTextW(dc, text, -1, &bounds,
                (state->link ? DT_CENTER : DT_LEFT) | DT_VCENTER |
                    DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old_font)
      ::SelectObject(dc, old_font);
    finish_paint();
    return 0;
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, NavProc, 1);
    delete state;
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
}

template <typename Painter>
inline void PaintBuffered(HWND window, const Painter& painter) {
  PAINTSTRUCT paint{};
  HDC target = ::BeginPaint(window, &paint);
  RECT bounds{};
  ::GetClientRect(window, &bounds);
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  HDC buffer = target ? ::CreateCompatibleDC(target) : nullptr;
  HBITMAP bitmap = buffer && width > 0 && height > 0
                       ? ::CreateCompatibleBitmap(target, width, height)
                       : nullptr;
  const HGDIOBJ previous = bitmap ? ::SelectObject(buffer, bitmap) : nullptr;
  HDC dc = bitmap ? buffer : target;
  if (dc) {
    painter(dc, bounds);
    if (bitmap)
      ::BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
  }
  if (previous)
    ::SelectObject(buffer, previous);
  if (bitmap)
    ::DeleteObject(bitmap);
  if (buffer)
    ::DeleteDC(buffer);
  ::EndPaint(window, &paint);
}

inline LRESULT CALLBACK GroupBoxProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR,
                                     DWORD_PTR) {
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == settings_theme::kChanged) {
    ::InvalidateRect(window, nullptr, FALSE);
    return 0;
  }
  if (message == WM_PAINT) {
    PaintBuffered(window, [&](HDC dc, const RECT& bounds) {
      const COLORREF surface = settings_theme::GetColor(COLOR_BTNFACE);
      HBRUSH surface_brush = ::CreateSolidBrush(surface);
      ::FillRect(dc, &bounds, surface_brush);
      ::DeleteObject(surface_brush);

      HFONT font =
          reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
      const HGDIOBJ previous_font = font ? ::SelectObject(dc, font) : nullptr;
      wchar_t label[128]{};
      ::GetWindowTextW(window, label, static_cast<int>(_countof(label)));
      SIZE text_size{};
      ::GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)),
                              &text_size);
      const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
      const int line_y = (std::max)(4, static_cast<int>(text_size.cy / 2));
      const COLORREF border =
          Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 82);
      HPEN pen = ::CreatePen(PS_SOLID, 1, border);
      HGDIOBJ previous_pen = ::SelectObject(dc, pen);
      HGDIOBJ previous_brush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
      ::RoundRect(dc, bounds.left, line_y, bounds.right - 1, bounds.bottom - 1,
                  ::MulDiv(8, dpi, 96), ::MulDiv(8, dpi, 96));
      ::SelectObject(dc, previous_brush);
      ::SelectObject(dc, previous_pen);
      ::DeleteObject(pen);

      RECT title{bounds.left + ::MulDiv(10, dpi, 96), 0,
                 bounds.left + ::MulDiv(16, dpi, 96) + text_size.cx,
                 text_size.cy};
      HBRUSH title_background = ::CreateSolidBrush(surface);
      ::FillRect(dc, &title, title_background);
      ::DeleteObject(title_background);
      title.left += ::MulDiv(3, dpi, 96);
      ::SetBkMode(dc, TRANSPARENT);
      ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
      ::DrawTextW(dc, label, -1, &title,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
      if (previous_font)
        ::SelectObject(dc, previous_font);
    });
    return 0;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(window, GroupBoxProc, 9);
  return ::DefSubclassProc(window, message, wparam, lparam);
}

inline LRESULT CALLBACK ToggleProc(HWND window,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam,
                                   UINT_PTR,
                                   DWORD_PTR data) {
  auto* state = reinterpret_cast<ToggleState*>(data);
  if (message == WM_MOUSEMOVE) {
    if (!state->hover) {
      state->hover = true;
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
      ::TrackMouseEvent(&tracking);
      ::InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  } else if (message == WM_MOUSELEAVE) {
    state->hover = false;
    ::InvalidateRect(window, nullptr, FALSE);
    return 0;
  } else if (message == WM_ERASEBKGND) {
    return 1;
  } else if (message == BM_SETCHECK || message == WM_ENABLE ||
             message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    ::InvalidateRect(window, nullptr, FALSE);
    return result;
  } else if (message == WM_PAINT) {
    PaintBuffered(window, [&](HDC dc, const RECT& bounds) {
      HBRUSH background = CreateControlBackgroundBrush(state->background);
      ::FillRect(dc, &bounds, background);
      ::DeleteObject(background);
      const bool enabled = ::IsWindowEnabled(window) != FALSE;
      const bool checked =
          ::SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
      const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
      const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
      const COLORREF neutral =
          Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 34);
      const COLORREF fill = !enabled  ? settings_theme::GetColor(COLOR_BTNFACE)
                            : checked ? accent
                            : state->hover   ? Mix(accent, surface, 20)
                            : state->neutral ? neutral
                                             : surface;
      const COLORREF border =
          checked ? accent
                  : Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 76);
      Gdiplus::Graphics canvas(dc);
      canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
      const Gdiplus::RectF shape(0.5f, 0.5f,
                                 static_cast<Gdiplus::REAL>(bounds.right - 1),
                                 static_cast<Gdiplus::REAL>(bounds.bottom - 1));
      const Gdiplus::REAL radius = (std::min)(
          shape.Height / 2.0f, static_cast<Gdiplus::REAL>(::MulDiv(
                                   5, ::GetDeviceCaps(dc, LOGPIXELSX), 96)));
      Gdiplus::GraphicsPath path;
      const bool round_left = state->segment == ToggleState::Segment::None ||
                              state->segment == ToggleState::Segment::Left;
      const bool round_right = state->segment == ToggleState::Segment::None ||
                               state->segment == ToggleState::Segment::Right;
      AddControlPath(path, shape, radius, round_left, round_right);
      Gdiplus::SolidBrush brush(GdiPlusColor(fill));
      Gdiplus::Pen pen(GdiPlusColor(border), 1.0f);
      canvas.FillPath(&brush, &path);
      canvas.DrawPath(&pen, &path);

      wchar_t label[128]{};
      ::GetWindowTextW(window, label, static_cast<int>(_countof(label)));
      HFONT font =
          reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
      const HGDIOBJ previous_font = font ? ::SelectObject(dc, font) : nullptr;
      ::SetBkMode(dc, TRANSPARENT);
      ::SetTextColor(dc, !enabled ? settings_theme::GetColor(COLOR_GRAYTEXT)
                         : checked
                             ? settings_theme::GetColor(COLOR_HIGHLIGHTTEXT)
                             : settings_theme::GetColor(COLOR_WINDOWTEXT));
      RECT text_bounds = bounds;
      ::DrawTextW(dc, label, -1, &text_bounds,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
      const LRESULT ui_state = ::SendMessageW(window, WM_QUERYUISTATE, 0, 0);
      if (::GetFocus() == window && !(ui_state & UISF_HIDEFOCUS)) {
        Gdiplus::Pen focus(GdiPlusColor(accent, 180), 1.0f);
        focus.SetDashStyle(Gdiplus::DashStyleDot);
        canvas.DrawPath(&focus, &path);
      }
      if (previous_font)
        ::SelectObject(dc, previous_font);
    });
    return 0;
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, ToggleProc, 2);
    delete state;
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
}

inline LRESULT CALLBACK CheckboxProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR,
                                     DWORD_PTR data) {
  auto* state = reinterpret_cast<CheckboxState*>(data);
  if (message == WM_MOUSEMOVE && !state->hover) {
    state->hover = true;
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
    ::TrackMouseEvent(&tracking);
    ::InvalidateRect(window, nullptr, FALSE);
  } else if (message == WM_MOUSELEAVE) {
    state->hover = false;
    ::InvalidateRect(window, nullptr, FALSE);
  } else if (message == WM_ERASEBKGND) {
    return 1;
  } else if (message == BM_SETCHECK || message == WM_ENABLE ||
             message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    ::InvalidateRect(window, nullptr, FALSE);
    return result;
  } else if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(window, &paint);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    const bool enabled = ::IsWindowEnabled(window) != FALSE;
    const bool checked =
        ::SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
    const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
    HBRUSH background = CreateControlBackgroundBrush(state->background);
    ::FillRect(dc, &bounds, background);
    ::DeleteObject(background);

    const int scale = ::GetDeviceCaps(dc, LOGPIXELSX);
    const int measured_size = ::MulDiv(14, scale, 96);
    const int control_height = static_cast<int>(bounds.bottom - bounds.top);
    const int box_size =
        (std::min)(control_height - 2, (std::max)(measured_size, 12));
    const int box_top = (control_height - box_size) / 2;
    RECT box{bounds.left, box_top, bounds.left + box_size, box_top + box_size};
    const COLORREF fill = !enabled  ? settings_theme::GetColor(COLOR_BTNFACE)
                          : checked ? accent
                          : state->hover ? Mix(accent, surface, 18)
                                         : surface;
    const COLORREF border =
        checked ? accent
                : Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 96);
    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ previous_brush = ::SelectObject(dc, brush);
    const HGDIOBJ previous_pen = ::SelectObject(dc, pen);
    const int radius = (std::max)(2, ::MulDiv(3, scale, 96));
    ::RoundRect(dc, box.left, box.top, box.right, box.bottom, radius, radius);
    ::SelectObject(dc, previous_pen);
    ::SelectObject(dc, previous_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);

    if (checked) {
      HPEN check_pen =
          ::CreatePen(PS_SOLID, (std::max)(2, ::MulDiv(2, scale, 96)),
                      settings_theme::GetColor(COLOR_HIGHLIGHTTEXT));
      const HGDIOBJ previous = ::SelectObject(dc, check_pen);
      ::MoveToEx(dc, box.left + box_size * 2 / 9, box.top + box_size / 2,
                 nullptr);
      ::LineTo(dc, box.left + box_size * 4 / 9, box.top + box_size * 7 / 10);
      ::LineTo(dc, box.left + box_size * 8 / 10, box.top + box_size * 3 / 10);
      ::SelectObject(dc, previous);
      ::DeleteObject(check_pen);
    }

    wchar_t label[128]{};
    ::GetWindowTextW(window, label, static_cast<int>(_countof(label)));
    HFONT font =
        reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
    const HGDIOBJ previous_font = font ? ::SelectObject(dc, font) : nullptr;
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, enabled ? settings_theme::GetColor(COLOR_WINDOWTEXT)
                               : settings_theme::GetColor(COLOR_GRAYTEXT));
    RECT text_bounds = bounds;
    text_bounds.left = box.right + ::MulDiv(6, scale, 96);
    ::DrawTextW(
        dc, label, -1, &text_bounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous_font)
      ::SelectObject(dc, previous_font);
    ::EndPaint(window, &paint);
    return 0;
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, CheckboxProc, 3);
    delete state;
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
}

inline LRESULT CALLBACK SwitchProc(HWND window,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam,
                                   UINT_PTR,
                                   DWORD_PTR data) {
  auto* state = reinterpret_cast<SwitchState*>(data);
  if (message == WM_MOUSEMOVE) {
    if (!state->hover) {
      state->hover = true;
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
      ::TrackMouseEvent(&tracking);
      ::InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  } else if (message == WM_MOUSELEAVE) {
    state->hover = false;
    ::InvalidateRect(window, nullptr, FALSE);
    return 0;
  } else if (message == WM_ERASEBKGND) {
    return 1;
  } else if (message == BM_SETCHECK || message == WM_ENABLE ||
             message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    ::InvalidateRect(window, nullptr, FALSE);
    return result;
  } else if (message == WM_PAINT) {
    PaintBuffered(window, [&](HDC dc, const RECT& bounds) {
      ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
      const bool enabled = ::IsWindowEnabled(window) != FALSE;
      const bool checked =
          ::SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
      const int scale = ::GetDeviceCaps(dc, LOGPIXELSX);
      const int track_height = (std::max)(12, ::MulDiv(14, scale, 96));
      const int track_width = (std::max)(24, ::MulDiv(28, scale, 96));
      const int left = bounds.right - track_width;
      const int top = (bounds.bottom - bounds.top - track_height) / 2;
      RECT track{left, top, left + track_width, top + track_height};
      const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
      const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
      const COLORREF off = Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                               surface, state->hover ? 104 : 86);
      const COLORREF fill = !enabled  ? settings_theme::GetColor(COLOR_BTNFACE)
                            : checked ? accent
                                      : off;
      Gdiplus::Graphics canvas(dc);
      canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
      const Gdiplus::RectF track_shape(
          static_cast<Gdiplus::REAL>(track.left) + 0.5f,
          static_cast<Gdiplus::REAL>(track.top) + 0.5f,
          static_cast<Gdiplus::REAL>(track_width) - 1.0f,
          static_cast<Gdiplus::REAL>(track_height) - 1.0f);
      Gdiplus::GraphicsPath track_path;
      AddControlPath(track_path, track_shape, track_shape.Height / 2.0f);
      Gdiplus::SolidBrush track_brush(GdiPlusColor(fill));
      canvas.FillPath(&track_brush, &track_path);

      const int inset = (std::max)(2, ::MulDiv(2, scale, 96));
      const int knob_size = track_height - inset * 2;
      const int knob_left =
          checked ? track.right - inset - knob_size : track.left + inset;
      Gdiplus::SolidBrush knob(Gdiplus::Color(255, 255, 255, 255));
      Gdiplus::Pen knob_border(Gdiplus::Color(42, 0, 0, 0), 1.0f);
      const Gdiplus::RectF knob_shape(
          static_cast<Gdiplus::REAL>(knob_left) + 0.5f,
          static_cast<Gdiplus::REAL>(track.top + inset) + 0.5f,
          static_cast<Gdiplus::REAL>(knob_size) - 1.0f,
          static_cast<Gdiplus::REAL>(knob_size) - 1.0f);
      canvas.FillEllipse(&knob, knob_shape);
      canvas.DrawEllipse(&knob_border, knob_shape);
      const LRESULT ui_state = ::SendMessageW(window, WM_QUERYUISTATE, 0, 0);
      if (::GetFocus() == window && !(ui_state & UISF_HIDEFOCUS)) {
        Gdiplus::Pen focus(GdiPlusColor(accent, 180), 1.0f);
        focus.SetDashStyle(Gdiplus::DashStyleDot);
        canvas.DrawPath(&focus, &track_path);
      }
    });
    return 0;
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, SwitchProc, 5);
    delete state;
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
}

inline void DrawComboField(HWND window,
                           HDC dc,
                           const RECT& bounds,
                           const ComboState* state) {
  const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
  const bool enabled = ::IsWindowEnabled(window) != FALSE;
  const bool focused = ::GetFocus() == window ||
                       ::SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0) != 0;
  const bool hover = state && state->hover;
  const COLORREF fill =
      enabled ? surface : settings_theme::GetColor(COLOR_BTNFACE);
  const COLORREF border =
      focused ? settings_theme::GetColor(COLOR_HIGHLIGHT)
      : hover ? Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 96)
              : Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 76);
  const COLORREF foreground =
      settings_theme::GetColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
  ::FillRect(dc, &bounds,
             settings_theme::GetBrush(enabled ? COLOR_WINDOW : COLOR_BTNFACE));

  const int scale = ::GetDeviceCaps(dc, LOGPIXELSX);
  // Keep the closed field outline at one physical pixel. Scaling this stroke
  // with DPI makes it visibly heavier than the adjacent one-pixel list frames
  // (and turns it into a two-pixel outline at common high-DPI settings).
  constexpr Gdiplus::REAL border_stroke = 1.0f;
  const Gdiplus::REAL glyph_stroke =
      static_cast<Gdiplus::REAL>((std::max)(1, ::MulDiv(1, scale, 96)));
  constexpr Gdiplus::REAL inset = border_stroke / 2.0f;
  const Gdiplus::RectF frame(
      inset, inset,
      (std::max)(1.0f, static_cast<Gdiplus::REAL>(bounds.right) - inset * 2.0f),
      (std::max)(1.0f,
                 static_cast<Gdiplus::REAL>(bounds.bottom) - inset * 2.0f));
  const Gdiplus::REAL radius =
      static_cast<Gdiplus::REAL>(ControlCornerDiameter(window)) / 2.0f;
  Gdiplus::GraphicsPath path;
  AddControlPath(path, frame, (std::min)(radius, frame.Height / 2.0f));
  {
    Gdiplus::Graphics canvas(dc);
    canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush brush(GdiPlusColor(fill));
    Gdiplus::Pen pen(GdiPlusColor(border), border_stroke);
    canvas.FillPath(&brush, &path);
    canvas.DrawPath(&pen, &path);

    const Gdiplus::REAL center_x = static_cast<Gdiplus::REAL>(
        bounds.right - (std::max)(10, ::MulDiv(11, scale, 96)));
    const Gdiplus::REAL center_y =
        static_cast<Gdiplus::REAL>(bounds.bottom - bounds.top) / 2.0f;
    const Gdiplus::REAL half_width =
        static_cast<Gdiplus::REAL>((std::max)(3, ::MulDiv(3, scale, 96)));
    const Gdiplus::REAL arrow_height =
        static_cast<Gdiplus::REAL>((std::max)(2, ::MulDiv(2, scale, 96)));
    Gdiplus::Pen arrow(GdiPlusColor(foreground), glyph_stroke);
    arrow.SetStartCap(Gdiplus::LineCapRound);
    arrow.SetEndCap(Gdiplus::LineCapRound);
    canvas.DrawLine(&arrow, center_x - half_width, center_y - arrow_height / 2,
                    center_x, center_y + arrow_height / 2);
    canvas.DrawLine(&arrow, center_x, center_y + arrow_height / 2,
                    center_x + half_width, center_y - arrow_height / 2);
  }

  wchar_t label[512]{};
  ::GetWindowTextW(window, label, static_cast<int>(std::size(label)));
  HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
  const HGDIOBJ previous_font = font ? ::SelectObject(dc, font) : nullptr;
  ::SetBkMode(dc, TRANSPARENT);
  ::SetTextColor(dc, foreground);
  RECT text_bounds = bounds;
  text_bounds.left += (std::max)(6, ::MulDiv(6, scale, 96));
  text_bounds.right -= (std::max)(24, ::MulDiv(24, scale, 96));
  ::DrawTextW(
      dc, label, -1, &text_bounds,
      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (previous_font)
    ::SelectObject(dc, previous_font);
}

inline UINT MeasureComboItemHeight(HWND dialog) {
  RECT height = {0, 0, 0, kComboItemHeightDlu};
  ::MapDialogRect(dialog, &height);
  return (std::max)(16u, static_cast<UINT>(height.bottom));
}

inline void DrawComboItem(const DRAWITEMSTRUCT& draw) {
  const bool field = (draw.itemState & ODS_COMBOBOXEDIT) != 0;
  const bool selected = (draw.itemState & ODS_SELECTED) != 0 && !field;
  const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
  const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF selected_fill =
      Mix(settings_theme::GetColor(COLOR_HIGHLIGHT), surface, 25);
  ::FillRect(draw.hDC, &draw.rcItem, settings_theme::GetBrush(COLOR_WINDOW));

  RECT selection = draw.rcItem;
  const int scale = ::GetDeviceCaps(draw.hDC, LOGPIXELSX);
  if (selected) {
    const int horizontal_inset = (std::max)(3, ::MulDiv(3, scale, 96));
    const int vertical_inset = (std::max)(1, ::MulDiv(1, scale, 96));
    selection.left += horizontal_inset;
    selection.top += vertical_inset;
    selection.right -= horizontal_inset;
    selection.bottom -= vertical_inset;
    HBRUSH selection_brush = ::CreateSolidBrush(selected_fill);
    HPEN selection_pen = ::CreatePen(PS_NULL, 0, selected_fill);
    const HGDIOBJ old_brush = ::SelectObject(draw.hDC, selection_brush);
    const HGDIOBJ old_pen = ::SelectObject(draw.hDC, selection_pen);
    const int radius = (std::max)(6, ::MulDiv(8, scale, 96));
    ::RoundRect(draw.hDC, selection.left, selection.top, selection.right,
                selection.bottom, radius, radius);
    ::SelectObject(draw.hDC, old_pen);
    ::SelectObject(draw.hDC, old_brush);
    ::DeleteObject(selection_pen);
    ::DeleteObject(selection_brush);

    const int marker_width = (std::max)(2, ::MulDiv(3, scale, 96));
    RECT marker{selection.left + horizontal_inset,
                selection.top + (selection.bottom - selection.top) / 4,
                selection.left + horizontal_inset + marker_width,
                selection.bottom - (selection.bottom - selection.top) / 4};
    HBRUSH marker_brush =
        ::CreateSolidBrush(settings_theme::GetColor(COLOR_HIGHLIGHT));
    HRGN marker_region =
        ::CreateRoundRectRgn(marker.left, marker.top, marker.right,
                             marker.bottom, marker_width, marker_width);
    if (marker_region) {
      ::FillRgn(draw.hDC, marker_region, marker_brush);
      ::DeleteObject(marker_region);
    }
    ::DeleteObject(marker_brush);
  }

  std::wstring label;
  int item = static_cast<int>(draw.itemID);
  if (item == -1)
    item = static_cast<int>(::SendMessageW(draw.hwndItem, CB_GETCURSEL, 0, 0));
  if (item != CB_ERR) {
    const int length = static_cast<int>(
        ::SendMessageW(draw.hwndItem, CB_GETLBTEXTLEN, item, 0));
    if (length >= 0) {
      label.resize(static_cast<size_t>(length) + 1);
      ::SendMessageW(draw.hwndItem, CB_GETLBTEXT, item,
                     reinterpret_cast<LPARAM>(label.data()));
      label.resize(static_cast<size_t>(length));
    }
  }
  RECT text = draw.rcItem;
  text.left += selected ? (std::max)(13, ::MulDiv(13, scale, 96))
                        : (std::max)(7, ::MulDiv(7, scale, 96));
  text.right -= (std::max)(7, ::MulDiv(7, scale, 96));
  ::SetBkMode(draw.hDC, TRANSPARENT);
  ::SetTextColor(draw.hDC, settings_theme::GetColor(
                               disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
  HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
  const HGDIOBJ old_font = font ? ::SelectObject(draw.hDC, font) : nullptr;
  ::DrawTextW(
      draw.hDC, label.c_str(), -1, &text,
      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old_font)
    ::SelectObject(draw.hDC, old_font);
}

inline void RoundComboWindow(HWND window) {
  Round(window, ControlCornerDiameter(window));
}

inline void SizeComboDropList(HWND combo, HWND list) {
  if (!combo || !list)
    return;
  const int count = static_cast<int>(::SendMessageW(combo, CB_GETCOUNT, 0, 0));
  if (count <= 0)
    return;

  const int visible = (std::min)(count, kComboVisibleItemLimit);
  int content_height = 0;
  for (int index = 0; index < visible; ++index) {
    const LRESULT measured = ::SendMessageW(combo, CB_GETITEMHEIGHT, index, 0);
    if (measured != CB_ERR)
      content_height += static_cast<int>(measured);
  }
  if (content_height <= 0)
    return;

  RECT list_bounds{};
  RECT list_client{};
  RECT combo_bounds{};
  if (!::GetWindowRect(list, &list_bounds) ||
      !::GetClientRect(list, &list_client) ||
      !::GetWindowRect(combo, &combo_bounds))
    return;
  const int current_height = list_bounds.bottom - list_bounds.top;
  const int client_height = list_client.bottom - list_client.top;
  const int chrome_height = (std::max)(0, current_height - client_height);
  const int desired_height = content_height + chrome_height;
  const bool opens_upward = list_bounds.bottom <= combo_bounds.top;
  const int top =
      opens_upward ? list_bounds.bottom - desired_height : list_bounds.top;
  ::SetWindowPos(list, nullptr, list_bounds.left, top,
                 list_bounds.right - list_bounds.left, desired_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  ::ShowScrollBar(list, SB_VERT, count > kComboVisibleItemLimit ? TRUE : FALSE);
}

inline void SetComboDroppedState(HWND window, bool dropped) {
  COMBOBOXINFO info{sizeof(info)};
  ::GetComboBoxInfo(window, &info);
  BOOL animation_enabled = FALSE;
  const bool suppress_animation =
      dropped && info.hwndList &&
      !::GetPropW(info.hwndList, kComboAnimationProperty) &&
      ::SystemParametersInfoW(SPI_GETCOMBOBOXANIMATION, 0, &animation_enabled,
                              0) != FALSE &&
      animation_enabled != FALSE;
  if (suppress_animation &&
      ::SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, FALSE, nullptr, 0))
    ::SetPropW(info.hwndList, kComboAnimationProperty,
               reinterpret_cast<HANDLE>(1));

  ::SendMessageW(window, CB_SHOWDROPDOWN, dropped ? TRUE : FALSE, 0);
  if (dropped)
    SizeComboDropList(window, info.hwndList);
  ::RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);

  if (!dropped)
    RestoreComboAnimation(info.hwndList);
}

inline LRESULT CALLBACK ComboListProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR,
                                      DWORD_PTR) {
  const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
  if ((message == WM_SHOWWINDOW && wparam == FALSE) || message == WM_NCDESTROY)
    RestoreComboAnimation(window);
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(window, ComboListProc, 6);
  return result;
}

inline LRESULT CALLBACK ComboProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam,
                                  UINT_PTR,
                                  DWORD_PTR data) {
  auto* state = reinterpret_cast<ComboState*>(data);
  if (message == WM_LBUTTONDOWN) {
    ::SetFocus(window);
    const bool dropped =
        ::SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
    SetComboDroppedState(window, !dropped);
    return 0;
  }
  if ((message == WM_KEYDOWN && wparam == VK_F4) ||
      (message == WM_SYSKEYDOWN && wparam == VK_DOWN &&
       (::GetKeyState(VK_MENU) & 0x8000) != 0)) {
    const bool dropped =
        ::SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
    SetComboDroppedState(window, !dropped);
    return 0;
  }
  if (message == WM_MOUSEMOVE && state) {
    if (!state->hover) {
      state->hover = true;
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
      ::TrackMouseEvent(&tracking);
      ::RedrawWindow(window, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    }
    return 0;
  }
  if (message == WM_MOUSELEAVE && state) {
    state->hover = false;
    ::RedrawWindow(window, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    return 0;
  }
  if (message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK)
    return 0;
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_NCPAINT)
    return 0;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC target = ::BeginPaint(window, &paint);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    HDC buffer = target ? ::CreateCompatibleDC(target) : nullptr;
    HBITMAP bitmap = buffer && width > 0 && height > 0
                         ? ::CreateCompatibleBitmap(target, width, height)
                         : nullptr;
    const HGDIOBJ previous = bitmap ? ::SelectObject(buffer, bitmap) : nullptr;
    HDC paint_dc = bitmap ? buffer : target;
    if (paint_dc) {
      ::FillRect(paint_dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
      DrawComboField(window, paint_dc, bounds, state);
      if (bitmap)
        ::BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    }
    if (previous)
      ::SelectObject(buffer, previous);
    if (bitmap)
      ::DeleteObject(bitmap);
    if (buffer)
      ::DeleteDC(buffer);
    ::EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_PRINTCLIENT) {
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    DrawComboField(window, reinterpret_cast<HDC>(wparam), bounds, state);
    return 0;
  }
  if (message == WM_SETFOCUS || message == WM_KILLFOCUS ||
      message == WM_ENABLE || message == WM_SIZE || message == WM_SETTEXT ||
      message == CB_SETCURSEL || message == WM_THEMECHANGED) {
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_SIZE || message == WM_THEMECHANGED)
      RoundComboWindow(window);
    ::RedrawWindow(window, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    return result;
  }
  if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, ComboProc, 7);
    delete state;
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
}

inline HWND Create(HWND dialog,
                   const wchar_t* class_name,
                   const std::wstring& text,
                   DWORD style,
                   WORD id,
                   int x,
                   int y,
                   int width,
                   int height) {
  HWND control = ::CreateWindowExW(
      0, class_name, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, width,
      height, dialog, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
      ::GetModuleHandleW(nullptr), nullptr);
  const HFONT font =
      reinterpret_cast<HFONT>(::SendMessage(dialog, WM_GETFONT, 0, 0));
  if (control && font)
    ::SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

inline void Round(HWND control, int radius) {
  if (!control)
    return;
  RECT bounds{};
  ::GetClientRect(control, &bounds);
  HRGN region = ::CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1,
                                     bounds.bottom + 1, radius, radius);
  if (region && !::SetWindowRgn(control, region, TRUE))
    ::DeleteObject(region);
}

inline void RoundDlu(HWND dialog, WORD id, int radius_dlu) {
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  const RECT radius = MapDialogUnits(dialog, 0, 0, radius_dlu, radius_dlu);
  const int measured_radius = static_cast<int>(radius.right);
  Round(control, measured_radius > 4 ? measured_radius : 4);
}

inline void StyleActionButton(
    HWND dialog,
    WORD id,
    ToggleState::Background background = ToggleState::Background::Window) {
  HWND control = ::GetDlgItem(dialog, id);
  if (control) {
    Round(control, ControlCornerDiameter(control));
    EnsureGdiPlus();
    DWORD_PTR data = 0;
    if (!::GetWindowSubclass(control, ToggleProc, 2, &data)) {
      auto* state = new ToggleState;
      if (!::SetWindowSubclass(control, ToggleProc, 2,
                               reinterpret_cast<DWORD_PTR>(state)))
        delete state;
      else
        data = reinterpret_cast<DWORD_PTR>(state);
    }
    if (data)
      reinterpret_cast<ToggleState*>(data)->background = background;
  }
}

inline void StyleInput(HWND dialog, WORD id) {
  HWND control = ::GetDlgItem(dialog, id);
  if (control)
    Round(control, ControlCornerDiameter(control));
}

inline void StyleCard(HWND dialog, WORD id) {
  RoundDlu(dialog, id, kCardRadiusDlu);
}

inline void StyleToggle(HWND dialog, WORD id) {
  EnsureGdiPlus();
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  ::SetWindowLongPtrW(control, GWL_STYLE,
                      ::GetWindowLongPtrW(control, GWL_STYLE) | BS_PUSHLIKE);
  DWORD_PTR existing = 0;
  if (!::GetWindowSubclass(control, ToggleProc, 2, &existing)) {
    auto* state = new ToggleState;
    if (!::SetWindowSubclass(control, ToggleProc, 2,
                             reinterpret_cast<DWORD_PTR>(state)))
      delete state;
  }
}

inline void StyleSegmentedToggle(HWND dialog,
                                 WORD id,
                                 ToggleState::Segment segment) {
  StyleToggle(dialog, id);
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  DWORD_PTR data = 0;
  if (::GetWindowSubclass(control, ToggleProc, 2, &data) && data) {
    auto* state = reinterpret_cast<ToggleState*>(data);
    state->neutral = true;
    state->segment = segment;
  }
}

inline void StyleCheckbox(
    HWND dialog,
    WORD id,
    ToggleState::Background background = ToggleState::Background::Window) {
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  DWORD_PTR existing = 0;
  if (!::GetWindowSubclass(control, CheckboxProc, 3, &existing)) {
    auto* state = new CheckboxState;
    if (!::SetWindowSubclass(control, CheckboxProc, 3,
                             reinterpret_cast<DWORD_PTR>(state)))
      delete state;
    else
      existing = reinterpret_cast<DWORD_PTR>(state);
  }
  if (existing)
    reinterpret_cast<CheckboxState*>(existing)->background = background;
}

inline void StyleGroupBox(HWND dialog, WORD id) {
  HWND control = ::GetDlgItem(dialog, id);
  DWORD_PTR existing = 0;
  if (control && !::GetWindowSubclass(control, GroupBoxProc, 9, &existing))
    ::SetWindowSubclass(control, GroupBoxProc, 9, 0);
}

inline void StyleSwitch(HWND dialog, WORD id) {
  EnsureGdiPlus();
  HWND control = ::GetDlgItem(dialog, id);
  if (!control)
    return;
  DWORD_PTR existing = 0;
  if (!::GetWindowSubclass(control, SwitchProc, 5, &existing)) {
    auto* state = new SwitchState;
    if (!::SetWindowSubclass(control, SwitchProc, 5,
                             reinterpret_cast<DWORD_PTR>(state)))
      delete state;
  }
}

inline void StyleCombo(HWND dialog, WORD id) {
  EnsureGdiPlus();
  HWND control = ::GetDlgItem(dialog, id);
  DWORD_PTR frame = 0;
  if (control && !::GetWindowSubclass(control, ComboProc, 7, &frame)) {
    auto* state = new ComboState;
    if (!::SetWindowSubclass(control, ComboProc, 7,
                             reinterpret_cast<DWORD_PTR>(state)))
      delete state;
  }
  if (control)
    RoundComboWindow(control);
  COMBOBOXINFO info{sizeof(info)};
  if (control && ::GetComboBoxInfo(control, &info) && info.hwndList) {
    ConfigureComboListWindow(info.hwndList);
    DWORD_PTR existing = 0;
    if (!::GetWindowSubclass(info.hwndList, ComboListProc, 6, &existing))
      ::SetWindowSubclass(info.hwndList, ComboListProc, 6,
                          reinterpret_cast<DWORD_PTR>(control));
  }
}

inline void PrepareCard(HWND dialog, WORD id) {
  HWND card = ::GetDlgItem(dialog, id);
  if (!card)
    return;
  LONG_PTR style = ::GetWindowLongPtrW(card, GWL_STYLE);
  style &= ~static_cast<LONG_PTR>(BS_TYPEMASK);
  // A card is a background sibling, not the parent of the controls placed on
  // it.  Clip those siblings and keep the card at the bottom of the z-order so
  // a pressed-state repaint cannot cover the card contents.
  style |= BS_OWNERDRAW | WS_CLIPSIBLINGS;
  ::SetWindowLongPtrW(card, GWL_STYLE, style);
  ::SetWindowPos(card, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  // Cards are decorative backgrounds.  Leaving the underlying owner-drawn
  // button enabled makes an empty-area click enter and leave the pressed state,
  // repainting a complex preview twice and producing a visible flash.
  ::EnableWindow(card, FALSE);
  StyleCard(dialog, id);
}

inline void DrawCard(const DRAWITEMSTRUCT& draw) {
  RECT bounds = draw.rcItem;
  ::FillRect(draw.hDC, &bounds, settings_theme::GetBrush(COLOR_BTNFACE));
  bounds.right -= 1;
  bounds.bottom -= 1;
  const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF border =
      Mix(settings_theme::GetColor(COLOR_3DSHADOW), surface, 76);
  HBRUSH brush = ::CreateSolidBrush(surface);
  HPEN pen = ::CreatePen(PS_SOLID, 1, border);
  const HGDIOBJ previous_brush = ::SelectObject(draw.hDC, brush);
  const HGDIOBJ previous_pen = ::SelectObject(draw.hDC, pen);
  const int radius = ::MulDiv(14, ::GetDeviceCaps(draw.hDC, LOGPIXELSX), 96);
  ::RoundRect(draw.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
              radius, radius);
  ::SelectObject(draw.hDC, previous_pen);
  ::SelectObject(draw.hDC, previous_brush);
  ::DeleteObject(pen);
  ::DeleteObject(brush);
}

inline void Install(HWND dialog, Page active, const InstallOptions& options) {
  DisableWindowTransitions(dialog);
  ResizeForSidebarFrame(dialog);
  ::SetWindowTextW(
      dialog,
      LocalText(L"小狼毫设置", L"小狼毫設定", L"Weasel settings").c_str());
  const int sidebar_width =
      MapDialogUnits(dialog, 0, 0, kSidebarWidthDlu, 0).right;
  DWORD_PTR sidebar_data = 0;
  if (!::GetWindowSubclass(dialog, SidebarProc, 4, &sidebar_data)) {
    auto* state =
        new SidebarState{sidebar_width, ::CreateSolidBrush(SidebarSurface())};
    if (!state->brush ||
        !::SetWindowSubclass(dialog, SidebarProc, 4,
                             reinterpret_cast<DWORD_PTR>(state))) {
      if (state->brush)
        ::DeleteObject(state->brush);
      delete state;
    }
  }
  struct Shift {
    HWND parent;
    int x;
  } shift{dialog, sidebar_width};
  ::EnumChildWindows(
      dialog,
      [](HWND child, LPARAM data) {
        const auto* shift = reinterpret_cast<const Shift*>(data);
        if (::GetParent(child) != shift->parent)
          return TRUE;
        RECT bounds{};
        ::GetWindowRect(child, &bounds);
        ::MapWindowPoints(HWND_DESKTOP, shift->parent,
                          reinterpret_cast<POINT*>(&bounds), 2);
        ::SetWindowPos(child, nullptr, bounds.left + shift->x, bounds.top, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&shift));
  RECT client{};
  ::GetClientRect(dialog, &client);
  const auto vertical = [&](int dlu) -> int {
    return static_cast<int>(MapDialogUnits(dialog, 0, 0, 0, dlu).bottom);
  };
  const int vertical_margin = vertical(7);
  const int horizontal_margin = sidebar_width / 16;
  const int margin =
      vertical_margin > horizontal_margin ? vertical_margin : horizontal_margin;
  const int content_width = sidebar_width - margin * 2;

  struct Entry {
    Page page;
    WORD id;
    const wchar_t* zh;
    const wchar_t* tw;
    const wchar_t* en;
  };
  const Entry entries[] = {
      {Page::Input, kInput, L"输入方案与语法模型", L"輸入方案與語法模型",
       L"Input methods and model"},
      {Page::Appearance, kAppearance, L"候选框 • 配色方案",
       L"候選框 • 配色方案", L"Candidate • Colors"},
      {Page::Fonts, kFonts, L"候选框 • 字体", L"候選框 • 字型",
       L"Candidate • Fonts"},
      {Page::StatusIcons, kStatusIcons, L"任务栏图标", L"工作列圖示",
       L"Taskbar icons"},
  };
  const int item_height = vertical(kNavigationItemHeightDlu);
  const int item_gap = vertical(kNavigationItemGapDlu);
  int top = margin + vertical(2);
  for (const auto& entry : entries) {
    HWND item =
        Create(dialog, L"BUTTON", LocalText(entry.zh, entry.tw, entry.en),
               BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, entry.id, margin, top,
               content_width, item_height);
    auto* state = new NavState{entry.page == active, false, false, entry.page};
    if (!item || !::SetWindowSubclass(item, NavProc, 1,
                                      reinterpret_cast<DWORD_PTR>(state)))
      delete state;
    top += item_height + item_gap;
  }
  HWND sidebar_divider = Create(dialog, L"STATIC", L"", 0, 0, sidebar_width - 1,
                                margin, 1, client.bottom - margin * 2);
  ::SetWindowSubclass(sidebar_divider, SidebarSeparatorProc, 8, 0);

  const int button_height = vertical(kButtonHeightDlu);
  const int action_button_width =
      MapDialogUnits(dialog, 0, 0, kActionButtonWidthDlu, 0).right;
  const int action_button_x = (sidebar_width - action_button_width) / 2;
  const int folder_link_height = vertical(kNavigationItemHeightDlu);
  const int folder_link_width = content_width;
  const int folder_link_x = margin;
  const int folder_link_y = client.bottom - margin - folder_link_height;
  const int close_y = folder_link_y - vertical(10) - button_height;
  const int apply_y = close_y - vertical(4) - button_height;
  const int separator_height = (std::max)(1, vertical(1));
  const int action_separator_y =
      apply_y - vertical(kSidebarSectionGapDlu) - separator_height;
  const int folder_separator_y =
      close_y + button_height + (folder_link_y - close_y - button_height) / 2;
  const auto create_sidebar_separator = [&](int y) {
    HWND separator = Create(dialog, L"STATIC", L"", 0, 0, margin, y,
                            content_width, separator_height);
    if (separator)
      ::SetWindowSubclass(separator, SidebarSeparatorProc, 8, 0);
  };
  create_sidebar_separator(action_separator_y);
  create_sidebar_separator(folder_separator_y);
  const int appearance_y = action_separator_y - vertical(6) - item_height;
  create_sidebar_separator(appearance_y - vertical(6) - separator_height);
  HWND appearance = Create(
      dialog, L"BUTTON",
      LocalText(L"设置界面颜色", L"設定介面色彩", L"Settings appearance"),
      BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, settings_theme::kNavigation, margin,
      appearance_y, content_width, item_height);
  auto* appearance_state = new NavState{false, false, false, Page::Input};
  if (!appearance ||
      !::SetWindowSubclass(appearance, NavProc, 1,
                           reinterpret_cast<DWORD_PTR>(appearance_state)))
    delete appearance_state;
  HWND folder =
      Create(dialog, L"BUTTON", options.user_folder,
             BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, kUserFolder, folder_link_x,
             folder_link_y, folder_link_width, folder_link_height);
  auto* folder_state = new NavState{false, false, true, Page::Input};
  if (!folder ||
      !::SetWindowSubclass(folder, NavProc, 1,
                           reinterpret_cast<DWORD_PTR>(folder_state)))
    delete folder_state;
  HWND apply = ::GetDlgItem(dialog, options.apply);
  if (apply) {
    ::SetWindowPos(apply, nullptr, action_button_x, apply_y,
                   action_button_width, button_height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetWindowTextW(apply, LocalText(L"应用", L"套用", L"Apply").c_str());
    StyleActionButton(dialog, options.apply, ToggleState::Background::Sidebar);
  }
  HWND close = ::GetDlgItem(dialog, options.close);
  if (!close)
    close = Create(dialog, L"BUTTON", LocalText(L"关闭", L"關閉", L"Close"),
                   BS_PUSHBUTTON | WS_TABSTOP, options.close, action_button_x,
                   close_y, action_button_width, button_height);
  else
    ::SetWindowPos(close, nullptr, action_button_x, close_y,
                   action_button_width, button_height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ::SetWindowTextW(close, LocalText(L"关闭", L"關閉", L"Close").c_str());
  StyleActionButton(dialog, options.close, ToggleState::Background::Sidebar);
  for (WORD id : options.hide)
    ::ShowWindow(::GetDlgItem(dialog, id), SW_HIDE);
}

inline bool OpenUserFolder(HWND dialog, const std::wstring& folder) {
  return reinterpret_cast<INT_PTR>(
             ::ShellExecuteW(dialog, L"open", folder.c_str(), nullptr, nullptr,
                             SW_SHOWNORMAL)) > 32;
}

}  // namespace settings_navigation
