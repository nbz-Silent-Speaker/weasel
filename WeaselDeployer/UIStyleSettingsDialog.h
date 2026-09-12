#pragma once
#include "resource.h"
#include "UIStyleSettings.h"
#include <WeaselAppearanceDraft.h>

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
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  COMMAND_ID_HANDLER(IDOK, OnSave)
  COMMAND_ID_HANDLER(IDC_APPLY, OnSave)
  COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  COMMAND_ID_HANDLER(IDC_RESTORE_APPEARANCE, OnReset)
  COMMAND_ID_HANDLER(IDC_ACRYLIC_ENABLED, OnMaterial)
  COMMAND_ID_HANDLER(IDC_EDIT_GROUP, OnEditor)
  COMMAND_ID_HANDLER(IDC_EDIT_SINGLE, OnEditor)
  COMMAND_HANDLER(IDC_COLOR_FAMILY, CBN_SELCHANGE, OnGroup)
  COMMAND_RANGE_HANDLER(IDC_COLOR_LIGHT, IDC_COLOR_DARK, OnSingle)
  END_MSG_MAP()
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnStaticColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnSave(WORD, WORD, HWND, BOOL&);
  LRESULT OnCancel(WORD, WORD, HWND, BOOL&);
  LRESULT OnReset(WORD, WORD, HWND, BOOL&);
  LRESULT OnMaterial(WORD, WORD, HWND, BOOL&);
  LRESULT OnEditor(WORD, WORD, HWND, BOOL&);
  LRESULT OnGroup(WORD, WORD, HWND, BOOL&);
  LRESULT OnSingle(WORD, WORD, HWND, BOOL&);
  struct PaletteEntry {
    CString label;
    int index;
    int heading = 0;
  };
  std::vector<PaletteEntry>& Entries(UINT id);
  void AddEntry(CComboBox& combo, PaletteEntry entry);
  void FillGroups();
  void FillSingles();
  void RefreshMode();
  void RefreshPreview();
  void ShowEditor(bool single);
  void DrawCombo(const DRAWITEMSTRUCT& draw);
  CString Text(UINT id) const;
  UIStyleSettings* settings_;
  weasel::AppearanceDraft draft_;
  std::vector<PaletteEntry> groups_;
  std::array<std::vector<PaletteEntry>, 2> singles_;
  std::array<bool, 2> single_{};
  ULONG_PTR graphics_token_ = 0;
  CFont heading_font_;
  int item_height_ = 24;
  bool saved_ = false;
  bool deployed_ = false;
  bool ready_ = false;
};
