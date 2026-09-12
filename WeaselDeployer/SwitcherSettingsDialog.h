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
  COMMAND_HANDLER(IDC_GET_SCHEMATA, BN_CLICKED, OnGetSchemata)
  COMMAND_HANDLER(IDC_SCHEMA_SEARCH, EN_CHANGE, OnSearchChanged)
  COMMAND_HANDLER(IDC_MODEL_DOWNLOAD, BN_CLICKED, OnModelPrimary)
  COMMAND_HANDLER(IDC_MODEL_SECONDARY, BN_CLICKED, OnModelSecondary)
  COMMAND_ID_HANDLER(IDOK, OnOK)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, LVN_ITEMCHANGED, OnSchemaListItemChanged)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnGetSchemata(WORD, WORD, HWND, BOOL&);
  LRESULT OnSearchChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelPrimary(WORD, WORD, HWND, BOOL&);
  LRESULT OnModelSecondary(WORD, WORD, HWND, BOOL&);
  LRESULT OnOK(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnSchemaListItemChanged(int, LPNMHDR, BOOL&);

  void Populate();
  void RebuildFilteredList();
  void ShowDetails(size_t index);
  void ShowModelControls(bool show);
  void UpdateModelUi();
  void FinishModelDownload();
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
  CStatic description_;
  CEdit hotkeys_;
  CButton get_schemata_;
  CEdit search_;
  CProgressBarCtrl model_progress_;
};
