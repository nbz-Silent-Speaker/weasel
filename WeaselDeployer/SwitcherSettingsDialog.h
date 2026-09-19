#pragma once

#include "resource.h"
#include <atomic>
#include <memory>
#include <rime_levers_api.h>
#include <string>
#include <vector>

#include "WanxiangModelManager.h"
#include "WanxiangSchemeManager.h"
#include "WanxiangUpdateManager.h"
#include "SettingsNavigation.h"

class SwitcherSettingsDialog
    : public settings_navigation::HostedDialogImpl<SwitcherSettingsDialog> {
 public:
  enum { IDD = IDD_SWITCHER_SETTING };

  SwitcherSettingsDialog(RimeSwitcherSettings* settings);
  ~SwitcherSettingsDialog();
  bool HasUnappliedChanges() const;
  bool ApplyChanges();
  bool IsApplying() const {
    return apply_operation_ != nullptr ||
           (scheme_update_operation_ && !scheme_update_operation_->done.load());
  }
  bool ConfirmClose();
  void PrepareClose();

 protected:
  BEGIN_MSG_MAP(SwitcherSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
  MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
  COMMAND_HANDLER(IDC_CHECK_SCHEME_UPDATES, BN_CLICKED, OnCheckUpdates)
  COMMAND_HANDLER(IDC_SCHEMA_UPDATE_SETTINGS,
                  CBN_SELCHANGE,
                  OnUpdateSettingsChanged)
  COMMAND_HANDLER(IDC_MODEL_DOWNLOAD, BN_CLICKED, OnModelPrimary)
  COMMAND_HANDLER(IDC_MODEL_SECONDARY, BN_CLICKED, OnModelSecondary)
  COMMAND_HANDLER(IDC_INPUT_MODE, CBN_SELCHANGE, OnInputModeChanged)
  COMMAND_ID_HANDLER(IDOK, OnOK)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, LVN_ITEMCHANGED, OnSchemaListItemChanged)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, NM_CUSTOMDRAW, OnSchemaCustomDraw)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_CLICK, OnProjectLink)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_RETURN, OnProjectLink)
  NOTIFY_HANDLER(IDC_USER_DATA_FOLDER, NM_CLICK, OnUserFolderLink)
  NOTIFY_HANDLER(IDC_USER_DATA_FOLDER, NM_RETURN, OnUserFolderLink)
  NOTIFY_HANDLER(IDC_CHECK_SCHEME_UPDATES, NM_CUSTOMDRAW, OnButtonCustomDraw)
  NOTIFY_HANDLER(IDC_MODEL_DOWNLOAD, NM_CUSTOMDRAW, OnButtonCustomDraw)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnCtlColorStatic(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnCtlColorEdit(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnCheckUpdates(WORD, WORD, HWND, BOOL&);
  LRESULT OnUpdateSettingsChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelPrimary(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelSecondary(WORD, WORD, HWND, BOOL&);
  LRESULT OnInputModeChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnOK(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD id, HWND, BOOL&);
  LRESULT OnSchemaListItemChanged(int, LPNMHDR, BOOL&);
  LRESULT OnSchemaCustomDraw(int, LPNMHDR, BOOL&);
  LRESULT OnProjectLink(int, LPNMHDR, BOOL&);
  LRESULT OnUserFolderLink(int, LPNMHDR, BOOL&);
  LRESULT OnButtonCustomDraw(int, LPNMHDR, BOOL&);

  void Populate();
  void RebuildList();
  void ShowDetails(size_t index);
  void ShowModelControls(bool show);
  void UpdateModelUi(const WanxiangModelManager::Progress* snapshot = nullptr);
  bool ModelUiNeedsRefresh(
      const WanxiangModelManager::Progress& progress) const;
  void FinishModelDownload();
  void FinishApply();
  void SetApplyingUi(bool applying);
  void FinishUpdateCheck();
  void FinishSchemeUpdate();
  void StartSchemeUpdate(const WanxiangUpdateManager::SchemeRelease& release);
  void UpdateCheckButton();
  void UpdateLastCheckText();
  void ShowUpdateList();
  void AdjustInputModeWidth();
  void UpdateDescriptionLayout();
  void ApplyControlRounding();
  void ApplyRoundedRegion(HWND control, int radius_dlu);
  bool HasSchemaSelectionChanges() const;
  bool HasPendingChanges() const;
  void UpdateApplyButton();
  bool ConfirmDiscardChanges();
  void DiscardPendingModel();
  bool RestorePersistedSettings();
  void CommitAppliedBaseline();
  int LoadUpdateFrequency(const std::string& schema_id) const;
  bool SaveUpdateFrequency(const std::string& schema_id, int frequency) const;
  std::wstring UpdateFrequencyText(int frequency) const;
  std::wstring ModelErrorText(HRESULT error_code) const;
  bool LoadInputMode(std::wstring* mode) const;
  bool SaveInputMode(const std::wstring& mode, std::wstring* error) const;
  std::wstring LocalText(const wchar_t* simplified,
                         const wchar_t* traditional,
                         const wchar_t* english) const;

  struct SchemaEntry {
    RimeSchemaInfo* info = nullptr;
    std::string id;
    std::wstring name;
    bool enabled = false;
    bool initial_enabled = false;
  };

  enum class PendingModelAction { None, Install, Remove };

  RimeLeversApi* api_;
  RimeSwitcherSettings* settings_;
  bool loaded_;
  bool modified_;
  size_t selected_schema_ = static_cast<size_t>(-1);
  std::vector<SchemaEntry> schemas_;
  WanxiangModelManager model_manager_;
  struct UpdateCheck {
    WanxiangUpdateManager::Result result;
    std::atomic<bool> done{false};
  };
  std::shared_ptr<UpdateCheck> update_check_;
  struct SchemeUpdateOperation {
    enum class Phase {
      Preparing,
      Downloading,
      Verifying,
      Extracting,
      Installing,
      Deploying,
      RollingBack,
      Finalizing,
    };
    bool success = false;
    bool restored = false;
    std::wstring error;
    std::wstring installed_tag;
    std::atomic<Phase> phase{Phase::Preparing};
    std::atomic<unsigned long long> completed{0};
    std::atomic<unsigned long long> total{0};
    std::atomic<bool> done{false};
  };
  std::shared_ptr<SchemeUpdateOperation> scheme_update_operation_;
  struct ApplyOperation {
    int result = 1;
    PendingModelAction model_action = PendingModelAction::None;
    bool model_operation_failed = false;
    std::wstring model_operation_error;
    std::atomic<bool> done{false};
  };
  std::shared_ptr<ApplyOperation> apply_operation_;
  bool close_after_apply_ = false;
  bool model_operation_failed_ = false;
  bool scheme_update_available_ = false;
  bool model_update_available_ = false;
  bool open_update_list_after_check_ = false;
  bool check_button_accent_ = false;
  bool model_button_accent_ = false;
  bool model_status_active_ = false;
  PendingModelAction pending_model_action_ = PendingModelAction::None;
  unsigned long long latest_model_size_ = 0;
  std::wstring latest_release_tag_;
  WanxiangUpdateManager::SchemeRelease latest_scheme_release_;
  std::wstring latest_model_sha256_;
  std::wstring model_download_phase_;
  std::wstring model_download_speed_;
  std::wstring model_download_amount_;
  std::wstring model_progress_percent_;
  bool model_ui_snapshot_valid_ = false;
  WanxiangModelManager::State model_ui_state_ =
      WanxiangModelManager::State::NotInstalled;
  unsigned long long model_ui_transferred_ = 0;
  unsigned long long model_ui_total_ = 0;
  HRESULT model_ui_error_code_ = S_OK;
  BG_ERROR_CONTEXT model_ui_error_context_ = BG_ERROR_CONTEXT_NONE;
  bool loading_input_mode_ = false;
  bool input_mode_modified_ = false;
  std::wstring selected_input_mode_ = L"全拼";
  std::wstring initial_input_mode_ = L"全拼";
  std::wstring tooltip_text_;
  int selected_update_frequency_ = 2;
  int initial_update_frequency_ = 2;
  bool update_frequency_modified_ = false;
  ULONGLONG last_progress_tick_ = 0;
  unsigned long long last_progress_bytes_ = 0;
  CRect model_secondary_rect_;
  CRect model_primary_rect_;
  CRect model_active_secondary_rect_;
  CRect model_active_primary_rect_;
  CRect input_mode_base_rect_;

  CCheckListViewCtrl schema_list_;
  CEdit description_;
  CProgressBarCtrl model_progress_;
  CComboBox input_mode_;
  CComboBox update_frequency_;
  CToolTipCtrl tooltip_;
  CFont heading_font_;
  CBrush description_brush_;
};
