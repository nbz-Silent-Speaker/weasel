#include "stdafx.h"
#include "SwitcherSettingsDialog.h"

#include "Configurator.h"
#include "WeaselDeployer.h"

#include <algorithm>
#include <cwctype>
#include <set>

#include <rime_levers_api.h>
#include <WeaselUtility.h>

namespace {
constexpr UINT_PTR kModelTimer = 1;

std::wstring Lowercase(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
  return value;
}
}  // namespace

SwitcherSettingsDialog::SwitcherSettingsDialog(RimeSwitcherSettings* settings)
    : settings_(settings), loaded_(false), modified_(false) {
  api_ = (RimeLeversApi*)rime_get_api()->find_module("levers")->get_api();
}

SwitcherSettingsDialog::~SwitcherSettingsDialog() {}

std::wstring SwitcherSettingsDialog::LocalText(const wchar_t* simplified,
                                               const wchar_t* traditional,
                                               const wchar_t* english) const {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE)
    return english;
  const WORD sublanguage = SUBLANGID(language);
  return sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
                 sublanguage == SUBLANG_CHINESE_SINGAPORE
             ? simplified
             : traditional;
}

void SwitcherSettingsDialog::Populate() {
  if (!settings_)
    return;
  loaded_ = false;
  schemas_.clear();

  RimeSchemaList available = {0};
  api_->get_available_schema_list(settings_, &available);
  RimeSchemaList selected = {0};
  api_->get_selected_schema_list(settings_, &selected);

  std::set<std::string> selected_ids;
  for (size_t i = 0; i < selected.size; ++i) {
    if (selected.list[i].schema_id)
      selected_ids.emplace(selected.list[i].schema_id);
  }

  std::set<RimeSchemaInfo*> recruited;
  const auto append_schema = [&](RimeSchemaListItem& item) {
    auto* info = reinterpret_cast<RimeSchemaInfo*>(item.reserved);
    if (!info || recruited.find(info) != recruited.end())
      return;
    recruited.insert(info);
    SchemaEntry entry;
    entry.info = info;
    entry.id = item.schema_id ? item.schema_id : "";
    entry.name = u8tow(item.name ? item.name : entry.id.c_str());
    entry.enabled = selected_ids.find(entry.id) != selected_ids.end();
    schemas_.push_back(std::move(entry));
  };

  for (size_t i = 0; i < selected.size; ++i) {
    if (!selected.list[i].schema_id)
      continue;
    for (size_t j = 0; j < available.size; ++j) {
      if (available.list[j].schema_id &&
          !strcmp(available.list[j].schema_id, selected.list[i].schema_id)) {
        append_schema(available.list[j]);
        break;
      }
    }
  }
  for (size_t i = 0; i < available.size; ++i)
    append_schema(available.list[i]);

  if (const char* hotkeys = api_->get_hotkeys(settings_))
    hotkeys_.SetWindowTextW(u8tow(hotkeys).c_str());

  RebuildFilteredList();
  loaded_ = true;
  modified_ = false;
}

void SwitcherSettingsDialog::RebuildFilteredList() {
  CString search_text;
  search_.GetWindowTextW(search_text);
  const std::wstring query = Lowercase(search_text.GetString());

  loaded_ = false;
  schema_list_.DeleteAllItems();
  int row = 0;
  for (size_t i = 0; i < schemas_.size(); ++i) {
    const auto& schema = schemas_[i];
    if (!query.empty() &&
        Lowercase(schema.name).find(query) == std::wstring::npos &&
        Lowercase(u8tow(schema.id)).find(query) == std::wstring::npos) {
      continue;
    }
    schema_list_.AddItem(row, 0, schema.name.c_str());
    schema_list_.SetItemData(row, static_cast<DWORD_PTR>(i));
    schema_list_.SetCheckState(row, schema.enabled ? TRUE : FALSE);
    ++row;
  }
  loaded_ = true;

  if (schema_list_.GetItemCount() > 0) {
    schema_list_.SelectItem(0);
    ShowDetails(static_cast<size_t>(schema_list_.GetItemData(0)));
  } else {
    selected_schema_ = static_cast<size_t>(-1);
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DESCRIPTION, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_STATUS, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_VERSION, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_SOURCE, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_UPDATE, L"");
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_RESTORE_PACKAGE), SW_HIDE);
    ShowModelControls(false);
  }
}

