#include "stdafx.h"
#include "SwitcherSettingsDialog.h"

#include "Configurator.h"
#include "WanxiangUpdateManager.h"
#include "WeaselDeployer.h"

#include <algorithm>
#include <set>

#include <rime_levers_api.h>
#include <WeaselUtility.h>

namespace {
constexpr UINT_PTR kModelTimer = 1;
constexpr int kUpdateSmart =
    static_cast<int>(WanxiangUpdateManager::Frequency::Smart);
constexpr int kUpdateDaily =
    static_cast<int>(WanxiangUpdateManager::Frequency::Daily);
constexpr int kUpdateWeekly =
    static_cast<int>(WanxiangUpdateManager::Frequency::Weekly);
constexpr int kUpdateMonthly =
    static_cast<int>(WanxiangUpdateManager::Frequency::Monthly);
constexpr int kUpdateDisabled =
    static_cast<int>(WanxiangUpdateManager::Frequency::Disabled);
constexpr UINT kUpdateMenuFirst = 41001;
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

int SwitcherSettingsDialog::LoadUpdateFrequency(
    const std::string& schema_id) const {
  return static_cast<int>(WanxiangUpdateManager::LoadFrequency(schema_id));
}

bool SwitcherSettingsDialog::SaveUpdateFrequency(const std::string& schema_id,
                                                 int frequency) const {
  return WanxiangUpdateManager::SaveFrequency(
      schema_id, static_cast<WanxiangUpdateManager::Frequency>(frequency));
}

std::wstring SwitcherSettingsDialog::UpdateFrequencyText(int frequency) const {
  switch (frequency) {
    case kUpdateSmart:
      return LocalText(L"智能", L"智慧", L"Smart");
    case kUpdateDaily:
      return LocalText(L"每天", L"每天", L"Daily");
    case kUpdateMonthly:
      return LocalText(L"每月", L"每月", L"Monthly");
    case kUpdateDisabled:
      return LocalText(L"关闭", L"關閉", L"Off");
    default:
      return LocalText(L"每周", L"每週", L"Weekly");
  }
}

void SwitcherSettingsDialog::SetLastUpdateCheckText(const std::wstring& text) {
  ::SetDlgItemTextW(m_hWnd, IDC_UPDATE_STATUS, text.c_str());
}

bool SwitcherSettingsDialog::ConfirmDiscardChanges() {
  if (!modified_)
    return true;
  return ::MessageBoxW(
             m_hWnd,
             LocalText(L"方案选择尚未保存。关闭并放弃这些更改吗？",
                       L"方案選擇尚未儲存。關閉並放棄這些變更嗎？",
                       L"Schema selections have not been saved. Close and "
                       L"discard the changes?")
                 .c_str(),
             LocalText(L"放弃更改", L"放棄變更", L"Discard changes").c_str(),
             MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
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

  RebuildList();
  loaded_ = true;
  modified_ = false;
}

void SwitcherSettingsDialog::RebuildList() {
  loaded_ = false;
  schema_list_.DeleteAllItems();
  int row = 0;
  for (size_t i = 0; i < schemas_.size(); ++i) {
    const auto& schema = schemas_[i];
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
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_PROJECT_LINKS, L"");
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
  const bool wanxiang = schema.id == "wanxiang_lite";
  std::wstring title = schema.name;
  if (wanxiang)
    title += L"  ·  " + std::wstring(WanxiangUpdateManager::kInstalledVersion);
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, title.c_str());
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

  ::SetDlgItemTextW(
      m_hWnd, IDC_SCHEMA_PROJECT_LINKS,
      wanxiang
          ? L"<a href=\"https://github.com/amzxyz/rime-wanxiang\">GitHub</a>"
            L"  ·  <a href=\"https://cnb.cool/amzxyz/rime-wanxiang\">CNB "
            L"国内源</a>"
          : L"");
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_PROJECT_LINKS),
               wanxiang ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS),
               wanxiang ? SW_SHOW : SW_HIDE);
  if (wanxiang) {
    const auto label = LocalText(L"更新设置：", L"更新設定：", L"Update: ") +
                       UpdateFrequencyText(LoadUpdateFrequency(schema.id));
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_UPDATE_SETTINGS, label.c_str());
  }
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

