#include "stdafx.h"
#include "WeaselTrayIcon.h"
#include <atlstr.h>
#include <WeaselUserSettings.h>
#include <WeaselMenu.h>

#include <algorithm>
#include <gdiplus.h>
#include <string>

#pragma comment(lib, "gdiplus.lib")

// nasty
#include <resource.h>

static UINT mode_icon[] = {IDI_ZH, IDI_ZH, IDI_EN, IDI_ZH, IDI_EN, IDI_RELOAD};
static const WCHAR* mode_label[] = {NULL,
                                    /*L"中文"*/ NULL,
                                    /*L"西文"*/ NULL,
                                    NULL,
                                    /*L"中文大写"*/ NULL,
                                    /*L"西文大写"*/ NULL,
                                    L"Under maintenance"};

namespace {
unsigned int LoadPackageUpdateCount() {
  constexpr wchar_t kRegistry[] = L"Software\\Rime\\Weasel\\PackageUpdates";
  DWORD count = 0;
  DWORD size = sizeof(count);
  if (::RegGetValueW(HKEY_CURRENT_USER, kRegistry, L"AvailableCount",
                     RRF_RT_REG_DWORD, nullptr, &count,
                     &size) != ERROR_SUCCESS) {
    return 0;
  }
  return (std::min)(count, 99ul);
}

std::wstring SettingsMenuText(unsigned int count) {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE) {
    return L"Settings (&S)\t" + std::to_wstring(count) + L" updates";
  }
  const WORD sublanguage = SUBLANGID(language);
  const bool simplified = sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
                          sublanguage == SUBLANG_CHINESE_SINGAPORE;
  return (simplified ? L"输入法设定 (&S)\t" : L"輸入法設定 (&S)\t") +
         std::to_wstring(count) + (simplified ? L" 项更新" : L" 項更新");
}

bool SetMenuCommandText(HMENU menu, UINT command, const std::wstring& text) {
  const int count = ::GetMenuItemCount(menu);
  for (int index = 0; index < count; ++index) {
    if (::GetMenuItemID(menu, index) == command) {
      MENUITEMINFOW item = {};
      item.cbSize = sizeof(item);
      item.fMask = MIIM_STRING;
      item.dwTypeData = const_cast<wchar_t*>(text.c_str());
      return ::SetMenuItemInfoW(menu, index, TRUE, &item) != FALSE;
    }
    if (HMENU child = ::GetSubMenu(menu, index)) {
      if (SetMenuCommandText(child, command, text))
        return true;
    }
  }
  return false;
}

bool CapsLockEnabled() {
  return (::GetKeyState(VK_CAPITAL) & 1) != 0;
}

HICON LoadIconFile(const std::wstring& path) {
  if (path.empty() ||
      ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return nullptr;
  }
  HICON icon = reinterpret_cast<HICON>(
      ::LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0,
                   LR_LOADFROMFILE | LR_DEFAULTSIZE));
  if (icon)
    return icon;
  Gdiplus::Bitmap image(path.c_str());
  if (image.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  return image.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
}

HICON LoadResourceIcon(UINT resource) {
  return reinterpret_cast<HICON>(
      ::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource),
                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
}

HICON LoadBaseIcon(const std::wstring& custom,
                   const std::wstring& schema,
                   UINT fallback) {
  if (HICON icon = LoadIconFile(custom))
    return icon;
  if (HICON icon = LoadIconFile(schema))
    return icon;
  return LoadResourceIcon(fallback);
}

HICON AddCapsBadge(HICON base, weasel::StatusIconCapsBadge badge) {
  if (!base)
    return nullptr;
  constexpr INT size = 32;
  Gdiplus::Bitmap canvas(size, size, PixelFormat32bppARGB);
  Gdiplus::Graphics graphics(&canvas);
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
  graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
  Gdiplus::Bitmap source(base);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.DrawImage(&source, Gdiplus::Rect(0, 0, size, size));
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  const Gdiplus::Color fill_color(255, 213, 67, 62);
  const Gdiplus::Color outline_color(235, 255, 255, 255);
  Gdiplus::SolidBrush fill(fill_color);
  Gdiplus::Pen outline(outline_color, 2.0f);
  if (badge == weasel::StatusIconCapsBadge::Dot) {
    const Gdiplus::RectF dot(21.0f, 2.0f, 9.0f, 9.0f);
    graphics.FillEllipse(&fill, dot);
    graphics.DrawEllipse(&outline, dot);
  } else {
    const Gdiplus::RectF circle(16.0f, 0.0f, 16.0f, 16.0f);
    graphics.FillEllipse(&fill, circle);
    graphics.DrawEllipse(&outline, circle);
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleBold,
                       Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(L"A", 1, &font, circle, &format, &text);
  }
  HICON result = nullptr;
  return canvas.GetHICON(&result) == Gdiplus::Ok ? result : nullptr;
}
}  // namespace

WeaselTrayIcon::WeaselTrayIcon(weasel::UI& ui)
    : m_style(ui.style()),
      m_status(ui.status()),
      m_mode(INITIAL),
      m_schema_zhung_icon(),
      m_schema_ascii_icon(),
      m_disabled(false) {
  Gdiplus::GdiplusStartupInput startup;
  if (Gdiplus::GdiplusStartup(&m_graphics_token, &startup, nullptr) !=
      Gdiplus::Ok) {
    m_graphics_token = 0;
  }
}

WeaselTrayIcon::~WeaselTrayIcon() {
  if (m_graphics_token)
    Gdiplus::GdiplusShutdown(m_graphics_token);
}

