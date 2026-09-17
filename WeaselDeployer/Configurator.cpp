#include "stdafx.h"
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "FontSettingsDialog.h"
#include "SwitcherSettingsDialog.h"
#include "StatusIconSettingsDialog.h"
#include "UIStyleSettings.h"
#include "UIStyleSettingsDialog.h"
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
Configurator::Configurator() {
  CreateFileIfNotExist("default.custom.yaml");
  CreateFileIfNotExist("weasel.custom.yaml");
}

void Configurator::Initialize() {
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
  LOG(INFO) << "WeaselDeployer reporting.";
  rime_api->deployer_initialize(NULL);
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
  if (!api->load_settings(settings.settings())) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    return 1;
  }
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

LRESULT CALLBACK SettingsHostProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam) {
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

  bool Create(HWND page, HWND owner) {
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{sizeof(existing)};
    if (!::GetClassInfoExW(instance, kSettingsHostClass, &existing)) {
      WNDCLASSEXW type{sizeof(type)};
      type.lpfnWndProc = SettingsHostProc;
      type.hInstance = instance;
      type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
      type.hbrBackground = ::GetSysColorBrush(COLOR_BTNFACE);
      type.lpszClassName = kSettingsHostClass;
      if (!::RegisterClassExW(&type) &&
          ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
      }
    }

    RECT bounds{};
    ::GetWindowRect(page, &bounds);
    const DWORD style =
        static_cast<DWORD>(::GetWindowLongPtrW(page, GWL_STYLE)) & ~WS_VISIBLE;
    const DWORD ex_style =
        static_cast<DWORD>(::GetWindowLongPtrW(page, GWL_EXSTYLE));
    window_ = ::CreateWindowExW(
        ex_style, kSettingsHostClass,
        settings_navigation::LocalText(L"小狼毫设置", L"小狼毫設定",
                                       L"Weasel settings")
            .c_str(),
        style | WS_CLIPCHILDREN, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr,
        instance, nullptr);
    if (window_)
      settings_navigation::DisableWindowTransitions(window_);
    return window_ != nullptr;
  }

  HWND window() const { return window_; }

  void SetActivePage(HWND page) const {
    ::SetPropW(window_, kSettingsHostActivePage,
               reinterpret_cast<HANDLE>(page));
  }

 private:
  HWND window_ = nullptr;
};

class SettingsPageInstance {
 public:
  SettingsPageInstance() = default;
  SettingsPageInstance(const SettingsPageInstance&) = delete;
  SettingsPageInstance& operator=(const SettingsPageInstance&) = delete;
  ~SettingsPageInstance() { Reset(); }

