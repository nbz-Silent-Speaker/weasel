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
#include <WeaselUtility.h>
#pragma warning(disable : 4005)
#include <rime_api.h>
#include <rime_levers_api.h>
#pragma warning(default : 4005)
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
class SettingsPageInstance {
 public:
  SettingsPageInstance() = default;
  SettingsPageInstance(const SettingsPageInstance&) = delete;
  SettingsPageInstance& operator=(const SettingsPageInstance&) = delete;
  ~SettingsPageInstance() { Reset(); }

  bool Create(settings_navigation::Page page, HWND owner) {
    Reset();
    if (page == settings_navigation::Page::Input) {
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers)
        return false;
      input_api_ = reinterpret_cast<RimeLeversApi*>(levers->get_api());
      if (!input_api_)
        return false;
      switcher_ = input_api_->switcher_settings_init();
      auto* settings = reinterpret_cast<RimeCustomSettings*>(switcher_);
      if (!switcher_ || !input_api_->load_settings(settings)) {
        Reset();
        return false;
      }
      input_dialog_ = std::make_unique<SwitcherSettingsDialog>(switcher_);
      window_ = input_dialog_->Create(owner);
    } else if (page == settings_navigation::Page::Appearance) {
      RimeModule* levers = rime_get_api()->find_module("levers");
      if (!levers)
        return false;
      auto* api = reinterpret_cast<RimeLeversApi*>(levers->get_api());
      appearance_settings_ =
          std::make_unique<UIStyleSettings>(weasel::ColorSchemeTarget::Default);
      if (!api || !api->load_settings(appearance_settings_->settings())) {
        MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
                   MB_OK | MB_ICONERROR);
        Reset();
        return false;
      }
      appearance_dialog_ =
          std::make_unique<UIStyleSettingsDialog>(appearance_settings_.get());
      window_ = appearance_dialog_->Create(owner);
    } else if (page == settings_navigation::Page::Fonts) {
      font_dialog_ = std::make_unique<FontSettingsDialog>();
      window_ = font_dialog_->Create(owner);
    } else {
      status_dialog_ = std::make_unique<StatusIconSettingsDialog>();
      window_ = status_dialog_->Create(owner);
    }

    if (!window_) {
      Reset();
      return false;
    }
    settings_navigation::AttachHost(window_);
    return true;
  }

  HWND window() const { return window_; }

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
  auto active = std::make_unique<SettingsPageInstance>();
  if (!active->Create(initial_page, owner))
    return 1;

  ::ShowWindow(active->window(), SW_SHOW);
  ::UpdateWindow(active->window());
  ::SetForegroundWindow(active->window());

  MSG message{};
  bool running = true;
  while (running) {
    const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      if (result == 0)
        ::PostQuitMessage(static_cast<int>(message.wParam));
      return result < 0 ? 1 : 0;
    }

    if (!message.hwnd &&
        message.lParam == reinterpret_cast<LPARAM>(active->window())) {
      if (message.message == settings_navigation::kHostCloseMessage) {
        running = false;
        continue;
      }
      if (message.message == settings_navigation::kHostNavigateMessage) {
        auto replacement = std::make_unique<SettingsPageInstance>();
        const auto page = settings_navigation::PageFromCommand(
            static_cast<WORD>(message.wParam));
        if (!replacement->Create(page, owner))
          continue;

        RECT bounds{};
        ::GetWindowRect(active->window(), &bounds);
        ::SetWindowPos(replacement->window(), HWND_TOP, bounds.left, bounds.top,
                       0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::RedrawWindow(
            replacement->window(), nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        ::ShowWindow(active->window(), SW_HIDE);
        ::SetForegroundWindow(replacement->window());
        active = std::move(replacement);
        continue;
      }
    }

    if (::IsDialogMessageW(active->window(), &message))
      continue;
    ::TranslateMessage(&message);
    ::DispatchMessageW(&message);
  }
  return 0;
}

int Configurator::UpdateWorkspace(bool report_errors) {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
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

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
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

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
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