std::wstring SwitcherSettingsDialog::ModelErrorText(HRESULT error_code) const {
  std::wstring message;
  constexpr unsigned int kHttpFacility = 25;
  if (HRESULT_FACILITY(error_code) == kHttpFacility) {
    switch (HRESULT_CODE(error_code)) {
      case 403:
        message = LocalText(L"下载源拒绝了访问。", L"下載來源拒絕了存取。",
                            L"The download source denied access.");
        break;
      case 404:
        message = LocalText(L"下载文件不存在。", L"下載檔案不存在。",
                            L"The download file was not found.");
        break;
      case 408:
        message = LocalText(L"连接下载源超时。", L"連線下載來源逾時。",
                            L"The download source timed out.");
        break;
      case 429:
        message = LocalText(L"下载源请求过多，请稍后继续。",
                            L"下載來源要求過多，請稍後繼續。",
                            L"The download source is busy. Try again later.");
        break;
      default:
        if (HRESULT_CODE(error_code) >= 500)
          message =
              LocalText(L"下载源暂时不可用。", L"下載來源暫時無法使用。",
                        L"The download source is temporarily unavailable.");
        break;
    }
  }
  if (message.empty()) {
    message = LocalText(
        L"下载暂时无法继续，请检查网络后重试。",
        L"下載暫時無法繼續，請檢查網路後重試。",
        L"The download cannot continue. Check the network and retry.");
  }
  return message;
}