  bool Create(settings_navigation::Page page, HWND owner) {
    Reset();
    if (page == settings_navigation::Page::Input) {
      const ULONGLONG started = ::GetTickCount64();
      LOG(INFO) << "Creating Input settings page.";
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
      LOG(INFO) << "Input settings levers API ready after "
                << (::GetTickCount64() - started) << " ms.";
      switcher_ = input_api_->switcher_settings_init();
      LOG(INFO) << "Input switcher object ready after "
                << (::GetTickCount64() - started) << " ms.";
      auto* settings = reinterpret_cast<RimeCustomSettings*>(switcher_);
      if (!switcher_ || !input_api_->load_settings(settings)) {
        LOG(ERROR) << "Settings preview could not load switcher settings.";
        Reset();
        return false;
      }
      LOG(INFO) << "Input switcher settings loaded after "
                << (::GetTickCount64() - started) << " ms.";
      input_dialog_ = std::make_unique<SwitcherSettingsDialog>(switcher_);
      window_ = input_dialog_->Create(owner);
      LOG(INFO) << "Input settings window created after "
                << (::GetTickCount64() - started) << " ms.";
    } else if (page == settings_navigation::Page::Appearance) {
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers) {
        LOG(ERROR) << "Settings preview could not load the Rime levers module.";
        return false;
      }
      auto* api = reinterpret_cast<RimeLeversApi*>(levers->get_api());
      appearance_settings_ =
          std::make_unique<UIStyleSettings>(weasel::ColorSchemeTarget::Default);
      if (!api || !api->load_settings(appearance_settings_->settings())) {
        LOG(ERROR) << "Settings preview could not load appearance settings.";
        MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
                   MB_OK | MB_ICONERROR);
        Reset();
        return false;
      }
      appearance_dialog_ =
          std::make_unique<UIStyleSettingsDialog>(appearance_settings_.get());
      window_ = appearance_dialog_->Create(owner);
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
      window_ = font_dialog_->Create(owner);
    } else {
      status_dialog_ = std::make_unique<StatusIconSettingsDialog>();
      window_ = status_dialog_->Create(owner);
    }

    if (!window_) {
      LOG(ERROR) << "Settings page window creation failed for page "
                 << static_cast<int>(page) << ", error " << ::GetLastError()
                 << ".";
      Reset();
      return false;
    }
    settings_navigation::AttachHost(window_);
    return true;
  }

  HWND window() const { return window_; }

  bool EmbedIn(HWND host) {
    if (!window_ || !host)
      return false;
    LONG_PTR style = ::GetWindowLongPtrW(window_, GWL_STYLE);
    style &= ~(static_cast<LONG_PTR>(WS_POPUP) | WS_CAPTION | WS_SYSMENU |
               WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    ::SetWindowLongPtrW(window_, GWL_STYLE, style);
    LONG_PTR ex_style = ::GetWindowLongPtrW(window_, GWL_EXSTYLE);
    ex_style &= ~(static_cast<LONG_PTR>(WS_EX_APPWINDOW) | WS_EX_DLGMODALFRAME |
                  WS_EX_WINDOWEDGE);
    ::SetWindowLongPtrW(window_, GWL_EXSTYLE, ex_style);
    ::SetLastError(ERROR_SUCCESS);
    if (!::SetParent(window_, host) && ::GetLastError() != ERROR_SUCCESS)
      return false;
    RECT client{};
    ::GetClientRect(host, &client);
    ::SetWindowPos(window_, HWND_TOP, 0, 0, client.right, client.bottom,
                   SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
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
  HWND owner = ::GetActiveWindow();
  settings_navigation::ClearUnappliedChanges();
  SettingsHostWindow host;
  std::array<std::unique_ptr<SettingsPageInstance>,
             settings_navigation::kPageCount>
      pages;
  auto& initial = pages[settings_navigation::PageIndex(initial_page)];
  initial = std::make_unique<SettingsPageInstance>();
  if (!initial->Create(initial_page, owner)) {
    LOG(ERROR) << "Unable to create the initial settings page.";
    return 1;
  }
  SettingsPageInstance* active = initial.get();
  if (!host.Create(active->window(), owner) ||
      !active->EmbedIn(host.window())) {
    LOG(ERROR) << "Unable to create the stable settings window frame.";
    return 1;
  }
  host.SetActivePage(active->window());

  ::ShowWindow(active->window(), SW_SHOW);
  ::ShowWindow(host.window(), SW_SHOW);
  ::UpdateWindow(host.window());
  ::SetForegroundWindow(host.window());

  wchar_t preview_navigation[2] = {};
  const bool preview_navigation_test =
      weasel::IsSettingsPreviewMode() &&
      ::GetEnvironmentVariableW(L"WEASEL_PREVIEW_NAVIGATE_INPUT",
                                preview_navigation,
                                _countof(preview_navigation)) &&
      preview_navigation[0] == L'1';
  if (preview_navigation_test) {
    ::PostThreadMessageW(::GetCurrentThreadId(),
                         settings_navigation::kHostNavigateMessage,
                         settings_navigation::kInput,
                         reinterpret_cast<LPARAM>(active->window()));
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
        const auto page = settings_navigation::PageFromCommand(
            static_cast<WORD>(message.wParam));
        auto& destination = pages[settings_navigation::PageIndex(page)];
        if (!destination) {
          destination = std::make_unique<SettingsPageInstance>();
          if (!destination->Create(page, owner) ||
              !destination->EmbedIn(host.window())) {
            LOG(ERROR) << "Unable to create or embed settings page "
                       << static_cast<int>(page) << ".";
            destination.reset();
            continue;
          }
        }
        if (destination.get() == active)
          continue;

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
        if (preview_navigation_test &&
            page == settings_navigation::Page::Input) {
          if (!::IsWindowVisible(active->window()))
            navigation_test_result |= 1;
          if (::GetPropW(host.window(), kSettingsHostActivePage) !=
              reinterpret_cast<HANDLE>(active->window())) {
            navigation_test_result |= 2;
          }
          if (!::GetDlgItem(active->window(), IDC_SWITCHER_TITLE))
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
