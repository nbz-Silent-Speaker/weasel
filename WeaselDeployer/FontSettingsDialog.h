#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <WeaselUserSettings.h>

#include <array>
#include <string>
#include <vector>

class UIStyleSettings;

class FontSettingsDialog
    : public settings_navigation::HostedDialogImpl<FontSettingsDialog> {
 public:
  enum { IDD = IDD_FONT_SETTING };
  explicit FontSettingsDialog(UIStyleSettings* appearance_settings)
      : appearance_settings_(appearance_settings) {}
  bool HasUnappliedChanges() const;
  bool ApplyChanges();
  bool ConfirmClose();

 protected:
  BEGIN_MSG_MAP(FontSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  MESSAGE_HANDLER(WM_CTLCOLORBTN, OnButtonColor)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  COMMAND_HANDLER(IDC_FONT_ROLE_LIST, LBN_SELCHANGE, OnRoleChanged)
  COMMAND_HANDLER(IDC_FONT_FAMILY_CHINESE,
                  CBN_SELCHANGE,
                  OnLanguageValueChanged)
  COMMAND_HANDLER(IDC_FONT_FAMILY_LATIN, CBN_SELCHANGE, OnLanguageValueChanged)
  COMMAND_HANDLER(IDC_FONT_STYLE_LIST, LBN_SELCHANGE, OnSharedValueChanged)
  COMMAND_HANDLER(IDC_FONT_SIZE_LIST, LBN_SELCHANGE, OnSharedValueChanged)
  COMMAND_RANGE_HANDLER(IDC_FONT_PREVIEW_HORIZONTAL,
                        IDC_FONT_PREVIEW_VERTICAL,
                        OnPreviewLayoutChanged)
  COMMAND_RANGE_HANDLER(IDC_FONT_PREVIEW_LIGHT,
                        IDC_FONT_PREVIEW_DARK,
                        OnPreviewThemeChanged)
  COMMAND_ID_HANDLER(IDC_FONT_RESTORE, OnRestore)
  COMMAND_ID_HANDLER(IDC_FONT_APPLY, OnApply)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnStaticColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnButtonColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM id, LPARAM, BOOL&);
  LRESULT OnRoleChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnLanguageValueChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnSharedValueChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnPreviewLayoutChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnPreviewThemeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnRestore(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD id, HWND, BOOL&);

  void Localize();
  void EnumerateFonts();
  void PopulateSelectors();
  void LoadRoleChoices();
  void LoadLanguageChoice(size_t language);
  void LoadSharedChoice();
  void StoreLanguageChoice(size_t language, WORD changed_id);
  void RefreshApplyState();
  void RefreshPreview();
  void BeginRolePreviewPulse();
  void DrawPreview(const DRAWITEMSTRUCT& draw);
  weasel::FontSettings AppearanceDefaults() const;
  bool ConfirmDiscard();
  std::wstring LocalText(const wchar_t* simplified,
                         const wchar_t* traditional,
                         const wchar_t* english) const;

  weasel::FontSettings initial_;
  weasel::FontSettings draft_;
  std::vector<std::wstring> fonts_;
  size_t role_ = static_cast<size_t>(weasel::FontRole::Candidate);
  bool loading_ = false;
  bool preview_vertical_ = false;
  bool preview_dark_ = false;
  ULONGLONG role_preview_pulse_started_ = 0;
  UIStyleSettings* appearance_settings_ = nullptr;
};
