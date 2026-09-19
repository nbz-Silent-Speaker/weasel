#include "stdafx.h"
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "FontSettingsDialog.h"
#include "SwitcherSettingsDialog.h"
#include "StatusIconSettingsDialog.h"
#include "UIStyleSettings.h"
#include "UIStyleSettingsDialog.h"
#include "SettingsPerformance.h"
#include "SettingsAppearancePopup.h"
#include "DictManagementDialog.h"
#include <WeaselConstants.h>
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <WeaselUserSettings.h>
#include <WeaselUtility.h>
#pragma warning(disable : 4005)
#include <rime_api.h>
#include <rime_levers_api.h>
#pragma warning(default : 4005)
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include "WeaselDeployer.h"

static void CreateFileIfNotExist(std::string filename) {
  std::filesystem::path file_path = WeaselUserDataPath() / u8tow(filename);
  DWORD dwAttrib = GetFileAttributes(file_path.c_str());
  if (!(INVALID_FILE_ATTRIBUTES != dwAttrib &&
        0 == (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) {
    std::wofstream o(file_path.c_str(), std::ios::app);
    o.close();
  }
}
Configurator::Configurator() = default;

void Configurator::Initialize() {
  const auto setup_started = settings_performance::Now();
  // Touching a user folder on a sleeping/removable drive can block for much
  // longer than creating the settings frame.  Keep it in Initialize() so the
  // settings path can paint its loading frame before accessing that drive.
  CreateFileIfNotExist("default.custom.yaml");
  CreateFileIfNotExist("weasel.custom.yaml");
  RIME_STRUCT(RimeTraits, weasel_traits);
  std::string shared_dir = wtou8(WeaselSharedDataPath().wstring());
  std::string user_dir = wtou8(WeaselUserDataPath().wstring());
  weasel_traits.shared_data_dir = shared_dir.c_str();
  weasel_traits.user_data_dir = user_dir.c_str();
  weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
  std::string distribution_name = wtou8(get_weasel_ime_name());
  weasel_traits.distribution_name = distribution_name.c_str();
  weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
  weasel_traits.distribution_version = WEASEL_VERSION;
  weasel_traits.app_name = "rime.weasel";
  std::string log_dir = WeaselLogPath().u8string();
  weasel_traits.log_dir = log_dir.c_str();
  RimeApi* rime_api = rime_get_api();
  assert(rime_api);
  rime_api->setup(&weasel_traits);
  settings_performance::Record("rime.setup", setup_started);
  LOG(INFO) << "WeaselDeployer reporting.";
  const auto deployer_started = settings_performance::Now();
  rime_api->deployer_initialize(NULL);
  settings_performance::Record("rime.deployer_initialize", deployer_started);
}

static bool configure_first_run_schema(RimeLeversApi* api,
                                       RimeSwitcherSettings* switcher_settings,
                                       bool* reconfigured) {
  RimeCustomSettings* settings =
      reinterpret_cast<RimeCustomSettings*>(switcher_settings);
  if (!api->load_settings(settings))
    return false;

  constexpr const char* kDefaultSchema = "wanxiang_lite";
  RimeSchemaList available = {0};
  api->get_available_schema_list(switcher_settings, &available);
  bool found = false;
  for (size_t i = 0; i < available.size; ++i) {
    if (available.list[i].schema_id &&
        !strcmp(available.list[i].schema_id, kDefaultSchema)) {
      found = true;
      break;
    }
  }
  if (!found) {
    LOG(ERROR) << "Bundled default schema is unavailable: " << kDefaultSchema;
    return false;
  }

  const char* selection[] = {kDefaultSchema};
  api->select_schemas(switcher_settings, selection, 1);
  if (!api->save_settings(settings))
    return false;
  *reconfigured = true;
  return true;
}

int Configurator::Run(bool installing) {
  if (!installing)
    return ConfigureSettings(settings_navigation::Page::Input);

  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers)
    return 1;
  RimeLeversApi* api = (RimeLeversApi*)levers->get_api();
  if (!api)
    return 1;

  bool reconfigured = false;
  RimeSwitcherSettings* switcher_settings = api->switcher_settings_init();

  const bool first_run =
      installing && api->is_first_run((RimeCustomSettings*)switcher_settings);

  bool switcher_configured = true;
  if (first_run) {
    switcher_configured =
        configure_first_run_schema(api, switcher_settings, &reconfigured);
  }
  api->custom_settings_destroy((RimeCustomSettings*)switcher_settings);

  if (first_run && !switcher_configured)
    return 1;

  if (installing || reconfigured) {
    return UpdateWorkspace(reconfigured);
  }
  return 0;
}

int Configurator::ConfigureColorScheme(weasel::ColorSchemeTarget target) {
  if (target == weasel::ColorSchemeTarget::Default)
    return ConfigureSettings(settings_navigation::Page::Appearance);

  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers)
    return 1;
  auto api = (RimeLeversApi*)levers->get_api();
  if (!api)
    return 1;
  UIStyleSettings settings(target);
  // The dialog loads the appearance snapshot once during initialization.
  UIStyleSettingsDialog dialog(&settings);
  dialog.DoModal();
  return 0;
}

