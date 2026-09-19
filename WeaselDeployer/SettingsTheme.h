#pragma once

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <WeaselUserSettings.h>
#include "SettingsColor.h"
#include <map>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace settings_theme {
inline constexpr UINT kChanged = WM_APP + 0x535;
inline constexpr UINT kOpen = WM_APP + 0x536;
inline constexpr WPARAM kAccentOnly = 1;
inline constexpr WORD kNavigation = 30006;
enum class Mode { System, Light, Dark };
enum class Accent { System, Custom, Default };
struct Preferences {
  Mode mode = Mode::System;
  Accent accent = Accent::Default;
  settings_color::Color custom;
};
inline Preferences Load() {
  const auto text =
      weasel::UserSettingsStore().ReadString(L"SettingsUiAppearance");
  std::wistringstream input(text);
  int mode = -1, accent = -1;
  std::wstring color;
  if (input >> mode >> accent >> color) {
    const auto parsed = settings_color::Parse(color);
    if (mode >= 0 && mode <= 2 && accent >= 0 && accent <= 2 && parsed)
      return {static_cast<Mode>(mode), static_cast<Accent>(accent), *parsed};
  }
  return {};
}
inline Preferences& Current() {
  static Preferences preferences = Load();
  return preferences;
}
inline LSTATUS Save() {
  const auto& p = Current();
  return weasel::UserSettingsStore().WriteString(
      L"SettingsUiAppearance", std::to_wstring(static_cast<int>(p.mode)) +
                                   L" " +
                                   std::to_wstring(static_cast<int>(p.accent)) +
                                   L" " + settings_color::Hex(p.custom));
}
inline COLORREF Ref(settings_color::Color c) {
  return RGB(c.r, c.g, c.b);
}
inline settings_color::Color Color(COLORREF c) {
  return {GetRValue(c), GetGValue(c), GetBValue(c)};
}
struct Palette {
  bool dark = false;
  COLORREF accent = RGB(10, 157, 161);
  std::map<int, HBRUSH> brushes;
  ~Palette() {
    for (const auto& item : brushes)
      ::DeleteObject(item.second);
  }
};
inline Palette& Colors() {
  static Palette colors;
  return colors;
}
inline void Refresh() {
  auto& colors = Colors();
  const auto& p = Current();
  weasel::UserSettingsStore personalization(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
  colors.dark = p.mode == Mode::Dark ||
                (p.mode == Mode::System &&
                 !personalization.ReadDword(L"AppsUseLightTheme", 1));
  if (p.accent == Accent::System) {
    weasel::UserSettingsStore dwm(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\DWM");
    const DWORD system_accent = dwm.ReadDword(L"AccentColor", 0xffffffff);
    colors.accent = system_accent & 0xffffff;
    if (system_accent == 0xffffffff) {
      DWORD argb = 0;
      BOOL opaque = FALSE;
      if (SUCCEEDED(::DwmGetColorizationColor(&argb, &opaque)))
        colors.accent = RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
      else
        colors.accent = ::GetSysColor(COLOR_HIGHLIGHT);
    }
  } else
    colors.accent =
        Ref(p.accent == Accent::Custom ? p.custom : settings_color::Color{});
  // Brushes are updated in place only between paints on the UI thread.
  for (const auto& item : colors.brushes)
    ::DeleteObject(item.second);
  colors.brushes.clear();
}
inline COLORREF GetColor(int index) {
  const auto& p = Colors();
  switch (index) {
    case COLOR_HIGHLIGHT:
    case COLOR_HOTLIGHT:
      return p.accent;
    case COLOR_HIGHLIGHTTEXT:
      return settings_color::DarkText(Color(p.accent)) ? RGB(0, 0, 0)
                                                       : RGB(255, 255, 255);
    case COLOR_WINDOW:
      return p.dark ? RGB(43, 43, 43) : RGB(255, 255, 255);
    case COLOR_BTNFACE:
      return p.dark ? RGB(32, 32, 32) : RGB(240, 240, 240);
    case COLOR_WINDOWTEXT:
    case COLOR_BTNTEXT:
      return p.dark ? RGB(245, 245, 245) : RGB(20, 20, 20);
    case COLOR_GRAYTEXT:
      return p.dark ? RGB(170, 170, 170) : RGB(108, 108, 108);
    case COLOR_3DSHADOW:
      return p.dark ? RGB(142, 142, 142) : RGB(160, 160, 160);
    default:
      return ::GetSysColor(index);
  }
}
inline HBRUSH GetBrush(int index) {
  auto& brush = Colors().brushes[index];
  if (!brush)
    brush = ::CreateSolidBrush(GetColor(index));
  return brush;
}
inline void ApplyNative(HWND window) {
  if (::GetPropW(window, L"Weasel.SettingsCustomEdit"))
    return;  // The picker owns its edit frame, padding and light/dark colors.
  wchar_t name[64]{};
  ::GetClassNameW(window, name, _countof(name));
  if (_wcsicmp(name, WC_LISTVIEWW) == 0) {
    ListView_SetBkColor(window, GetColor(COLOR_WINDOW));
    ListView_SetTextBkColor(window, GetColor(COLOR_WINDOW));
    ListView_SetTextColor(window, GetColor(COLOR_WINDOWTEXT));
  }
  if (_wcsicmp(name, L"Edit") == 0 || _wcsicmp(name, WC_LISTVIEWW) == 0 ||
      _wcsicmp(name, L"ListBox") == 0 || _wcsicmp(name, L"ComboBox") == 0) {
    const auto theme =
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(Colors().dark ? 2 : 1));
    if (::GetPropW(window, L"Weasel.SettingsTheme") != theme) {
      ::SetPropW(window, L"Weasel.SettingsTheme", theme);
      ::SetWindowTheme(
          window, Colors().dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
  }
}
enum class UpdateKind { Full, AccentOnly };

inline UINT RedrawFlags(UpdateKind kind) {
  return RDW_INVALIDATE | RDW_ALLCHILDREN |
         (kind == UpdateKind::Full ? RDW_ERASE : 0);
}

inline void Update(HWND host, UpdateKind kind = UpdateKind::Full) {
  static bool updating = false;
  if (updating)
    return;
  updating = true;
  Refresh();
  const bool full = kind == UpdateKind::Full;
  if (full) {
    BOOL dark = Colors().dark;
    ::DwmSetWindowAttribute(host, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                            sizeof(dark));
    // Keep the native Windows 11 frame, but make its border follow the
    // explicitly selected settings mode rather than the system mode.
    constexpr DWORD kWindowBorderColor = 34;  // DWMWA_BORDER_COLOR
    const COLORREF frame_border =
        Colors().dark ? RGB(63, 63, 63) : RGB(210, 210, 210);
    ::DwmSetWindowAttribute(host, kWindowBorderColor, &frame_border,
                            sizeof(frame_border));
  }
  const WPARAM change = full ? 0 : kAccentOnly;
  ::SendMessageW(host, kChanged, change, 0);
  ::EnumChildWindows(
      host,
      [](HWND child, LPARAM parameter) {
        const bool full = parameter == 0;
        if (full)
          ApplyNative(child);
        ::SendMessageW(child, kChanged, full ? 0 : kAccentOnly, 0);
        return TRUE;
      },
      full ? 0 : 1);
  ::RedrawWindow(host, nullptr, nullptr, RedrawFlags(kind));
  updating = false;
}
}  // namespace settings_theme