void SwitcherSettingsDialog::UpdateModelUi() {
  const auto progress = model_manager_.GetProgress();
  const bool downloading =
      progress.state == WanxiangModelManager::State::Downloading;
  const bool waiting_retry =
      progress.state == WanxiangModelManager::State::WaitingRetry;
  const bool paused = progress.state == WanxiangModelManager::State::Paused;
  const bool transferred =
      progress.state == WanxiangModelManager::State::Transferred;
  const bool show_progress =
      downloading || waiting_retry || paused || transferred;
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS),
               show_progress ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS_TEXT),
               show_progress ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_NOTE), SW_SHOW);

  HWND primary = GetDlgItem(IDC_MODEL_DOWNLOAD);
  HWND secondary = GetDlgItem(IDC_MODEL_SECONDARY);
  ::SetDlgItemTextW(m_hWnd, IDC_MODEL_NOTE,
                    LocalText(L"下载完成后将自动安装并重新部署。",
                              L"下載完成後將自動安裝並重新部署。",
                              L"After download, the model is installed and "
                              L"redeployed automatically.")
                        .c_str());
  if (show_progress) {
    const auto total =
        progress.total ? progress.total : WanxiangModelManager::kExpectedSize;
    const int value = static_cast<int>(std::min<unsigned long long>(
        1000, progress.transferred * 1000 / total));
    model_progress_.SetPos(value);
    wchar_t label[80] = {};
    swprintf_s(label, L"%llu / %llu MB · %d%%",
               progress.transferred / 1024 / 1024, total / 1024 / 1024,
               value / 10);
    ::SetDlgItemTextW(m_hWnd, IDC_MODEL_PROGRESS_TEXT, label);
  }
  if (downloading) {
    ::SetDlgItemTextW(
        m_hWnd, IDC_MODEL_NOTE,
        LocalText(L"正在下载；关闭此窗口后仍会继续。",
                  L"正在下載；關閉此視窗後仍會繼續。",
                  L"Downloading continues after this window is closed.")
            .c_str());
    ::SetWindowTextW(
        primary, LocalText(L"正在下载", L"正在下載", L"Downloading").c_str());
    ::EnableWindow(primary, FALSE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"取消下载", L"取消下載", L"Cancel download").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (waiting_retry) {
    ::SetDlgItemTextW(
        m_hWnd, IDC_MODEL_NOTE,
        LocalText(L"网络暂时不可用，系统将自动重试，已下载内容会保留。",
                  L"網路暫時無法使用，系統將自動重試，已下載內容會保留。",
                  L"The network is temporarily unavailable. The download "
                  L"will retry and keep its progress.")
            .c_str());
    ::SetWindowTextW(primary,
                     LocalText(L"立即重试", L"立即重試", L"Retry now").c_str());
    ::EnableWindow(primary, TRUE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"取消下载", L"取消下載", L"Cancel download").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (paused) {
    std::wstring note = LocalText(
        L"下载已暂停，已下载内容会保留。", L"下載已暫停，已下載內容會保留。",
        L"The download is paused and its progress is preserved.");
    if (FAILED(progress.error_code))
      note += L" " + ModelErrorText(progress.error_code);
    ::SetDlgItemTextW(m_hWnd, IDC_MODEL_NOTE, note.c_str());
    ::SetWindowTextW(primary,
                     LocalText(L"继续下载", L"繼續下載", L"Continue").c_str());
    ::EnableWindow(primary, TRUE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"取消下载", L"取消下載", L"Cancel download").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (transferred) {
    ::SetDlgItemTextW(
        m_hWnd, IDC_MODEL_NOTE,
        LocalText(L"下载完成，正在校验并安装。", L"下載完成，正在校驗並安裝。",
                  L"Download complete. Verifying and installing.")
            .c_str());
    ::SetWindowTextW(primary,
                     LocalText(L"正在校验", L"正在校驗", L"Verifying").c_str());
    ::EnableWindow(primary, FALSE);
    ::ShowWindow(secondary, SW_HIDE);
  } else if (progress.state == WanxiangModelManager::State::Installed) {
    ::SetDlgItemTextW(m_hWnd, IDC_MODEL_NOTE,
                      LocalText(L"语言模型已安装。", L"語言模型已安裝。",
                                L"The language model is installed.")
                          .c_str());
    ::SetWindowTextW(primary,
                     LocalText(L"已安装", L"已安裝", L"Installed").c_str());
    ::EnableWindow(primary, FALSE);
    ::SetWindowTextW(
        secondary,
        LocalText(L"移除模型", L"移除模型", L"Remove model").c_str());
    ::ShowWindow(secondary, SW_SHOW);
  } else if (progress.state == WanxiangModelManager::State::Modified) {
    ::SetDlgItemTextW(
        m_hWnd, IDC_MODEL_NOTE,
        LocalText(
            L"检测到用户修改的模型文件，不会自动覆盖。",
            L"偵測到使用者修改的模型檔案，不會自動覆蓋。",
            L"A modified model file was found and will not be overwritten.")
            .c_str());
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
    ::SetWindowTextW(
        secondary,
        LocalText(L"取消下载", L"取消下載", L"Cancel download").c_str());
    ::ShowWindow(secondary, SW_SHOW);
    ::SetDlgItemTextW(m_hWnd, IDC_MODEL_NOTE,
                      ModelErrorText(progress.error_code).c_str());
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
  model_progress_.Attach(GetDlgItem(IDC_MODEL_PROGRESS));
  model_progress_.SetRange32(0, 1000);

  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
  // Package importing remains hidden until conflict-safe transactional install
  // is implemented. Do not fall back to the legacy command window.
  ::ShowWindow(GetDlgItem(IDC_ADD_SCHEMA_GROUP), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_GET_SCHEMATA), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_ADD_SCHEME_URL), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_IMPORT_SCHEME), SW_HIDE);
  std::wstring last_release;
  SYSTEMTIME last_checked = {};
  if (WanxiangUpdateManager::LoadLastCheck(&last_release, &last_checked)) {
    wchar_t checked[64] = {};
    swprintf_s(checked, L"%04u-%02u-%02u %02u:%02u", last_checked.wYear,
               last_checked.wMonth, last_checked.wDay, last_checked.wHour,
               last_checked.wMinute);
    SetLastUpdateCheckText(
        LocalText(L"上次检查：", L"上次檢查：", L"Last checked: ") + checked +
        L" · " + last_release);
  } else {
    SetLastUpdateCheckText(LocalText(L"上次检查：尚未检查",
                                     L"上次檢查：尚未檢查",
                                     L"Last checked: not yet"));
  }

  Populate();
  SetTimer(kModelTimer, 500);

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (!ConfirmDiscardChanges())
    return 0;
  KillTimer(kModelTimer);
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (!ConfirmDiscardChanges())
    return 0;
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

LRESULT SwitcherSettingsDialog::OnCheckUpdates(WORD, WORD, HWND, BOOL&) {
  HWND button = GetDlgItem(IDC_CHECK_SCHEME_UPDATES);
  ::EnableWindow(button, FALSE);
  ::SetWindowTextW(button,
                   LocalText(L"正在检查…", L"正在檢查…", L"Checking…").c_str());
  SetLastUpdateCheckText(LocalText(L"正在检查可更新方案…",
                                   L"正在檢查可更新方案…",
                                   L"Checking managed schemas…"));
  RedrawWindow(nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

  const auto result = WanxiangUpdateManager::CheckNow();
  if (!result.success) {
    SetLastUpdateCheckText(LocalText(L"检查失败，请稍后重试",
                                     L"檢查失敗，請稍後重試",
                                     L"Check failed. Try again later."));
    LOG(ERROR) << "Unable to check Wanxiang releases: " << wtou8(result.error);
  } else if (!result.update_available) {
    SetLastUpdateCheckText(
        LocalText(L"刚刚检查 · 所有可更新方案均为最新版本",
                  L"剛剛檢查 · 所有可更新方案均為最新版本",
                  L"Checked just now · all managed schemas are current"));
  } else {
    SetLastUpdateCheckText(
        LocalText(L"刚刚检查 · 万象拼音 Lite 最新发布：",
                  L"剛剛檢查 · 萬象拼音 Lite 最新發佈：",
                  L"Checked just now · latest Wanxiang Lite release: ") +
        result.latest_tag);
  }
  ::SetWindowTextW(
      button,
      LocalText(L"立即检查更新", L"立即檢查更新", L"Check now").c_str());
  ::EnableWindow(button, TRUE);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnUpdateSettings(WORD,
                                                 WORD,
                                                 HWND control,
                                                 BOOL&) {
  if (selected_schema_ >= schemas_.size())
    return 0;
  const auto& schema = schemas_[selected_schema_];
  if (schema.id != "wanxiang_lite")
    return 0;

  const int current = LoadUpdateFrequency(schema.id);
  HMENU menu = CreatePopupMenu();
  if (!menu)
    return 0;
  const auto append = [&](int frequency, const std::wstring& label) {
    AppendMenuW(menu, MF_STRING | (current == frequency ? MF_CHECKED : 0),
                kUpdateMenuFirst + frequency, label.c_str());
  };
  append(kUpdateSmart, LocalText(L"智能", L"智慧", L"Smart"));
  append(kUpdateDaily, LocalText(L"每天", L"每天", L"Daily"));
  append(kUpdateWeekly, LocalText(L"每周", L"每週", L"Weekly"));
  append(kUpdateMonthly, LocalText(L"每月", L"每月", L"Monthly"));
  append(kUpdateDisabled, LocalText(L"关闭", L"關閉", L"Off"));

  RECT rect = {};
  ::GetWindowRect(control, &rect);
  const UINT command =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                     rect.left, rect.bottom, 0, m_hWnd, nullptr);
  DestroyMenu(menu);
  if (command < kUpdateMenuFirst ||
      command > kUpdateMenuFirst + kUpdateDisabled) {
    return 0;
  }
  const int frequency = static_cast<int>(command - kUpdateMenuFirst);
  if (!SaveUpdateFrequency(schema.id, frequency)) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法保存此方案的更新设置。", L"無法儲存此方案的更新設定。",
                  L"Unable to save update settings for this schema.")
            .c_str(),
        LocalText(L"保存失败", L"儲存失敗", L"Save failed").c_str(),
        MB_OK | MB_ICONERROR);
    return 0;
  }
  const auto label = LocalText(L"更新设置：", L"更新設定：", L"Update: ") +
                     UpdateFrequencyText(frequency);
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_UPDATE_SETTINGS, label.c_str());
  return 0;
}

LRESULT SwitcherSettingsDialog::OnProjectLink(int,
                                              LPNMHDR notification,
                                              BOOL&) {
  const auto* link = reinterpret_cast<PNMLINK>(notification);
  if (!link)
    return 0;
  const std::wstring url(link->item.szUrl);
  if (url.rfind(L"https://github.com/amzxyz/rime-wanxiang", 0) != 0 &&
      url.rfind(L"https://cnb.cool/amzxyz/rime-wanxiang", 0) != 0) {
    return 0;
  }
  ShellExecuteW(m_hWnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnModelPrimary(WORD, WORD, HWND, BOOL&) {
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
      state == WanxiangModelManager::State::WaitingRetry ||
      state == WanxiangModelManager::State::Paused ||
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
    api_->select_schemas(settings_, selection.data(),
                         static_cast<int>(selection.size()));
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