int Configurator::ConfigureFonts() {
  return ConfigureSettings(settings_navigation::Page::Fonts);
}

int Configurator::ConfigureStatusIcons() {
  return ConfigureSettings(settings_navigation::Page::StatusIcons);
}

namespace {
inline constexpr wchar_t kSettingsHostClass[] = L"Weasel.SettingsWindow";
inline constexpr wchar_t kSettingsHostActivePage[] =
    L"Weasel.SettingsActivePage";

HMONITOR SelectSettingsMonitor(HWND owner) {
  if (owner && ::IsWindow(owner))
    return ::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);

  // The deployer is normally started by the tray process, so it has no active
  // window of its own.  The pointer is still over the tray menu when the
  // command is invoked and therefore identifies the display where the user
  // expects the settings window to open.
  POINT cursor{};
  if (::GetCursorPos(&cursor))
    return ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

  if (const HWND foreground = ::GetForegroundWindow())
    return ::MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
  return ::MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY);
}

RECT CenteredWindowBounds(HMONITOR monitor, int width, int height) {
  MONITORINFO info{sizeof(info)};
  RECT work_area{};
  if (monitor && ::GetMonitorInfoW(monitor, &info)) {
    work_area = info.rcWork;
  } else {
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
  }

  const int work_width = work_area.right - work_area.left;
  const int work_height = work_area.bottom - work_area.top;
  const int left = work_area.left + (work_width - width) / 2;
  const int top = work_area.top + (work_height - height) / 2;
  const int clamped_left = left < work_area.left ? work_area.left : left;
  const int clamped_top = top < work_area.top ? work_area.top : top;
  return {clamped_left, clamped_top, clamped_left + width,
          clamped_top + height};
}