void WeaselTrayIcon::CustomizeMenu(HMENU hMenu) {
  weasel::SetMenuCommandChecked(hMenu, ID_WEASELTRAY_ACRYLIC,
                                weasel::UserSettings::Load().acrylic);
  const auto update_count = LoadPackageUpdateCount();
  if (update_count)
    SetMenuCommandText(hMenu, ID_WEASELTRAY_SETTINGS,
                       SettingsMenuText(update_count));
}

BOOL WeaselTrayIcon::Create(HWND hTargetWnd) {
  HMODULE hModule = GetModuleHandle(NULL);
  CIcon icon;
  icon.LoadIconW(IDI_ZH);
  BOOL bRet =
      CSystemTray::Create(hModule, NULL, WM_WEASEL_TRAY_NOTIFY,
                          get_weasel_ime_name().c_str(), icon, IDR_MENU_POPUP);
  if (hTargetWnd) {
    SetTargetWnd(hTargetWnd);
  }
  if (!m_style.display_tray_icon) {
    RemoveIcon();
  } else {
    AddIcon();
  }
  return bRet;
}

void WeaselTrayIcon::RequestRefresh() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_refresh_enabled) {
    return;
  }
  m_pending_state = WeaselTrayIconState::From(m_style, m_status);
  if (m_refresh_pending) {
    return;
  }
  m_refresh_pending = true;
  if (!::PostMessage(GetTargetWnd(), WM_WEASEL_SERVICE_NOTIFY, 0, 0)) {
    m_refresh_pending = false;
  }
}

void WeaselTrayIcon::ApplyRefresh() {
  WeaselTrayIconState state;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_refresh_pending || !m_refresh_enabled) {
      return;
    }
    state = m_pending_state;
    state.caps_lock = CapsLockEnabled();
    m_refresh_pending = false;
    m_refresh_in_progress = true;
  }
  Refresh(state);
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_refresh_in_progress = false;
  }
  m_state_cv.notify_all();
}

void WeaselTrayIcon::ReloadSettings() {
  if (!m_last_state.valid)
    return;
  auto state = m_last_state;
  state.caps_lock = CapsLockEnabled();
  Refresh(state, true);
}

void WeaselTrayIcon::PollSystemState() {
  if (!m_last_state.valid)
    return;
  const bool caps_lock = CapsLockEnabled();
  if (caps_lock == m_last_state.caps_lock)
    return;
  auto state = m_last_state;
  state.caps_lock = caps_lock;
  Refresh(state);
}

void WeaselTrayIcon::DisableRefresh() {
  std::unique_lock<std::mutex> lock(m_state_mutex);
  m_refresh_enabled = false;
  m_refresh_pending = false;
  m_state_cv.wait(lock, [this] { return !m_refresh_in_progress; });
}

void WeaselTrayIcon::Refresh(const WeaselTrayIconState& state, bool force) {
  m_last_state = state;
  if (!state.display_tray_icon &&
      !state.disabled)  // display notification when deploying
  {
    if (m_mode != INITIAL) {
      RemoveIcon();
      m_mode = INITIAL;
    }
    m_disabled = false;
    return;
  }
  WeaselTrayMode mode = state.disabled ? DISABLED
                        : state.ascii_mode
                            ? (state.caps_lock ? ASCII_CAPS : ASCII)
                            : (state.caps_lock ? ZHUNG_CAPS : ZHUNG);
  /* change icon, when
          1,mode changed
          2,icon changed
          3,both m_schema_zhung_icon and state.current_zhung_icon empty(for
     initialize) 4,both m_schema_ascii_icon and state.current_ascii_icon
     empty(for initialize)
  */
  if (force || mode != m_mode ||
      m_schema_zhung_icon != state.current_zhung_icon ||
      (m_schema_zhung_icon.empty() && state.current_zhung_icon.empty()) ||
      m_schema_ascii_icon != state.current_ascii_icon ||
      (m_schema_ascii_icon.empty() && state.current_ascii_icon.empty())) {
    ShowIcon();
    m_mode = mode;
    m_schema_zhung_icon = state.current_zhung_icon;
    m_schema_ascii_icon = state.current_ascii_icon;
    const auto icons = weasel::StatusIconSettings::Load();
    HICON icon = nullptr;
    if (mode == ASCII) {
      icon = LoadBaseIcon(icons.english, m_schema_ascii_icon, IDI_EN);
    } else if (mode == ZHUNG) {
      icon = LoadBaseIcon(icons.chinese, m_schema_zhung_icon, IDI_ZH);
    } else if (mode == ASCII_CAPS) {
      if (icons.caps_mode == weasel::StatusIconCapsMode::Custom)
        icon = LoadIconFile(icons.english_caps);
      if (!icon) {
        HICON base = LoadBaseIcon(icons.english, m_schema_ascii_icon, IDI_EN);
        icon = AddCapsBadge(base, icons.caps_badge);
        if (base)
          ::DestroyIcon(base);
      }
    } else if (mode == ZHUNG_CAPS) {
      if (icons.caps_mode == weasel::StatusIconCapsMode::Custom)
        icon = LoadIconFile(icons.chinese_caps);
      if (!icon) {
        HICON base = LoadBaseIcon(icons.chinese, m_schema_zhung_icon, IDI_ZH);
        icon = AddCapsBadge(base, icons.caps_badge);
        if (base)
          ::DestroyIcon(base);
      }
    }
    if (icon) {
      SetIcon(icon);
      ::DestroyIcon(icon);
    } else {
      SetIcon(mode_icon[mode]);
    }

    if (mode_label[mode] && m_disabled == false) {
      CString info;
      info.LoadStringW(IDS_STR_UNDER_MAINTENANCE);
      ShowBalloon(info, get_weasel_ime_name().c_str());
      m_disabled = true;
    }
    if (m_mode != DISABLED)
      m_disabled = false;
  } else if (!Visible()) {
    ShowIcon();
  }
}
