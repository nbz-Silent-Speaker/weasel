#include "stdafx.h"
#include "WeaselTrayIcon.h"
#include <atlstr.h>
#include <WeaselUserSettings.h>
#include <WeaselMenu.h>

#include <algorithm>
#include <string>

// nasty
#include <resource.h>

static UINT mode_icon[] = {IDI_ZH, IDI_ZH, IDI_EN, IDI_RELOAD};
static const WCHAR* mode_label[] = {NULL, /*L"中文"*/ NULL, /*L"西文"*/ NULL,
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
}  // namespace

WeaselTrayIcon::WeaselTrayIcon(weasel::UI& ui)
    : m_style(ui.style()),
      m_status(ui.status()),
      m_mode(INITIAL),
      m_schema_zhung_icon(),
      m_schema_ascii_icon(),
      m_disabled(false) {}

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

void WeaselTrayIcon::DisableRefresh() {
  std::unique_lock<std::mutex> lock(m_state_mutex);
  m_refresh_enabled = false;
  m_refresh_pending = false;
  m_state_cv.wait(lock, [this] { return !m_refresh_in_progress; });
}

void WeaselTrayIcon::Refresh(const WeaselTrayIconState& state) {
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
  WeaselTrayMode mode = state.disabled     ? DISABLED
                        : state.ascii_mode ? ASCII
                                           : ZHUNG;
  /* change icon, when
          1,mode changed
          2,icon changed
          3,both m_schema_zhung_icon and state.current_zhung_icon empty(for
     initialize) 4,both m_schema_ascii_icon and state.current_ascii_icon
     empty(for initialize)
  */
  if (mode != m_mode || m_schema_zhung_icon != state.current_zhung_icon ||
      (m_schema_zhung_icon.empty() && state.current_zhung_icon.empty()) ||
      m_schema_ascii_icon != state.current_ascii_icon ||
      (m_schema_ascii_icon.empty() && state.current_ascii_icon.empty())) {
    ShowIcon();
    m_mode = mode;
    m_schema_zhung_icon = state.current_zhung_icon;
    m_schema_ascii_icon = state.current_ascii_icon;
    if (mode == ASCII) {
      if (m_schema_ascii_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_ascii_icon.c_str());
    } else if (mode == ZHUNG) {
      if (m_schema_zhung_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_zhung_icon.c_str());
    } else
      SetIcon(mode_icon[mode]);

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
