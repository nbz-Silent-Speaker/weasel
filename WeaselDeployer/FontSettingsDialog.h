#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <WeaselUserSettings.h>

#include <array>
#include <string>
#include <vector>

class FontSettingsDialog : public CDialogImpl<FontSettingsDialog> {
 public:
  enum { IDD = IDD_FONT_SETTING };

 protected:
  BEGIN_MSG_MAP(FontSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  COMMAND_HANDLER(IDC_FONT_ROLE, LBN_SELCHANGE, OnSelectionChanged)
  COMMAND_HANDLER(IDC_FONT_LANGUAGE, LBN_SELCHANGE, OnSelectionChanged)
  COMMAND_HANDLER(IDC_FONT_FAMILY, LBN_SELCHANGE, OnValueChanged)
  COMMAND_HANDLER(IDC_FONT_POINT, LBN_SELCHANGE, OnValueChanged)
  COMMAND_HANDLER(IDC_FONT_SEARCH, EN_CHANGE, OnSearchChanged)
  COMMAND_RANGE_HANDLER(IDC_FONT_SHAPE_REGULAR,
                        IDC_FONT_SHAPE_ITALIC,
                        OnShapeChanged)
  COMMAND_RANGE_HANDLER(IDC_FONT_PREVIEW_HORIZONTAL,
                        IDC_FONT_PREVIEW_VERTICAL,
                        OnPreviewLayoutChanged)
  COMMAND_ID_HANDLER(IDC_FONT_RESTORE, OnRestore)
  COMMAND_ID_HANDLER(IDC_FONT_APPLY, OnApply)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnSelectionChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnValueChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnSearchChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnShapeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnPreviewLayoutChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnRestore(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD id, HWND, BOOL&);

  void Localize();
  void EnumerateFonts();
  void PopulateSelectors();
  void PopulateFontList();
  void LoadCurrentChoice();
  void StoreCurrentChoice();
  void RefreshShapeSupport();
  void RefreshApplyState();
  void RefreshPreview();
  void DrawPreview(const DRAWITEMSTRUCT& draw);
  void ApplyRoundedRegion(UINT control, int radius_dlu);
  bool ConfirmDiscard();
  std::wstring LocalText(const wchar_t* simplified,
                         const wchar_t* traditional,
                         const wchar_t* english) const;

  weasel::FontSettings initial_;
  weasel::FontSettings draft_;
  std::vector<std::wstring> fonts_;
  size_t role_ = static_cast<size_t>(weasel::FontRole::Candidate);
  size_t language_ = static_cast<size_t>(weasel::FontLanguage::Chinese);
  bool loading_ = false;
  bool preview_vertical_ = false;
  CFont title_font_;
  CFont heading_font_;
};