LRESULT CALLBACK SettingsHostProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam) {
  if (message == settings_theme::kOpen) {
    settings_theme::Popup().Open(window, reinterpret_cast<HWND>(lparam));
    return 0;
  }
  if (message == WM_ERASEBKGND) {
    RECT bounds{};
    ::GetClientRect(window, &bounds);
    ::FillRect(reinterpret_cast<HDC>(wparam), &bounds,
               settings_theme::GetBrush(COLOR_BTNFACE));
    return 1;
  }
  if (message == WM_SETTINGCHANGE ||
      message == WM_DWMCOLORIZATIONCOLORCHANGED ||
      message == WM_SYSCOLORCHANGE || message == WM_THEMECHANGED) {
    settings_theme::Update(window);
    settings_theme::Popup().SystemChanged();
  }
  if (message == WM_CLOSE) {
    const HWND active =
        reinterpret_cast<HWND>(::GetPropW(window, kSettingsHostActivePage));
    if (active) {
      ::PostThreadMessageW(::GetCurrentThreadId(),
                           settings_navigation::kHostCloseMessage, IDCANCEL,
                           reinterpret_cast<LPARAM>(active));
    }
    return 0;
  }
  if (message == WM_SIZE) {
    struct ResizePages {
      HWND host;
      int width;
      int height;
    } resize{window, LOWORD(lparam), HIWORD(lparam)};
    ::EnumChildWindows(
        window,
        [](HWND child, LPARAM data) {
          const auto resize = reinterpret_cast<const ResizePages*>(data);
          if (::GetParent(child) != resize->host)
            return TRUE;
          ::SetWindowPos(child, nullptr, 0, 0, resize->width, resize->height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&resize));
  }
  if (message == WM_NCDESTROY)
    ::RemovePropW(window, kSettingsHostActivePage);
  return ::DefWindowProcW(window, message, wparam, lparam);
}

class SettingsHostWindow {
 public:
  SettingsHostWindow() = default;
  SettingsHostWindow(const SettingsHostWindow&) = delete;
  SettingsHostWindow& operator=(const SettingsHostWindow&) = delete;
  ~SettingsHostWindow() {
    if (window_ && ::IsWindow(window_))
      ::DestroyWindow(window_);
  }

  bool Create(HWND owner, HMONITOR monitor) {
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{sizeof(existing)};
    if (!::GetClassInfoExW(instance, kSettingsHostClass, &existing)) {
      WNDCLASSEXW type{sizeof(type)};
      type.lpfnWndProc = SettingsHostProc;
      type.hInstance = instance;
      type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
      // The configured light/dark mode may intentionally differ from the
      // Windows mode.  A system class brush would expose a large light block
      // whenever the host is erased before its page children repaint.
      type.hbrBackground = nullptr;
      type.lpszClassName = kSettingsHostClass;
      if (!::RegisterClassExW(&type) &&
          ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
      }
    }

    constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU |
                            WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    // WS_EX_WINDOWEDGE draws a legacy light strip below a dark Windows 11
    // title bar.  The settings layout uses a fixed DPI-scaled logical size.
    constexpr DWORD extended_style = WS_EX_CONTROLPARENT;
    const RECT initial = CenteredWindowBounds(monitor, 1, 1);
    window_ =
        ::CreateWindowExW(extended_style, kSettingsHostClass,
                          settings_navigation::LocalText(
                              L"小狼毫设置", L"小狼毫設定", L"Weasel settings")
                              .c_str(),
                          style, initial.left, initial.top, 1, 1, owner,
                          nullptr, instance, nullptr);
    if (window_)
      settings_navigation::DisableWindowTransitions(window_);
    return window_ != nullptr;
  }

  HWND window() const { return window_; }

  bool SizeForPage(HWND page, HMONITOR monitor) const {
    RECT page_bounds{};
    if (!window_ || !page || !::GetWindowRect(page, &page_bounds))
      return false;
    RECT host_bounds{0, 0, page_bounds.right - page_bounds.left,
                     page_bounds.bottom - page_bounds.top};
    const DWORD style =
        static_cast<DWORD>(::GetWindowLongPtrW(window_, GWL_STYLE));
    const DWORD extended_style =
        static_cast<DWORD>(::GetWindowLongPtrW(window_, GWL_EXSTYLE));
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using AdjustWindowRectExForDpiFn =
        BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    const auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
        ::GetProcAddress(user32, "GetDpiForWindow"));
    const auto adjust_for_dpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        ::GetProcAddress(user32, "AdjustWindowRectExForDpi"));
    const BOOL adjusted =
        adjust_for_dpi && get_dpi
            ? adjust_for_dpi(&host_bounds, style, FALSE, extended_style,
                             get_dpi(window_))
            : ::AdjustWindowRectEx(&host_bounds, style, FALSE, extended_style);
    if (!adjusted) {
      return false;
    }
    const int width = host_bounds.right - host_bounds.left;
    const int height = host_bounds.bottom - host_bounds.top;
    const RECT centered = CenteredWindowBounds(monitor, width, height);
    return ::SetWindowPos(window_, nullptr, centered.left, centered.top, width,
                          height, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
  }

  bool ShowCentered(HMONITOR monitor) const {
    RECT bounds{};
    if (!window_ || !::GetWindowRect(window_, &bounds))
      return false;
    const RECT centered = CenteredWindowBounds(
        monitor, bounds.right - bounds.left, bounds.bottom - bounds.top);
    return ::SetWindowPos(window_, HWND_TOP, centered.left, centered.top, 0, 0,
                          SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) !=
           FALSE;
  }

  void SetActivePage(HWND page) const {
    ::SetPropW(window_, kSettingsHostActivePage,
               reinterpret_cast<HANDLE>(page));
  }

 private:
  HWND window_ = nullptr;
};

