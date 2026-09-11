#pragma once
#include "resource.h"
#include "UIStyleSettings.h"

class UIStyleSettingsDialog : public CDialogImpl<UIStyleSettingsDialog> {
 public:
  enum { IDD = IDD_STYLE_SETTING };
  explicit UIStyleSettingsDialog(UIStyleSettings* settings)
      : settings_(settings) {}
  bool saved() const { return saved_; }
  bool deployed() const { return deployed_; }

 protected:
  BEGIN_MSG_MAP(UIStyleSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnThemeChanged)
  COMMAND_ID_HANDLER(IDOK, OnSave)
  COMMAND_ID_HANDLER(IDC_APPLY, OnSave)
  COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  COMMAND_ID_HANDLER(IDC_RESTORE_APPEARANCE, OnReset)
  COMMAND_ID_HANDLER(IDC_ADVANCED_COLORS, OnAdvanced)
  COMMAND_HANDLER(IDC_SCHEME_SOURCE, CBN_SELCHANGE, OnSource)
  COMMAND_RANGE_HANDLER(IDC_ACRYLIC_GROUP, IDC_NORMAL_GROUP, OnGroup)
  COMMAND_RANGE_HANDLER(IDC_ACRYLIC_LIGHT, IDC_NORMAL_DARK, OnSingle)
  END_MSG_MAP()
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnSave(WORD, WORD, HWND, BOOL&);
  LRESULT OnCancel(WORD, WORD, HWND, BOOL&);
  LRESULT OnReset(WORD, WORD, HWND, BOOL&);
  LRESULT OnAdvanced(WORD, WORD, HWND, BOOL&);
  LRESULT OnSource(WORD, WORD, HWND, BOOL&);
  LRESULT OnGroup(WORD, WORD, HWND, BOOL&);
  LRESULT OnSingle(WORD, WORD, HWND, BOOL&);
  void FillGroups();
  void FillSingles();
  void RefreshStatus();
  void ShowAdvanced(bool show);
  CString Text(UINT id) const;
  UIStyleSettings* settings_;
  std::array<std::string, 4> colors_;
  bool saved_ = false;
  bool deployed_ = false;
  bool acrylic_ = true;
  bool ready_ = false;
};
