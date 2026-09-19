// WeaselDeployer.cpp : Defines the entry point for the application.
//
#include "stdafx.h"
#include <WeaselUtility.h>
#include <WeaselColorScheme.h>
#include <array>
#include <filesystem>
#include <fstream>
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "WanxiangModelManager.h"
#include "WanxiangUpdateManager.h"
#include "InputMethodIcon.h"
#include <ShellScalingApi.h>

#pragma comment(lib, "Shcore.lib")

CAppModule _Module;

static int Run(LPTSTR lpCmdLine);

namespace {
constexpr std::uintmax_t kSettingsWarmFileLimit = 2 * 1024 * 1024;
constexpr size_t kSettingsWarmFileCount = 256;

bool IsSettingsMetadata(const std::filesystem::path& path) {
  const std::wstring name = path.filename().wstring();
  if (name == L"default.yaml" || name == L"default.custom.yaml" ||
      name == L"weasel.yaml" || name == L"weasel.custom.yaml") {
    return true;
  }
  constexpr wchar_t suffix[] = L".schema.yaml";
  return name.size() >= _countof(suffix) - 1 &&
         name.compare(name.size() - (_countof(suffix) - 1),
                      _countof(suffix) - 1, suffix) == 0;
}

void WarmSettingsDirectory(const std::filesystem::path& directory,
                           size_t* warmed_files) {
  if (!warmed_files || *warmed_files >= kSettingsWarmFileCount)
    return;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(directory, error), end;
       !error && iterator != end && *warmed_files < kSettingsWarmFileCount;
       iterator.increment(error)) {
    const auto& entry = *iterator;
    if (!entry.is_regular_file(error) || error ||
        !IsSettingsMetadata(entry.path())) {
      error.clear();
      continue;
    }
    const auto size = entry.file_size(error);
    if (error || size > kSettingsWarmFileLimit) {
      error.clear();
      continue;
    }
    std::ifstream input(entry.path(), std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (input)
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    ++*warmed_files;
  }
}

void WarmSettingsMetadata() {
  const ULONGLONG started = ::GetTickCount64();
  size_t warmed_files = 0;
  const auto shared = WeaselSharedDataPath();
  const auto user = WeaselUserDataPath();
  WarmSettingsDirectory(shared, &warmed_files);
  WarmSettingsDirectory(user, &warmed_files);
  WarmSettingsDirectory(user / L"build", &warmed_files);
  LOG(INFO) << "Warmed " << warmed_files << " settings metadata files in "
            << (::GetTickCount64() - started) << " ms.";
}
}  // namespace

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine,
                       int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);

  // The elevated branding writer is independent of the running settings UI:
  // no deployer mutex, Rime initialization, user-profile writes or windows.
  int argument_count = 0;
  LPWSTR* arguments =
      ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
  if (arguments && argument_count >= 2 &&
      !wcscmp(arguments[1], L"/input-method-icon")) {
    DWORD result = ERROR_INVALID_PARAMETER;
    try {
      if (argument_count == 3)
        result = input_method_icon::ApplyElevated(arguments[2]);
    } catch (const std::exception&) {
      result = ERROR_GEN_FAILURE;
    }
    ::LocalFree(arguments);
    return static_cast<int>(result);
  }
  if (arguments)
    ::LocalFree(arguments);

  // Establish DPI awareness before any window, font, or dialog resource is
  // created.  The project setting alone did not emit a manifest resource in
  // the packaged executable, which left Windows bitmap-scaling this UI.
  ::SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  HRESULT hRes = ::CoInitialize(NULL);
  // If you are running on NT 4.0 or higher you can use the following call
  // instead to make the EXE free threaded. This means that calls come in on a
  // random RPC thread.
  // HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hRes));

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LINK_CLASS);

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int ret = 0;
  // The automatic package check is a network operation started by the tray
  // service during sign-in.  It must not own the deployer UI lock: on a cold
  // boot that made the first /input process exit before creating a window.
  const bool package_check =
      lpCmdLine && !wcscmp(L"/package-update-check", lpCmdLine);
  HANDLE hMutex = CreateMutex(NULL, TRUE,
                              package_check ? L"WeaselPackageUpdateCheckMutex"
                                            : L"WeaselDeployerExclusiveMutex");
  if (!hMutex) {
    ret = 1;
  } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // A duplicate scheduled check has no work to do.  Interactive deployer
    // commands retain the existing single-instance error semantics.
    ret = package_check ? 0 : 1;
  } else {
    ret = Run(lpCmdLine);
  }

  if (hMutex) {
    CloseHandle(hMutex);
  }
  _Module.Term();
  ::CoUninitialize();

  return ret;
}

