#pragma once

#include "resource.h"
#include <rime_levers_api.h>
#include <string>
#include <vector>

#include "WanxiangModelManager.h"

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
  COMMAND_ID_HANDLER(IDOK, OnOK)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, LVN_ITEMCHANGED, OnSchemaListItemChanged)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_CLICK, OnProjectLink)
  NOTIFY_HANDLER(IDC_SCHEMA_PROJECT_LINKS, NM_RETURN, OnProjectLink)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnCheckUpdates(WORD, WORD, HWND, BOOL&);
  LRESULT OnUpdateSettings(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelPrimary(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelSecondary(WORD, WORD, HWND, BOOL&);
  LRESULT OnOK(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnSchemaListItemChanged(int, LPNMHDR, BOOL&);
  LRESULT OnProjectLink(int, LPNMHDR, BOOL&);

  void Populate();
  void RebuildList();
  void ShowDetails(size_t index);
  void ShowModelControls(bool show);
  void UpdateModelUi();
  void FinishModelDownload();
  bool ConfirmDiscardChanges();
  int LoadUpdateFrequency(const std::string& schema_id) const;
  bool SaveUpdateFrequency(const std::string& schema_id, int frequency) const;
  std::wstring UpdateFrequencyText(int frequency) const;
  void SetLastUpdateCheckText(const std::wstring& text);
  std::wstring ModelErrorText(HRESULT error_code) const;
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

  CCheckListViewCtrl schema_list_;
  CEdit description_;
  CEdit hotkeys_;
  CProgressBarCtrl model_progress_;
};
