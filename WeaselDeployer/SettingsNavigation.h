#pragma once

#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace settings_navigation {

enum class Page { Input, Appearance, Fonts, StatusIcons };
inline constexpr WORD kInput = 30001;
inline constexpr WORD kAppearance = 30002;
inline constexpr WORD kFonts = 30003;
inline constexpr WORD kStatusIcons = 30004;
inline constexpr WORD kUserFolder = 30005;
inline constexpr WORD kStatus = 30006;

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

inline bool IsPageResult(INT_PTR result) {
  return result >= kInput && result <= kStatusIcons;
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

inline COLORREF Mix(COLORREF foreground, COLORREF background, int alpha) {
  const auto mix = [alpha](int first, int second) {
    return (first * alpha + second * (255 - alpha)) / 255;
  };
  return RGB(mix(GetRValue(foreground), GetRValue(background)),
             mix(GetGValue(foreground), GetGValue(background)),
             mix(GetBValue(foreground), GetBValue(background)));
}

struct NavState {
  bool active;
  bool hover;
  bool link;
};

inline LRESULT CALLBACK NavProc(HWND window,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam,
                                UINT_PTR,
                                DWORD_PTR data) {
  auto* state = reinterpret_cast<NavState*>(data);
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
  } else if (message == WM_LBUTTONUP && state->link) {
    wchar_t folder[MAX_PATH]{};
    ::GetWindowTextW(window, folder, MAX_PATH);
    ::ShellExecuteW(::GetParent(window), L"open", folder, nullptr, nullptr,
                    SW_SHOWNORMAL);
    return 0;
  } else if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(window, &paint);
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    const COLORREF surface = ::GetSysColor(COLOR_BTNFACE);
    const COLORREF accent = ::GetSysColor(COLOR_HIGHLIGHT);
    const COLORREF fill =
        state->active ? Mix(accent, surface, 38)
                      : (state->hover ? Mix(accent, surface, 16) : surface);
    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_NULL, 0, fill);
    const HGDIOBJ old_brush = ::SelectObject(dc, brush);
    const HGDIOBJ old_pen = ::SelectObject(dc, pen);
    ::RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, 12,
                12);
    ::SelectObject(dc, old_pen);
    ::SelectObject(dc, old_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
    if (state->active) {
      RECT marker{bounds.left + 2, bounds.top + 8, bounds.left + 6,
                  bounds.bottom - 8};
      HBRUSH marker_brush = ::CreateSolidBrush(accent);
      ::FillRect(dc, &marker, marker_brush);
      ::DeleteObject(marker_brush);
    }
    wchar_t text[256]{};
    ::GetWindowTextW(window, text, 256);
    HFONT font =
        reinterpret_cast<HFONT>(::SendMessage(window, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(dc, font) : nullptr;
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, state->link ? accent : ::GetSysColor(COLOR_BTNTEXT));
    bounds.left += state->link ? 2 : 18;
    ::DrawTextW(dc, text, -1, &bounds,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old_font)
      ::SelectObject(dc, old_font);
    ::EndPaint(window, &paint);
    return 0;
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, NavProc, 1);
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

inline void Round(HWND control, int radius = 12) {
  if (!control)
    return;
  RECT bounds{};
  ::GetClientRect(control, &bounds);
  ::SetWindowRgn(control,
                 ::CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1,
                                      bounds.bottom + 1, radius, radius),
                 TRUE);
}

inline void Install(HWND dialog, Page active, const InstallOptions& options) {
  constexpr int width_dlu = 126;
  const int sidebar_width = MapDialogUnits(dialog, 0, 0, width_dlu, 0).right;
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
  RECT window{};
  ::GetWindowRect(dialog, &window);
  ::SetWindowPos(
      dialog, nullptr, 0, 0, window.right - window.left + sidebar_width,
      window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  RECT client{};
  ::GetClientRect(dialog, &client);
  const auto vertical = [&](int dlu) {
    return MapDialogUnits(dialog, 0, 0, 0, dlu).bottom;
  };
  const int margin = std::max(vertical(7), sidebar_width / 15);
  const int content_width = sidebar_width - margin * 2;
  Create(dialog, L"STATIC",
         LocalText(L"小狼毫设置", L"小狼毫設定", L"Weasel settings"), SS_LEFT,
         0, margin, margin, content_width, vertical(14));

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
      {Page::Appearance, kAppearance, L"候选框", L"候選框",
       L"Candidate window"},
      {Page::Fonts, kFonts, L"字体", L"字型", L"Fonts"},
      {Page::StatusIcons, kStatusIcons, L"状态图标", L"狀態圖示",
       L"Status icons"},
  };
  const int item_height = vertical(22);
  const int item_gap = vertical(4);
  int top = margin + vertical(22);
  for (const auto& entry : entries) {
    HWND item =
        Create(dialog, L"BUTTON", LocalText(entry.zh, entry.tw, entry.en),
               BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, entry.id, margin, top,
               content_width, item_height);
    ::SetWindowSubclass(item, NavProc, 1,
                        reinterpret_cast<DWORD_PTR>(
                            new NavState{entry.page == active, false, false}));
    top += item_height + item_gap;
  }
  Create(dialog, L"STATIC", L"", SS_ETCHEDVERT, 0, sidebar_width - 1, margin, 1,
         client.bottom - margin * 2);

  const int button_height = vertical(21);
  const int close_y = client.bottom - margin - button_height;
  const int apply_y = close_y - vertical(25);
  const int status_y = apply_y - vertical(17);
  const int folder_y = status_y - vertical(30);
  Create(dialog, L"STATIC",
         LocalText(L"用户文件夹", L"使用者資料夾", L"User folder"), SS_LEFT, 0,
         margin, folder_y, content_width, vertical(9));
  HWND folder =
      Create(dialog, L"BUTTON", options.user_folder,
             BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, kUserFolder, margin,
             folder_y + vertical(10), content_width, vertical(14));
  ::SetWindowSubclass(
      folder, NavProc, 1,
      reinterpret_cast<DWORD_PTR>(new NavState{false, false, true}));
  Create(dialog, L"STATIC", L"", SS_CENTER, kStatus, margin, status_y,
         content_width, vertical(10));

  HWND apply = ::GetDlgItem(dialog, options.apply);
  if (apply) {
    ::SetWindowPos(apply, nullptr, margin, apply_y, content_width,
                   button_height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetWindowTextW(apply, LocalText(L"应用", L"套用", L"Apply").c_str());
    Round(apply);
  }
  HWND close = ::GetDlgItem(dialog, options.close);
  if (!close)
    close = Create(dialog, L"BUTTON", LocalText(L"关闭", L"關閉", L"Close"),
                   BS_PUSHBUTTON | WS_TABSTOP, options.close, margin, close_y,
                   content_width, button_height);
  else
    ::SetWindowPos(close, nullptr, margin, close_y, content_width,
                   button_height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ::SetWindowTextW(close, LocalText(L"关闭", L"關閉", L"Close").c_str());
  Round(close);
  for (WORD id : options.hide)
    ::ShowWindow(::GetDlgItem(dialog, id), SW_HIDE);
}

inline void SetStatus(HWND dialog, const std::wstring& text) {
  ::SetDlgItemTextW(dialog, kStatus, text.c_str());
}

inline bool OpenUserFolder(HWND dialog, const std::wstring& folder) {
  return reinterpret_cast<INT_PTR>(
             ::ShellExecuteW(dialog, L"open", folder.c_str(), nullptr, nullptr,
                             SW_SHOWNORMAL)) > 32;
}

}  // namespace settings_navigation