static int Run(LPTSTR lpCmdLine) {
  Configurator configurator;

  if (!wcscmp(L"/model-download-complete", lpCmdLine)) {
    WanxiangModelManager model_manager;
    const auto progress = model_manager.GetProgress();
    if (progress.state != WanxiangModelManager::State::Transferred) {
      LOG(ERROR)
          << "Wanxiang model completion invoked without a transferred job.";
      return 1;
    }
    std::wstring error;
    if (!model_manager.CompleteDownload(&error)) {
      LOG(ERROR) << "Failed to finalize the Wanxiang model download: "
                 << wtou8(error);
      return 1;
    }
    return 0;
  }

  if (!wcscmp(L"/package-update-check", lpCmdLine)) {
    // This process is launched hidden after the tray is ready.  Reading the
    // small settings metadata here primes the OS file cache for the first
    // interactive settings launch without calling librime off its UI thread.
    WarmSettingsMetadata();
    const auto frequency =
        WanxiangUpdateManager::LoadFrequency("wanxiang_lite");
    if (!WanxiangUpdateManager::IsAutomaticCheckDue(frequency))
      return 0;
    const auto result = WanxiangUpdateManager::CheckNow();
    if (!result.success) {
      LOG(ERROR) << "Automatic Wanxiang update check failed: "
                 << wtou8(result.error);
      return 1;
    }
    if (result.scheme_update_available) {
      LOG(INFO) << "A newer Wanxiang release is available: "
                << wtou8(result.latest_tag);
    }
    if (result.model_update_available) {
      LOG(INFO) << "A newer Wanxiang grammar model is available: "
                << wtou8(result.latest_model_sha256) << " ("
                << result.latest_model_size << " bytes)";
    }
    return 0;
  }

  if (!wcscmp(L"/?", lpCmdLine) || !wcscmp(L"/help", lpCmdLine)) {
    WCHAR msg[1024] = {0};
    if (LoadString(GetModuleHandle(NULL), IDS_STR_HELP, msg,
                   sizeof(msg) / sizeof(TCHAR))) {
      MessageBox(NULL, msg, L"Weasel Deployer", MB_ICONINFORMATION | MB_OK);
    } else {
      MessageBox(NULL,
                 L"Usage: WeaselDeployer.exe [options]\n"
                 L"/acrylic-color - Set Acrylic color scheme\n"
                 L"/acrylic-color-dark - Set dark Acrylic color scheme\n"
                 L"/normal-color - Set normal color scheme\n"
                 L"/normal-color-dark - Set dark normal color scheme\n"
                 L"/? or /help		- Show this help message\n"
                 L"/deploy		- Update Workspace\n"
                 L"/dict		- Manage dictionary\n"
                 L"/sync		- Sync user data\n"
                 L"/install		- Install Weasel (Initial deployment)",
                 L"Weasel Deployer", MB_ICONINFORMATION | MB_OK);
    }
    return 0;
  }

  // Settings must create their frame before Rime reads the user's schemas.
  // ConfigureSettings performs that initialization after the first visible
  // frame, so do not initialize the deployer on this path.
  if (!wcscmp(L"/settings", lpCmdLine))
    return configurator.ConfigureColorScheme(
        weasel::ColorSchemeTarget::Default);
  if (!wcscmp(L"/input", lpCmdLine))
    return configurator.Run(false);
  if (!wcscmp(L"/fonts", lpCmdLine))
    return configurator.ConfigureFonts();
  if (!wcscmp(L"/status-icons", lpCmdLine))
    return configurator.ConfigureStatusIcons();
  if (!lpCmdLine[0])
    return configurator.Run(false);

  configurator.Initialize();

  if (!wcscmp(L"/acrylic-color", lpCmdLine))
    return configurator.ConfigureColorScheme(
        weasel::ColorSchemeTarget::Acrylic);
  if (!wcscmp(L"/acrylic-color-dark", lpCmdLine))
    return configurator.ConfigureColorScheme(
        weasel::ColorSchemeTarget::AcrylicDark);
  if (!wcscmp(L"/normal-color-dark", lpCmdLine))
    return configurator.ConfigureColorScheme(
        weasel::ColorSchemeTarget::NormalDark);
  if (!wcscmp(L"/normal-color", lpCmdLine))
    return configurator.ConfigureColorScheme(weasel::ColorSchemeTarget::Normal);

  bool deployment_scheduled = !wcscmp(L"/deploy", lpCmdLine);
  if (deployment_scheduled) {
    return configurator.UpdateWorkspace();
  }

  bool dict_management = !wcscmp(L"/dict", lpCmdLine);
  if (dict_management) {
    return configurator.DictManagement();
  }

  bool sync_user_dict = !wcscmp(L"/sync", lpCmdLine);
  if (sync_user_dict) {
    return configurator.SyncUserData();
  }

  bool installing = !wcscmp(L"/install", lpCmdLine);
  return configurator.Run(installing);
}
