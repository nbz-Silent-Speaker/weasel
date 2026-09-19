#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <atlbase.h>
#include <atlwin.h>
#include "../SettingsAppearancePopup.h"
#include <cstdlib>
#include <iostream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

void Check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
int main() {
  INITCOMMONCONTROLSEX init{sizeof(init),
                            ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  ::InitCommonControlsEx(&init);
  settings_navigation::EnsureGdiPlus();
  settings_theme::Refresh();
  HWND host = ::CreateWindowExW(0, L"STATIC", L"Appearance test", WS_POPUP, 0,
                                0, 1000, 800, nullptr, nullptr,
                                ::GetModuleHandleW(nullptr), nullptr);
  Check(host != nullptr, "Host creation");
  Check((settings_theme::RedrawFlags(settings_theme::UpdateKind::AccentOnly) &
         RDW_ERASE) == 0,
        "Accent-only update erases the settings page");
  Check((settings_theme::RedrawFlags(settings_theme::UpdateKind::Full) &
         RDW_ERASE) != 0,
        "Full theme update no longer erases stale surfaces");
  int changes = 0;
  settings_color::Color last;
  settings_color::Picker picker;
  HWND panel = picker.Create(
      host, reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), 0, 0,
      420, 96, [&](auto value) {
        last = value;
        ++changes;
      });
  Check(panel != nullptr, "Picker creation");
  HWND code = ::GetDlgItem(panel, 40103), model = ::GetDlgItem(panel, 40104);
  HWND field = ::GetDlgItem(panel, 40110), plane = ::GetDlgItem(panel, 40101);
  RECT code_bounds{}, model_bounds{};
  ::GetWindowRect(code, &code_bounds);
  ::GetWindowRect(model, &model_bounds);
  Check(code_bounds.top == model_bounds.top,
        "Color code and model are not on one row");
  picker.Set({}, true);
  RECT initial{};
  ::GetWindowRect(panel, &initial);
  for (int mode = 0; mode < 3; ++mode) {
    picker.Set({}, mode == 1);
    RECT current{};
    ::GetWindowRect(panel, &current);
    Check(::EqualRect(&initial, &current) != FALSE, "Accent mode moved picker");
    Check(((::GetWindowLongPtrW(plane, GWL_STYLE) & WS_VISIBLE) != 0) ==
              (mode == 1),
          "Picker visibility");
    Check(((::GetWindowLongPtrW(code, GWL_STYLE) & ES_READONLY) != 0) ==
              (mode != 1),
          "Readonly field");
  }
  picker.Set({}, true);
  Check((::GetWindowLongPtrW(field, GWL_STYLE) & WS_VISIBLE) == 0,
        "Duplicate HEX component field is visible");
  ::SetWindowTextW(code, L"#ff0000");
  Check(last == settings_color::Color{255, 0, 0}, "Live code entry");
  int before = changes;
  for (int type = 0; type < 5; ++type) {
    ::SendMessageW(model, CB_SETCURSEL, type, 0);
    ::SendMessageW(panel, WM_COMMAND, MAKEWPARAM(40104, CBN_SELCHANGE),
                   reinterpret_cast<LPARAM>(model));
    Check(changes == before, "Changing representation changed color");
  }
  ::SendMessageW(model, CB_SETCURSEL, 1, 0);
  ::SendMessageW(panel, WM_COMMAND, MAKEWPARAM(40104, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(model));
  Check((::GetWindowLongPtrW(field, GWL_STYLE) & WS_VISIBLE) != 0,
        "RGB component fields are hidden");
  ::SetWindowTextW(::GetDlgItem(panel, 40110), L"0");
  ::SetWindowTextW(::GetDlgItem(panel, 40111), L"255");
  ::SetWindowTextW(::GetDlgItem(panel, 40112), L"0");
  Check(last == settings_color::Color{0, 255, 0}, "RGB fields not connected");
  before = changes;
  ::SetWindowTextW(field, L"#xx");
  Check(changes == before, "Invalid entry changed color");
  ::SetWindowTextW(field, L"10");
  RECT bounds{};
  ::GetClientRect(field, &bounds);
  HRGN region = ::CreateRectRgn(0, 0, 0, 0);
  ::GetWindowRgn(field, region);
  RECT clipped{};
  ::GetRgnBox(region, &clipped);
  ::DeleteObject(region);
  Check(clipped.right >= bounds.right - 2,
        "Component field has stale clip after model switch");
  for (auto mode : {settings_theme::Mode::Light, settings_theme::Mode::Dark}) {
    settings_theme::Current().mode = mode;
    settings_theme::Current().accent = settings_theme::Accent::Default;
    settings_theme::Update(host);
    Check(settings_theme::Colors().dark == (mode == settings_theme::Mode::Dark),
          "Forced theme");
    Check(settings_theme::GetColor(COLOR_HIGHLIGHT) == RGB(10, 157, 161),
          "Default accent");
    Check(settings_theme::GetColor(COLOR_HIGHLIGHTTEXT) == RGB(255, 255, 255),
          "Default text contrast");
    Check(settings_navigation::ControlBackground(
              settings_navigation::ToggleState::Background::ButtonFace) ==
              settings_theme::GetColor(COLOR_BTNFACE),
          "Button-face control background");
    Check(settings_navigation::ControlBackground(
              settings_navigation::ToggleState::Background::Sidebar) ==
              settings_navigation::SidebarSurface(),
          "Sidebar control background");
  }
  HWND sidebar_button = settings_navigation::Create(
      host, L"BUTTON", L"Apply", BS_PUSHBUTTON, 49901, 0, 0, 100, 30);
  settings_navigation::StyleActionButton(
      host, 49901, settings_navigation::ToggleState::Background::Sidebar);
  DWORD_PTR button_state = 0;
  Check(::GetWindowSubclass(sidebar_button, settings_navigation::ToggleProc, 2,
                            &button_state) != FALSE &&
            reinterpret_cast<settings_navigation::ToggleState*>(button_state)
                    ->background ==
                settings_navigation::ToggleState::Background::Sidebar,
        "Sidebar action button retained the wrong corner background");
  HWND modal_checkbox = settings_navigation::Create(
      host, L"BUTTON", L"Update", BS_AUTOCHECKBOX, 49902, 0, 40, 100, 30);
  settings_navigation::StyleCheckbox(
      host, 49902, settings_navigation::ToggleState::Background::ButtonFace);
  DWORD_PTR checkbox_state = 0;
  Check(
      ::GetWindowSubclass(modal_checkbox, settings_navigation::CheckboxProc, 3,
                          &checkbox_state) != FALSE &&
          reinterpret_cast<settings_navigation::CheckboxState*>(checkbox_state)
                  ->background ==
              settings_navigation::ToggleState::Background::ButtonFace,
      "Update checkbox retained the wrong dialog background");
  HWND modal_button =
      settings_navigation::Create(host, L"BUTTON", L"Update selected",
                                  BS_PUSHBUTTON, 49903, 0, 80, 140, 30);
  settings_navigation::StyleActionButton(
      host, 49903, settings_navigation::ToggleState::Background::ButtonFace);
  DWORD_PTR modal_button_state = 0;
  Check(::GetWindowSubclass(modal_button, settings_navigation::ToggleProc, 2,
                            &modal_button_state) != FALSE &&
            reinterpret_cast<settings_navigation::ToggleState*>(
                modal_button_state)
                    ->background ==
                settings_navigation::ToggleState::Background::ButtonFace,
        "Update action button retained the wrong dialog background");
  HWND modal_group = settings_navigation::Create(
      host, L"BUTTON", L"Input schema", BS_GROUPBOX, 49904, 0, 120, 180, 70);
  settings_navigation::StyleGroupBox(host, 49904);
  DWORD_PTR modal_group_state = 0;
  Check(::GetWindowSubclass(modal_group, settings_navigation::GroupBoxProc, 9,
                            &modal_group_state) != FALSE,
        "Update group box is not theme painted");
  settings_theme::Current().accent = settings_theme::Accent::Custom;
  settings_theme::Current().custom = {255, 255, 0};
  settings_theme::Update(host);
  Check(settings_theme::GetColor(COLOR_HIGHLIGHTTEXT) == RGB(0, 0, 0),
        "Bright accent contrast");
  const DWORD gdi = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
  for (int n = 0; n < 100; ++n) {
    settings_theme::Current().mode =
        n % 2 ? settings_theme::Mode::Dark : settings_theme::Mode::Light;
    settings_theme::Update(host);
    settings_theme::GetBrush(COLOR_WINDOW);
  }
  Check(::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS) <= gdi + 3,
        "Theme brush leak");
  ::SendMessageW(model, CB_SETCURSEL, 4, 0);
  ::SendMessageW(panel, WM_COMMAND, MAKEWPARAM(40104, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(model));
  ::DestroyWindow(panel);
  for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
    panel = picker.Create(
        host, reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), 0, 0,
        ::MulDiv(420, dpi, 96), dpi, [](auto) {});
    picker.Set({}, true);
    Check(::SendDlgItemMessageW(panel, 40104, CB_GETCURSEL, 0, 0) == 4,
          "Reopened picker model differs from its fields");
    RECT outer{};
    ::GetClientRect(panel, &outer);
    for (int id : {40103, 40104, 40110, 40111, 40112, 40113}) {
      RECT child{};
      ::GetWindowRect(::GetDlgItem(panel, id), &child);
      ::MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&child), 2);
      if (!(child.left >= 0 && child.top >= 0 && child.right <= outer.right &&
            child.bottom <= outer.bottom))
        std::cerr << "Picker field " << id << " at DPI " << dpi << ": "
                  << child.left << ',' << child.top << '-' << child.right << ','
                  << child.bottom << " panel " << outer.right << 'x'
                  << outer.bottom << '\n';
      Check(child.left >= 0 && child.top >= 0 && child.right <= outer.right &&
                child.bottom <= outer.bottom,
            "Picker field outside scaled panel");
    }
    ::DestroyWindow(panel);
  }
  ::DestroyWindow(host);
  std::cout << "Native picker: live input, invalid input, fixed geometry, "
               "visibility, contrast, themes and GDI lifecycle passed.\n";
}
