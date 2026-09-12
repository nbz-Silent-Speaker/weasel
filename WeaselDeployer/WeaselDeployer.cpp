// WeaselDeployer.cpp : Defines the entry point for the application.
//
#include "stdafx.h"
#include <WeaselUtility.h>
#include <WeaselColorScheme.h>
#include <fstream>
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "WanxiangModelManager.h"

CAppModule _Module;

static int Run(LPTSTR lpCmdLine);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine,
                       int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);

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

  AtlInitCommonControls(ICC_BAR_CLASSES | ICC_TAB_CLASSES);

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int ret = 0;
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerExclusiveMutex");
  if (!hMutex) {
    ret = 1;
  } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
    ret = 1;
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
  configurator.Initialize();

  if (!wcscmp(L"/model-download-complete", lpCmdLine)) {
    WanxiangModelManager model_manager;
    const auto progress = model_manager.GetProgress();
    if (progress.state != WanxiangModelManager::State::Transferred) {
      LOG(ERROR)
          << "Wanxiang model completion invoked without a transferred job.";
      return 1;
    }
    std::wstring error;
    if (!model_manager.CompleteAndInstall(&error)) {
      LOG(ERROR) << "Failed to install Wanxiang model: " << wtou8(error);
      return 1;
    }
    if (configurator.UpdateWorkspace(false) != 0) {
      std::wstring rollback_error;
      const bool file_restored = model_manager.Rollback(&rollback_error);
      if (!file_restored) {
        LOG(ERROR) << "Failed to roll back Wanxiang model: "
                   << wtou8(rollback_error);
      } else if (configurator.UpdateWorkspace(false) != 0) {
        LOG(ERROR)
            << "Wanxiang model was restored, but redeploying the previous "
               "state also failed.";
      }
      return 1;
    }
    std::wstring commit_error;
    if (!model_manager.Commit(&commit_error)) {
      LOG(ERROR) << "Failed to finalize Wanxiang model install: "
                 << wtou8(commit_error);
      std::wstring rollback_error;
      const bool file_restored = model_manager.Rollback(&rollback_error);
      if (!file_restored) {
        LOG(ERROR) << "Failed to roll back Wanxiang model: "
                   << wtou8(rollback_error);
      } else if (configurator.UpdateWorkspace(false) != 0) {
        LOG(ERROR)
            << "Wanxiang model was restored, but redeploying the previous "
               "state also failed.";
      }
      return 1;
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

  if (!wcscmp(L"/settings", lpCmdLine))
    return configurator.ConfigureColorScheme(
        weasel::ColorSchemeTarget::Default);
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
