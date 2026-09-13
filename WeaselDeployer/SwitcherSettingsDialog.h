#pragma once

#include "resource.h"
#include <atomic>
#include <memory>
#include <rime_levers_api.h>
#include <string>
#include <vector>

#include "WanxiangModelManager.h"
#include "WanxiangUpdateManager.h"

class SwitcherSettingsDialog : public CDialogImpl<SwitcherSettingsDialog> {
 public:
  enum { IDD = IDD_SWITCHER_SETTING };

  SwitcherSettingsDialog(RimeSwitcherSettings* settings);
  ~SwitcherSettingsDialog();

 protected:
  BEGIN_MSG_MAP(SwitcherSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  COMMAND_HANDLER(IDC_CHECK_SCHEME_UPDATES, BN_CLICKED, OnCheckUpdates)
  COMMAND_HANDLER(IDC_SCHEMA_UPDATE_SETTINGS, BN_CLICKED, OnUpdateSettings)
  COMMAND_HANDLER(IDC_MODEL_DOWNLOAD, BN_CLICKED, OnModelPrimary)
  COMMAND_HANDLER(IDC_MODEL_SECONDARY, BN_CLICKED, OnModelSecondary)
  COMMAND_HANDLER(IDC_INPUT_MODE, CBN_SELCHANGE, OnInputModeChanged)
  COMMAND_ID_HANDLER(IDOK, OnOK)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, LVN_ITEMCHANGED, OnSchemaListItemChanged)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_CLICK, OnProjectLink)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_RETURN, OnProjectLink)
  NOTIFY_HANDLER(IDC_USER_DATA_FOLDER, NM_CLICK, OnUserFolderLink)
  NOTIFY_HANDLER(IDC_USER_DATA_FOLDER, NM_RETURN, OnUserFolderLink)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnCheckUpdates(WORD, WORD, HWND, BOOL&);
  LRESULT OnUpdateSettings(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelPrimary(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelSecondary(WORD, WORD, HWND, BOOL&);
  LRESULT OnInputModeChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnOK(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnSchemaListItemChanged(int, LPNMHDR, BOOL&);
  LRESULT OnProjectLink(int, LPNMHDR, BOOL&);
  LRESULT OnUserFolderLink(int, LPNMHDR, BOOL&);

  void Populate();
  void RebuildList();
  void ShowDetails(size_t index);
  void ShowModelControls(bool show);
  void UpdateModelUi();
  void FinishModelDownload();
  void FinishUpdateCheck();
  bool ConfirmDiscardChanges();
  int LoadUpdateFrequency(const std::string& schema_id) const;
  bool SaveUpdateFrequency(const std::string& schema_id, int frequency) const;
  std::wstring UpdateFrequencyText(int frequency) const;
  void SetLastUpdateCheckText(const std::wstring& text);
  std::wstring ModelErrorText(HRESULT error_code) const;
  std::wstring FormatSwitcherHotkeys(const std::wstring& hotkeys) const;
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
  };

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
  bool model_install_failed_ = false;
  bool loading_input_mode_ = false;
  bool input_mode_modified_ = false;
  std::wstring selected_input_mode_ = L"全拼";
  CRect model_secondary_rect_;
  CRect model_primary_rect_;

  CCheckListViewCtrl schema_list_;
  CEdit description_;
  CEdit hotkeys_;
  CProgressBarCtrl model_progress_;
  CComboBox input_mode_;
};