void SwitcherSettingsDialog::ShowDetails(size_t index) {
  if (index >= schemas_.size())
    return;
  selected_schema_ = index;
  const auto& schema = schemas_[index];
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, schema.name.c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_STATUS,
                    LocalText(schema.enabled ? L"已启用" : L"未启用",
                              schema.enabled ? L"已啟用" : L"未啟用",
                              schema.enabled ? L"Enabled" : L"Not enabled")
                        .c_str());

  std::string details;
  if (const char* description = api_->get_schema_description(schema.info))
    details += description;
  if (details.empty()) {
    if (const char* author = api_->get_schema_author(schema.info))
      details = author;
  }
  description_.SetWindowTextW(u8tow(details).c_str());

  const bool wanxiang = schema.id == "wanxiang_lite";
  ::SetDlgItemTextW(
      m_hWnd, IDC_SCHEMA_DETAIL_VERSION,
      wanxiang ? L"17.10.0"
               : LocalText(L"未提供", L"未提供", L"Not provided").c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_SCHEMA_DETAIL_SOURCE,
      wanxiang
          ? LocalText(L"万象官方 CNB · 已验证", L"萬象官方 CNB · 已驗證",
                      L"Official Wanxiang CNB · verified")
                .c_str()
          : LocalText(L"本机已有方案", L"本機已有方案", L"Local Rime schema")
                .c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_SCHEMA_DETAIL_UPDATE,
      wanxiang
          ? LocalText(L"稳定版", L"穩定版", L"Stable channel").c_str()
          : LocalText(L"手动管理", L"手動管理", L"Managed manually").c_str());

  // Update controls stay reserved until signed-feed updates are implemented.
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_RESTORE_PACKAGE), SW_HIDE);
  ShowModelControls(wanxiang);
  if (wanxiang)
    UpdateModelUi();
}

void SwitcherSettingsDialog::ShowModelControls(bool show) {
  constexpr int controls[] = {
      IDC_MODEL_GROUP,     IDC_MODEL_NAME,     IDC_MODEL_DESCRIPTION,
      IDC_MODEL_SOURCE,    IDC_MODEL_NOTE,     IDC_MODEL_DOWNLOAD,
      IDC_MODEL_SECONDARY, IDC_MODEL_PROGRESS, IDC_MODEL_PROGRESS_TEXT,
  };
  for (const int control : controls)
    ::ShowWindow(GetDlgItem(control), show ? SW_SHOW : SW_HIDE);
}

