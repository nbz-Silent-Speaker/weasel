#pragma once

#include <shellapi.h>
#include <windows.h>

#include <string>

namespace settings_navigation {

enum class Page { Input, Appearance, Fonts, StatusIcons };

inline constexpr WORD kInput = 30001;
inline constexpr WORD kAppearance = 30002;
inline constexpr WORD kFonts = 30003;
inline constexpr WORD kStatusIcons = 30004;

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
  switch (command) {
    case kAppearance:
      return Page::Appearance;
    case kFonts:
      return Page::Fonts;
    case kStatusIcons:
      return Page::StatusIcons;
    default:
      return Page::Input;
  }
}

inline WORD CommandFromPage(Page page) {
  switch (page) {
    case Page::Appearance:
      return kAppearance;
    case Page::Fonts:
      return kFonts;
    case Page::StatusIcons:
      return kStatusIcons;
    default:
      return kInput;
  }
}

inline RECT MapDialogUnits(HWND dialog,
                           int left,
                           int top,
                           int width,
                           int height) {
  RECT rectangle = {left, top, left + width, top + height};
  ::MapDialogRect(dialog, &rectangle);
  return rectangle;
}

inline void Install(HWND dialog, Page active) {
  constexpr int sidebar_width_dlu = 112;
  const RECT offset = MapDialogUnits(dialog, 0, 0, sidebar_width_dlu, 0);
  const int offset_x = offset.right;

  struct ShiftContext {
    HWND dialog;
    int offset_x;
  } shift = {dialog, offset_x};
  ::EnumChildWindows(
      dialog,
      [](HWND child, LPARAM parameter) {
        const auto* shift = reinterpret_cast<const ShiftContext*>(parameter);
        if (::GetParent(child) != shift->dialog)
          return TRUE;
        RECT rectangle = {};
        ::GetWindowRect(child, &rectangle);
        ::MapWindowPoints(HWND_DESKTOP, shift->dialog,
                          reinterpret_cast<POINT*>(&rectangle), 2);
        ::SetWindowPos(child, nullptr, rectangle.left + shift->offset_x,
                       rectangle.top, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&shift));

  RECT window = {};
  RECT client = {};
  ::GetWindowRect(dialog, &window);
  ::GetClientRect(dialog, &client);
  ::SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left + offset_x,
                 window.bottom - window.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  const HFONT font =
      reinterpret_cast<HFONT>(::SendMessage(dialog, WM_GETFONT, 0, 0));
  const auto create = [&](const wchar_t* class_name, const std::wstring& text,
                          DWORD style, WORD id, int left, int top, int width,
                          int height) {
    const RECT rectangle = MapDialogUnits(dialog, left, top, width, height);
    HWND control = ::CreateWindowExW(
        0, class_name, text.c_str(), WS_CHILD | WS_VISIBLE | style,
        rectangle.left, rectangle.top, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, dialog,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        ::GetModuleHandleW(nullptr), nullptr);
    if (control && font)
      ::SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
  };

  create(L"STATIC", LocalText(L"设置", L"設定", L"Settings"), SS_LEFT, 0, 14,
         12, 88, 14);
  struct Entry {
    Page page;
    WORD command;
    const wchar_t* simplified;
    const wchar_t* traditional;
    const wchar_t* english;
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
  int top = 34;
  for (const auto& entry : entries) {
    DWORD style = BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP;
    if (entry.page == Page::Input)
      style |= WS_GROUP;
    HWND button =
        create(L"BUTTON",
               LocalText(entry.simplified, entry.traditional, entry.english),
               style, entry.command, 10, top, 92, 22);
    if (button)
      ::SendMessage(button, BM_SETCHECK,
                    entry.page == active ? BST_CHECKED : BST_UNCHECKED, 0);
    top += 26;
  }

  const RECT divider = MapDialogUnits(dialog, 108, 8, 1, 0);
  ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT,
                    divider.left, divider.top, 1,
                    client.bottom - divider.top * 2, dialog, nullptr,
                    ::GetModuleHandleW(nullptr), nullptr);
}

inline bool Launch(Page page) {
  wchar_t executable[MAX_PATH] = {};
  if (!::GetModuleFileNameW(nullptr, executable, MAX_PATH))
    return false;
  const wchar_t* arguments = L"";
  switch (page) {
    case Page::Appearance:
      arguments = L"/settings";
      break;
    case Page::Fonts:
      arguments = L"/fonts";
      break;
    case Page::StatusIcons:
      arguments = L"/status-icons";
      break;
    default:
      break;
  }
  return reinterpret_cast<INT_PTR>(::ShellExecuteW(
             nullptr, L"open", executable, arguments, nullptr, SW_SHOWNORMAL)) >
         32;
}

}  // namespace settings_navigation