class SettingsLoadingDialog
    : public settings_navigation::HostedDialogImpl<SettingsLoadingDialog> {
 public:
  enum { IDD = IDD_SWITCHER_SETTING };

  BEGIN_MSG_MAP(SettingsLoadingDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(settings_theme::kChanged, OnThemeChanged)
  END_MSG_MAP()

 private:
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    // Reuse the input page resource only for its exact 540 x 286 DLU frame and
    // Segoe UI font.  Its data-bound controls stay hidden until Rime is ready.
    ::EnumChildWindows(
        m_hWnd,
        [](HWND child, LPARAM parent) {
          if (::GetParent(child) == reinterpret_cast<HWND>(parent))
            ::ShowWindow(child, SW_HIDE);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(m_hWnd));
    settings_navigation::ResizeForSidebarFrame(m_hWnd);
    return settings_navigation::HostedPageInitResult();
  }

  LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

  LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    return 0;
  }

  LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
    settings_navigation::PaintBuffered(m_hWnd, [this](HDC dc,
                                                      const RECT& bounds) {
      RECT sidebar = bounds;
      sidebar.right =
          settings_navigation::MapDialogUnits(
              m_hWnd, 0, 0, settings_navigation::kSidebarWidthDlu, 0)
              .right;
      HBRUSH sidebar_brush =
          ::CreateSolidBrush(settings_navigation::SidebarSurface());
      ::FillRect(dc, &sidebar, sidebar_brush);
      ::DeleteObject(sidebar_brush);

      RECT content = bounds;
      content.left = sidebar.right;
      ::FillRect(dc, &content, settings_theme::GetBrush(COLOR_BTNFACE));
      const int inset = settings_navigation::MapDialogUnits(
                            m_hWnd, settings_navigation::kPageInsetDlu, 0, 0, 0)
                            .right;
      content.left += inset;
      content.right -= inset;
      HFONT font =
          reinterpret_cast<HFONT>(::SendMessageW(m_hWnd, WM_GETFONT, 0, 0));
      const HGDIOBJ old_font = font ? ::SelectObject(dc, font) : nullptr;
      ::SetBkMode(dc, TRANSPARENT);
      ::SetTextColor(dc, settings_theme::GetColor(COLOR_GRAYTEXT));
      const auto text = settings_navigation::LocalText(
          L"正在加载设置…", L"正在載入設定…", L"Loading settings…");
      ::DrawTextW(dc, text.c_str(), -1, &content,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      if (old_font)
        ::SelectObject(dc, old_font);
    });
    return 0;
  }
};

class SettingsPageInstance {
 public:
  SettingsPageInstance() = default;
  SettingsPageInstance(const SettingsPageInstance&) = delete;
  SettingsPageInstance& operator=(const SettingsPageInstance&) = delete;
  ~SettingsPageInstance() { Reset(); }