void SwitcherSettingsDialog::UpdateModelUi() {
  const auto progress = model_manager_.GetProgress();
  const bool downloading =
      progress.state == WanxiangModelManager::State::Downloading;
  const bool transferred =
      progress.state == WanxiangModelManager::State::Transferred;
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS), downloading ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS_TEXT),
               downloading ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_NOTE), downloading ? SW_HIDE : SW_SHOW);

  HWND primary = GetDlgItem(IDC_MODEL_DOWNLOAD);
  HWND secondary = GetDlgItem(IDC_MODEL_SECONDARY);
  ::SetDlgItemTextW(m_hWnd, IDC_MODEL_NOTE,
                    LocalText(L"下载完成后将自动安装并重新部署。",
                              L"下載完成後將自動安裝並重新部署。",
                              L"After download, the model is installed and "
                              L"redeployed automatically.")
                        .c_str());
  if (downloading) {
    const auto total =
        progress.total ? progress.total : WanxiangModelManager::kExpectedSize;
    const int value = static_cast<int>(std::min<unsigned long long>(
        1000, progress.transferred * 1000 / total));
    model_progress_.SetPos(value);
    wchar_t label[64] = {};
    swprintf_s(label, L"%llu / %llu MB", progress.transferred / 1024 / 1024,
               total / 1024 / 1024);
    ::SetDlgItemTextW(m_hWnd, IDC_MODEL_PROGRESS_TEXT, label);
    ::SetWindowTextW(
        primary, LocalText(L"正在下载", L"正在下載", L"Downloading").c_str());
    ::EnableWindow(primary, FALSE);
    ::SetWindowTextW(secondary, LocalText(L"取消", L"取消", L"Cancel").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (transferred) {
    ::SetWindowTextW(primary,
                     LocalText(L"正在校验", L"正在校驗", L"Verifying").c_str());
    ::EnableWindow(primary, FALSE);
    ::ShowWindow(secondary, SW_HIDE);
  } else if (progress.state == WanxiangModelManager::State::Installed) {
    ::SetWindowTextW(primary,
                     LocalText(L"已安装", L"已安裝", L"Installed").c_str());
    ::EnableWindow(primary, FALSE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"移除模型", L"移除模型", L"Remove model").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (progress.state == WanxiangModelManager::State::Modified) {
    ::SetWindowTextW(primary, LocalText(L"现有文件已修改", L"現有檔案已修改",
                                        L"File modified")
                                  .c_str());
    ::EnableWindow(primary, FALSE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"移除模型", L"移除模型", L"Remove model").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (progress.state == WanxiangModelManager::State::Error) {
    ::SetWindowTextW(primary, LocalText(L"重试", L"重試", L"Retry").c_str());
    ::EnableWindow(primary, TRUE);
    ::ShowWindow(secondary, SW_HIDE);
    ::SetDlgItemTextW(
        m_hWnd, IDC_MODEL_NOTE,
        (LocalText(L"下载失败：", L"下載失敗：", L"Download failed: ") +
         progress.error)
            .c_str());
  } else {
    ::SetWindowTextW(
        primary,
        LocalText(L"下载模型", L"下載模型", L"Download model").c_str());
    ::EnableWindow(primary, TRUE);
    ::ShowWindow(secondary, SW_HIDE);
  }
}

void SwitcherSettingsDialog::FinishModelDownload() {
  std::wstring error;
  if (!model_manager_.CompleteAndInstall(&error)) {
    ::MessageBoxW(m_hWnd, error.c_str(),
                  LocalText(L"模型安装失败", L"模型安裝失敗",
                            L"Model installation failed")
                      .c_str(),
                  MB_OK | MB_ICONERROR);
    UpdateModelUi();
    return;
  }

  Configurator configurator;
  if (configurator.UpdateWorkspace(true) != 0) {
    std::wstring rollback_error;
    const bool file_restored = model_manager_.Rollback(&rollback_error);
    const bool restored =
        file_restored && configurator.UpdateWorkspace(false) == 0;
    std::wstring message =
        restored
            ? LocalText(L"模型已下载，但重新部署失败，原文件已经恢复。",
                        L"模型已下載，但重新部署失敗，原檔案已經恢復。",
                        L"The model downloaded, but deployment failed. The "
                        L"previous file was restored.")
            : LocalText(
                  L"模型已下载，但重新部署和自动恢复均失败，请查看部署日志。",
                  L"模型已下載，但重新部署和自動恢復均失敗，請查看部署記錄。",
                  L"The model downloaded, but deployment and automatic "
                  L"recovery both failed. Review the deployment log.");
    if (!rollback_error.empty())
      message += L"\n" + rollback_error;
    ::MessageBoxW(m_hWnd, message.c_str(),
                  LocalText(L"模型安装失败", L"模型安裝失敗",
                            L"Model installation failed")
                      .c_str(),
                  MB_OK | MB_ICONERROR);
  } else {
    std::wstring commit_error;
    if (!model_manager_.Commit(&commit_error)) {
      std::wstring rollback_error;
      const bool file_restored = model_manager_.Rollback(&rollback_error);
      const bool restored =
          file_restored && configurator.UpdateWorkspace(false) == 0;
      std::wstring message =
          restored
              ? LocalText(
                    L"模型已下载并部署，但无法完成安装记录，原文件已经恢复。",
                    L"模型已下載並部署，但無法完成安裝記錄，原檔案已經恢復。",
                    L"The model was deployed, but the installation could "
                    L"not be finalized. The previous file was restored.")
              : LocalText(
                    L"模型已部署，但无法完成安装记录和自动恢复，请查看部署日志"
                    L"。",
                    L"模型已部署，但無法完成安裝記錄和自動恢復，請查看部署記錄"
                    L"。",
                    L"The model was deployed, but finalization and automatic "
                    L"recovery failed. Review the deployment log.");
      if (!commit_error.empty())
        message += L"\n" + commit_error;
      if (!rollback_error.empty())
        message += L"\n" + rollback_error;
      ::MessageBoxW(m_hWnd, message.c_str(),
                    LocalText(L"模型安装失败", L"模型安裝失敗",
                              L"Model installation failed")
                        .c_str(),
                    MB_OK | MB_ICONERROR);
      UpdateModelUi();
      return;
    }
    ::MessageBoxW(
        m_hWnd,
        LocalText(
            L"语言模型已安装并完成重新部署。",
            L"語言模型已安裝並完成重新部署。",
            L"The language model is installed and Weasel has been redeployed.")
            .c_str(),
        LocalText(L"安装完成", L"安裝完成", L"Installation complete").c_str(),
        MB_OK | MB_ICONINFORMATION);
  }
  UpdateModelUi();
}

LRESULT SwitcherSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  schema_list_.SubclassWindow(GetDlgItem(IDC_SCHEMA_LIST));
  schema_list_.SetExtendedListViewStyle(
      LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES,
      LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

  CString schema_name;
  schema_name.LoadStringW(IDS_STR_SCHEMA_NAME);
  schema_list_.AddColumn(schema_name, 0);
  CRect rect;
  schema_list_.GetClientRect(&rect);
  schema_list_.SetColumnWidth(0, rect.Width() - 24);

  description_.Attach(GetDlgItem(IDC_SCHEMA_DESCRIPTION));
  hotkeys_.Attach(GetDlgItem(IDC_HOTKEYS));
  hotkeys_.EnableWindow(FALSE);
  get_schemata_.Attach(GetDlgItem(IDC_GET_SCHEMATA));
  search_.Attach(GetDlgItem(IDC_SCHEMA_SEARCH));
  model_progress_.Attach(GetDlgItem(IDC_MODEL_PROGRESS));
  model_progress_.SetRange32(0, 1000);

  // Keep future package-management controls in the resource layout, but do not
  // show promises that are not functional in this first integrated release.
  ::ShowWindow(GetDlgItem(IDC_AUTO_SCHEME_UPDATES), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_CHECK_SCHEME_UPDATES), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_ADD_SCHEME_URL), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_IMPORT_SCHEME), SW_HIDE);

  Populate();
  SetTimer(kModelTimer, 500);

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  KillTimer(kModelTimer);
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  KillTimer(kModelTimer);
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnTimer(UINT, WPARAM timer, LPARAM, BOOL&) {
  if (timer != kModelTimer)
    return 0;
  const auto progress = model_manager_.GetProgress();
  if (progress.state == WanxiangModelManager::State::Transferred) {
    UpdateModelUi();
    RedrawWindow(nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    FinishModelDownload();
  } else if (selected_schema_ < schemas_.size() &&
             schemas_[selected_schema_].id == "wanxiang_lite") {
    UpdateModelUi();
  }
  return 0;
}

LRESULT SwitcherSettingsDialog::OnSearchChanged(WORD, WORD, HWND, BOOL&) {
  RebuildFilteredList();
  return 0;
}

LRESULT SwitcherSettingsDialog::OnModelPrimary(WORD, WORD, HWND, BOOL&) {
  if (model_manager_.GetProgress().state ==
      WanxiangModelManager::State::Error) {
    model_manager_.Cancel();
  }
  std::wstring error;
  if (!model_manager_.Start(&error)) {
    ::MessageBoxW(
        m_hWnd, error.c_str(),
        LocalText(L"无法开始下载", L"無法開始下載", L"Unable to start download")
            .c_str(),
        MB_OK | MB_ICONERROR);
  }
  UpdateModelUi();
  return 0;
}

LRESULT SwitcherSettingsDialog::OnModelSecondary(WORD, WORD, HWND, BOOL&) {
  const auto state = model_manager_.GetProgress().state;
  if (state == WanxiangModelManager::State::Downloading ||
      state == WanxiangModelManager::State::Error) {
    model_manager_.Cancel();
    UpdateModelUi();
    return 0;
  }

  const int answer = ::MessageBoxW(
      m_hWnd,
      LocalText(L"确定移除已安装的语言模型吗？",
                L"確定移除已安裝的語言模型嗎？",
                L"Remove the installed language model?")
          .c_str(),
      LocalText(L"移除语言模型", L"移除語言模型", L"Remove language model")
          .c_str(),
      MB_YESNO | MB_ICONQUESTION);
  if (answer != IDYES)
    return 0;

  std::wstring error;
  if (!model_manager_.RemoveInstalled(&error)) {
    ::MessageBoxW(
        m_hWnd, error.c_str(),
        LocalText(L"无法移除模型", L"無法移除模型", L"Unable to remove model")
            .c_str(),
        MB_OK | MB_ICONERROR);
    return 0;
  }
  Configurator configurator;
  if (configurator.UpdateWorkspace(true) != 0) {
    std::wstring rollback_error;
    const bool file_restored = model_manager_.Rollback(&rollback_error);
    const bool restored =
        file_restored && configurator.UpdateWorkspace(false) == 0;
    std::wstring message =
        restored
            ? LocalText(
                  L"重新部署失败，语言模型已经恢复。",
                  L"重新部署失敗，語言模型已經恢復。",
                  L"Deployment failed and the language model was restored.")
            : LocalText(
                  L"重新部署和自动恢复均失败，请保留当前文件并查看部署日志。",
                  L"重新部署和自動恢復均失敗，請保留目前檔案並查看部署記錄。",
                  L"Deployment and automatic recovery both failed. Keep the "
                  L"current files and review the deployment log.");
    if (!rollback_error.empty())
      message += L"\n" + rollback_error;
    ::MessageBoxW(
        m_hWnd, message.c_str(),
        LocalText(L"移除失败", L"移除失敗", L"Removal failed").c_str(),
        MB_OK | MB_ICONERROR);
  } else {
    std::wstring commit_error;
    if (!model_manager_.Commit(&commit_error)) {
      std::wstring rollback_error;
      const bool file_restored = model_manager_.Rollback(&rollback_error);
      const bool restored =
          file_restored && configurator.UpdateWorkspace(false) == 0;
      std::wstring message =
          restored
              ? LocalText(L"模型已移除，但无法完成操作记录，模型已经恢复。",
                          L"模型已移除，但無法完成操作記錄，模型已經恢復。",
                          L"The model was removed, but the operation could not "
                          L"be finalized. The model was restored.")
              : LocalText(
                    L"模型已移除，但无法完成操作记录和自动恢复，请查看部署日志"
                    L"。",
                    L"模型已移除，但無法完成操作記錄和自動恢復，請查看部署記錄"
                    L"。",
                    L"The model was removed, but finalization and automatic "
                    L"recovery failed. Review the deployment log.");
      if (!commit_error.empty())
        message += L"\n" + commit_error;
      if (!rollback_error.empty())
        message += L"\n" + rollback_error;
      ::MessageBoxW(
          m_hWnd, message.c_str(),
          LocalText(L"移除失败", L"移除失敗", L"Removal failed").c_str(),
          MB_OK | MB_ICONERROR);
    }
  }
  UpdateModelUi();
  return 0;
}

LRESULT SwitcherSettingsDialog::OnGetSchemata(WORD, WORD, HWND hWndCtl, BOOL&) {
  HKEY hKey = nullptr;
  std::wstring hPath = is_wow64() ? L"Software\\WOW6432Node\\Rime\\Weasel"
                                  : L"Software\\Rime\\Weasel";
  LSTATUS result = RegOpenKey(HKEY_LOCAL_MACHINE, hPath.c_str(), &hKey);
  if (result == ERROR_SUCCESS) {
    WCHAR value[MAX_PATH] = {};
    DWORD length = sizeof(value);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, L"WeaselRoot", nullptr, &type,
                              reinterpret_cast<LPBYTE>(value), &length);
    if (result == ERROR_SUCCESS && type == REG_SZ) {
      WCHAR parameters[MAX_PATH + 37] = {};
      wcscpy_s<_countof(parameters)>(
          parameters,
          (std::wstring(L"/k \"") + value + L"\\rime-install.bat\"").c_str());
      SHELLEXECUTEINFOW command = {sizeof(SHELLEXECUTEINFOW)};
      command.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
      command.hwnd = hWndCtl;
      command.lpVerb = L"open";
      command.lpFile = L"cmd";
      command.lpParameters = parameters;
      command.nShow = SW_SHOW;
      if (ShellExecuteExW(&command) && command.hProcess) {
        WaitForSingleObject(command.hProcess, INFINITE);
        CloseHandle(command.hProcess);
        api_->load_settings(reinterpret_cast<RimeCustomSettings*>(settings_));
        Populate();
      }
    }
    RegCloseKey(hKey);
  }
  return 0;
}

LRESULT SwitcherSettingsDialog::OnOK(WORD, WORD code, HWND, BOOL&) {
  if (modified_ && settings_ && !schemas_.empty()) {
    std::vector<const char*> selection;
    for (const auto& schema : schemas_) {
      if (schema.enabled)
        selection.push_back(schema.id.c_str());
    }
    if (selection.empty()) {
      MSG_BY_IDS(IDS_STR_ERR_AT_LEAST_ONE_SEL, IDS_STR_NOT_REGULAR,
                 MB_OK | MB_ICONEXCLAMATION);
      return 0;
    }
    api_->select_schemas(settings_, selection.data(), selection.size());
  }
  KillTimer(kModelTimer);
  EndDialog(code);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnSchemaListItemChanged(int,
                                                        LPNMHDR notification,
                                                        BOOL&) {
  auto* item = reinterpret_cast<LPNMLISTVIEW>(notification);
  if (!loaded_ || !item || item->iItem < 0 ||
      item->iItem >= schema_list_.GetItemCount()) {
    return 0;
  }
  const size_t index =
      static_cast<size_t>(schema_list_.GetItemData(item->iItem));
  if (index >= schemas_.size())
    return 0;

  if ((item->uNewState & LVIS_STATEIMAGEMASK) !=
      (item->uOldState & LVIS_STATEIMAGEMASK)) {
    schemas_[index].enabled = schema_list_.GetCheckState(item->iItem) != FALSE;
    modified_ = true;
    if (selected_schema_ == index)
      ShowDetails(index);
  }
  if ((item->uNewState & LVIS_SELECTED) && !(item->uOldState & LVIS_SELECTED)) {
    ShowDetails(index);
  }
  return 0;
}