  bool Create(settings_navigation::Page page, HWND owner) {
    settings_navigation::HostedPageCreationScope hosted_page_creation;
    Reset();
    if (page == settings_navigation::Page::Input) {
      const ULONGLONG started = ::GetTickCount64();
      LOG(INFO) << "Creating Input settings page.";
      const auto input_started = settings_performance::Now();
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers) {
        LOG(ERROR) << "Settings preview could not load the Rime levers module.";
        return false;
      }
      input_api_ = reinterpret_cast<RimeLeversApi*>(levers->get_api());
      if (!input_api_) {
        LOG(ERROR) << "Settings preview could not load the Rime levers API.";
        return false;
      }
      switcher_ = input_api_->switcher_settings_init();
      auto* settings = reinterpret_cast<RimeCustomSettings*>(switcher_);
      if (!switcher_ || !input_api_->load_settings(settings)) {
        LOG(ERROR) << "Settings preview could not load switcher settings.";
        Reset();
        return false;
      }
      settings_performance::Record("input.settings", input_started);
      LOG(INFO) << "Input switcher settings loaded after "
                << (::GetTickCount64() - started) << " ms.";
      input_dialog_ = std::make_unique<SwitcherSettingsDialog>(switcher_);
      window_ = input_dialog_->CreateHosted(owner);
      LOG(INFO) << "Input settings window created after "
                << (::GetTickCount64() - started) << " ms.";
    } else if (page == settings_navigation::Page::Appearance) {
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers) {
        LOG(ERROR) << "Settings preview could not load the Rime levers module.";
        return false;
      }
      auto* api = reinterpret_cast<RimeLeversApi*>(levers->get_api());
      if (!api) {
        LOG(ERROR) << "Settings preview could not load appearance settings.";
        MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
                   MB_OK | MB_ICONERROR);
        Reset();
        return false;
      }
      appearance_settings_ =
          std::make_unique<UIStyleSettings>(weasel::ColorSchemeTarget::Default);
      appearance_dialog_ =
          std::make_unique<UIStyleSettingsDialog>(appearance_settings_.get());
      window_ = appearance_dialog_->CreateHosted(owner);
    } else if (page == settings_navigation::Page::Fonts) {
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers || !levers->get_api()) {
        LOG(ERROR) << "Font preview could not load the Rime levers module.";
        return false;
      }
      appearance_settings_ =
          std::make_unique<UIStyleSettings>(weasel::ColorSchemeTarget::Default);
      font_dialog_ =
          std::make_unique<FontSettingsDialog>(appearance_settings_.get());
      window_ = font_dialog_->CreateHosted(owner);
    } else {
      status_dialog_ = std::make_unique<StatusIconSettingsDialog>();
      window_ = status_dialog_->CreateHosted(owner);
    }

    if (!window_) {
      LOG(ERROR) << "Settings page window creation failed for page "
                 << static_cast<int>(page) << ", error " << ::GetLastError()
                 << ".";
      Reset();
      return false;
    }
    settings_navigation::AttachHost(window_);
    settings_theme::Update(owner);
    return true;
  }

  HWND window() const { return window_; }

  bool PrepareForDisplay() {
    return !appearance_dialog_ || appearance_dialog_->PrepareForDisplay();
  }

  bool FitIn(HWND host) {
    if (!window_ || !host || ::GetParent(window_) != host)
      return false;
    RECT client{};
    ::GetClientRect(host, &client);
    ::SetWindowPos(window_, HWND_TOP, 0, 0, client.right, client.bottom,
                   SWP_NOACTIVATE | SWP_HIDEWINDOW);
    return ::GetParent(window_) == host;
  }

  bool HasUnappliedChanges() const {
    if (input_dialog_)
      return input_dialog_->HasUnappliedChanges();
    if (appearance_dialog_)
      return appearance_dialog_->HasUnappliedChanges();
    if (font_dialog_)
      return font_dialog_->HasUnappliedChanges();
    return status_dialog_ && status_dialog_->HasUnappliedChanges();
  }

  bool ApplyChanges() {
    if (input_dialog_)
      return input_dialog_->ApplyChanges();
    if (appearance_dialog_)
      return appearance_dialog_->ApplyChanges();
    if (font_dialog_)
      return font_dialog_->ApplyChanges();
    return status_dialog_ && status_dialog_->ApplyChanges();
  }

  bool IsApplying() const {
    return input_dialog_ && input_dialog_->IsApplying();
  }

  void SetApplyEnabled(bool enabled) const {
    WORD id = 0;
    if (input_dialog_)
      id = IDOK;
    else if (appearance_dialog_)
      id = IDC_APPLY;
    else if (font_dialog_)
      id = IDC_FONT_APPLY;
    else if (status_dialog_)
      id = IDC_STATUS_APPLY;
    if (id) {
      if (HWND button = ::GetDlgItem(window_, id))
        ::EnableWindow(button, enabled ? TRUE : FALSE);
    }
  }

  void RefreshNavigation() const {
    for (WORD id = settings_navigation::kInput;
         id <= settings_navigation::kStatusIcons; ++id) {
      if (HWND item = ::GetDlgItem(window_, id))
        ::InvalidateRect(item, nullptr, FALSE);
    }
  }

  void PrepareClose() {
    if (input_dialog_)
      input_dialog_->PrepareClose();
  }

 private:
  void Reset() {
    if (window_ && ::IsWindow(window_)) {
      settings_navigation::DetachHost(window_);
      ::DestroyWindow(window_);
    }
    window_ = nullptr;
    status_dialog_.reset();
    font_dialog_.reset();
    appearance_dialog_.reset();
    appearance_settings_.reset();
    input_dialog_.reset();
    if (switcher_ && input_api_) {
      input_api_->custom_settings_destroy(
          reinterpret_cast<RimeCustomSettings*>(switcher_));
    }
    switcher_ = nullptr;
    input_api_ = nullptr;
  }

  HWND window_ = nullptr;
  RimeLeversApi* input_api_ = nullptr;
  RimeSwitcherSettings* switcher_ = nullptr;
  std::unique_ptr<SwitcherSettingsDialog> input_dialog_;
  std::unique_ptr<UIStyleSettings> appearance_settings_;
  std::unique_ptr<UIStyleSettingsDialog> appearance_dialog_;
  std::unique_ptr<FontSettingsDialog> font_dialog_;
  std::unique_ptr<StatusIconSettingsDialog> status_dialog_;
};
}  // namespace

int Configurator::ConfigureSettings(settings_navigation::Page initial_page) {
  settings_theme::Refresh();
  const auto started = settings_performance::Now();
  HWND owner = ::GetActiveWindow();
  const HMONITOR target_monitor = SelectSettingsMonitor(owner);
  settings_navigation::ClearUnappliedChanges();
  SettingsHostWindow host;
  std::array<std::unique_ptr<SettingsPageInstance>,
             settings_navigation::kPageCount>
      pages;
  if (!host.Create(owner, target_monitor)) {
    LOG(ERROR) << "Unable to create the stable settings window frame.";
    return 1;
  }

  SettingsLoadingDialog loading;
  HWND loading_window = loading.CreateHosted(host.window());
  if (!loading_window || !host.SizeForPage(loading_window, target_monitor) ||
      !::SetWindowPos(
          loading_window, HWND_TOP, 0, 0, 0, 0,
          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
    LOG(ERROR) << "Unable to create the settings loading frame.";
    return 1;
  }
  host.SetActivePage(loading_window);
  settings_theme::Update(host.window());
  if (!host.ShowCentered(target_monitor)) {
    LOG(ERROR) << "Unable to show the centered settings loading frame.";
    return 1;
  }
  ::UpdateWindow(host.window());
  ::SetForegroundWindow(host.window());
  settings_performance::Record("host.frame-visible", started);

  // librime owns process-global state and must be initialized and queried on
  // this UI thread.  The lightweight frame is already visible, so a sleeping
  // user-data drive cannot make the tray command appear unresponsive.
  Initialize();

  // Preserve the already painted loading surface while the real page creates
  // and applies native themes to its controls.
  ::SendMessageW(host.window(), WM_SETREDRAW, FALSE, 0);
  auto& initial = pages[settings_navigation::PageIndex(initial_page)];
  initial = std::make_unique<SettingsPageInstance>();
  if (!initial->Create(initial_page, host.window())) {
    LOG(ERROR) << "Unable to create the initial settings page.";
    return 1;
  }
  SettingsPageInstance* active = initial.get();
  if (!host.SizeForPage(active->window(), target_monitor) ||
      !active->FitIn(host.window())) {
    LOG(ERROR) << "Unable to size the settings window frame.";
    return 1;
  }

  // Only prepare the requested page. Unvisited pages must never delay the
  // first visible frame of the settings window.
  if (!active->PrepareForDisplay()) {
    return 1;
  }
  settings_performance::Record("host.prepared", started);

  // Replace the loading surface as one atomic host repaint.  The outer frame
  // has already been visible and centered throughout initialization.
  ::ShowWindow(loading_window, SW_HIDE);
  ::ShowWindow(active->window(), SW_SHOW);
  host.SetActivePage(active->window());
  loading.DestroyWindow();
  ::SendMessageW(host.window(), WM_SETREDRAW, TRUE, 0);
  ::RedrawWindow(
      host.window(), nullptr, nullptr,
      RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
  ::SetFocus(::GetDlgItem(
      active->window(),
      static_cast<WORD>(settings_navigation::kInput +
                        settings_navigation::PageIndex(initial_page))));

  wchar_t preview_navigation[2] = {};
  const bool preview_navigation_test =
      weasel::IsSettingsPreviewMode() &&
      ::GetEnvironmentVariableW(L"WEASEL_PREVIEW_NAVIGATE_INPUT",
                                preview_navigation,
                                _countof(preview_navigation)) &&
      preview_navigation[0] == L'1';
  const bool preview_appearance_test =
      weasel::IsSettingsPreviewMode() &&
      ::GetEnvironmentVariableW(L"WEASEL_PREVIEW_NAVIGATE_APPEARANCE",
                                preview_navigation,
                                _countof(preview_navigation)) &&
      preview_navigation[0] == L'1';
  const auto test_destination = preview_appearance_test
                                    ? settings_navigation::kAppearance
                                    : settings_navigation::kInput;
  if (preview_navigation_test || preview_appearance_test) {
    ::PostThreadMessageW(
        ::GetCurrentThreadId(), settings_navigation::kHostNavigateMessage,
        test_destination, reinterpret_cast<LPARAM>(active->window()));
  }

  const auto refresh_shared_state = [&pages]() {
    const bool applying = std::any_of(
        pages.begin(), pages.end(),
        [](const auto& page) { return page && page->IsApplying(); });
    const bool enable_apply =
        settings_navigation::HasAnyUnappliedChanges() && !applying;
    for (const auto& page : pages) {
      if (!page)
        continue;
      page->SetApplyEnabled(enable_apply);
      page->RefreshNavigation();
    }
  };
  refresh_shared_state();

  MSG message{};
  bool running = true;
  int navigation_test_result = 0;
  while (running) {
    const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      if (result == 0)
        ::PostQuitMessage(static_cast<int>(message.wParam));
      return result < 0 ? 1 : 0;
    }

    if (!message.hwnd &&
        message.message == settings_navigation::kHostStateChangedMessage) {
      refresh_shared_state();
      continue;
    }

    if (settings_theme::Popup().Translate(message))
      continue;

    if (!message.hwnd &&
        message.message == settings_navigation::kHostApplyMessage) {
      const bool sender_is_page =
          std::any_of(pages.begin(), pages.end(), [&message](const auto& page) {
            return page &&
                   message.lParam == reinterpret_cast<LPARAM>(page->window());
          });
      if (sender_is_page) {
        // Apply pages that complete synchronously first.  The input page may
        // start a background deployment, so it must be the final operation.
        constexpr std::array<settings_navigation::Page,
                             settings_navigation::kPageCount>
            apply_order = {settings_navigation::Page::Appearance,
                           settings_navigation::Page::Fonts,
                           settings_navigation::Page::StatusIcons,
                           settings_navigation::Page::Input};
        for (const auto page_id : apply_order) {
          auto& page = pages[settings_navigation::PageIndex(page_id)];
          if (page && page->HasUnappliedChanges() && !page->ApplyChanges())
            break;
        }
        refresh_shared_state();
      }
      continue;
    }

    if (!message.hwnd &&
        message.lParam == reinterpret_cast<LPARAM>(active->window())) {
      if (message.message == settings_navigation::kHostCloseMessage) {
        const bool has_unapplied =
            std::any_of(pages.begin(), pages.end(), [](const auto& page) {
              return page && page->HasUnappliedChanges();
            });
        bool close = !has_unapplied;
        if (has_unapplied) {
          close =
              ::MessageBoxW(
                  host.window(),
                  settings_navigation::LocalText(
                      L"一个或多个页面存在尚未应用的设置。是否放弃全部未应用的"
                      L"更改？",
                      L"一個或多個頁面存在尚未套用的設定。是否放棄全部未套用的"
                      L"變更？",
                      L"One or more pages contain unapplied settings. "
                      L"Discard all unapplied changes?")
                      .c_str(),
                  settings_navigation::LocalText(
                      L"未应用的设置", L"未套用的設定", L"Unapplied settings")
                      .c_str(),
                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
        }
        if (close) {
          for (auto& page : pages) {
            if (page)
              page->PrepareClose();
          }
          running = false;
        }
        continue;
      }
      if (message.message == settings_navigation::kHostNavigateMessage) {
        const auto navigation_started = settings_performance::Now();
        const auto page = settings_navigation::PageFromCommand(
            static_cast<WORD>(message.wParam));
        auto& destination = pages[settings_navigation::PageIndex(page)];
        if (!destination) {
          destination = std::make_unique<SettingsPageInstance>();
          if (!destination->Create(page, host.window()) ||
              !destination->FitIn(host.window())) {
            LOG(ERROR) << "Unable to create or embed settings page "
                       << static_cast<int>(page) << ".";
            destination.reset();
            continue;
          }
        }
        if (destination.get() == active)
          continue;

        if (!destination->PrepareForDisplay()) {
          MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
                     MB_OK | MB_ICONERROR);
          continue;
        }

        // Swap the hosted dialogs while painting is suspended.  Showing the
        // replacement before hiding the current child can cause the dialog
        // manager to hide the newly activated sibling again.  Keeping the
        // host frozen makes the correct hide-then-show order atomic on screen.
        ::SendMessageW(host.window(), WM_SETREDRAW, FALSE, 0);
        ::ShowWindow(active->window(), SW_HIDE);
        ::SetWindowPos(
            destination->window(), HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        active = destination.get();
        host.SetActivePage(active->window());
        ::SendMessageW(host.window(), WM_SETREDRAW, TRUE, 0);
        ::RedrawWindow(host.window(), nullptr, nullptr,
                       RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                           RDW_ALLCHILDREN | RDW_UPDATENOW);
        LOG(INFO) << "Navigated settings host to page "
                  << static_cast<int>(page) << ".";
        refresh_shared_state();
        ::SetFocus(
            ::GetDlgItem(active->window(), static_cast<WORD>(message.wParam)));
        settings_performance::Record(
            page == settings_navigation::Page::Appearance
                ? "navigation.appearance"
                : "navigation.other",
            navigation_started);
        if ((preview_navigation_test || preview_appearance_test) &&
            message.wParam == test_destination) {
          if (!::IsWindowVisible(active->window()))
            navigation_test_result |= 1;
          if (::GetPropW(host.window(), kSettingsHostActivePage) !=
              reinterpret_cast<HANDLE>(active->window())) {
            navigation_test_result |= 2;
          }
          if (!::GetDlgItem(active->window(), preview_appearance_test
                                                  ? IDC_PREVIEW_LIGHT
                                                  : IDC_SWITCHER_TITLE))
            navigation_test_result |= 4;
          for (auto& loaded_page : pages) {
            if (loaded_page)
              loaded_page->PrepareClose();
          }
          running = false;
        }
        continue;
      }
    }

    if (::IsDialogMessageW(active->window(), &message))
      continue;
    ::TranslateMessage(&message);
    ::DispatchMessageW(&message);
  }
  return navigation_test_result;
}

int Configurator::UpdateWorkspace(bool report_errors) {
  const bool preview = weasel::IsSettingsPreviewMode();
  HANDLE hMutex = CreateMutex(
      NULL, TRUE,
      preview ? L"WeaselSettingsPreviewDeployMutex" : L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    if (report_errors) {
      // MessageBox(NULL,
      // L"正在執行另一項部署任務，方纔所做的修改將在輸入法再次啓動後生效。",
      // L"【小狼毫】", MB_OK | MB_ICONINFORMATION);
      MSG_BY_IDS(IDS_STR_DEPLOYING_RESTARTREQ, IDS_STR_WEASEL,
                 MB_OK | MB_ICONINFORMATION);
    }
    return 1;
  }

  std::unique_ptr<weasel::Client> client;
  if (!preview)
    client = std::make_unique<weasel::Client>();
  if (client && client->Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client->StartMaintenance();
  }

  bool deployed = false;
  {
    RimeApi* rime = rime_get_api();
    // initialize default config, preset schemas
    const bool schemasDeployed = rime->deploy() != False;
    // initialize weasel config
    deployed = rime->deploy_config_file("weasel.yaml", "config_version") &&
               schemasDeployed;
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client && client->Connect()) {
    LOG(INFO) << "Resuming service.";
    client->EndMaintenance();
  }
  if (!deployed && report_errors) {
    MSG_BY_IDS(IDS_STR_SCHEME_DEPLOY_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
  }
  return deployed ? 0 : 1;
}

int Configurator::DictManagement() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }

  {
    RimeApi* rime = rime_get_api();
    if (RIME_API_AVAILABLE(rime, run_task)) {
      rime->run_task("installation_update");  // setup user data sync dir
    }
    DictManagementDialog dlg;
    dlg.DoModal();
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
  }
  return 0;
}

int Configurator::SyncUserData() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }

  {
    RimeApi* rime = rime_get_api();
    if (!rime->sync_user_data()) {
      LOG(ERROR) << "Error synching user data.";
      CloseHandle(hMutex);
      return 1;
    }
    rime->join_maintenance_thread();
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
  }
  return 0;
}
