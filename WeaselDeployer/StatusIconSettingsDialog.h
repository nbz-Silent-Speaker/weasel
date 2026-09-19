#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <WeaselUserSettings.h>
#include <WeaselInputMethodIcon.h>

#include <array>
#include <string>
#include <vector>

class StatusIconSettingsDialog
    : public settings_navigation::HostedDialogImpl<StatusIconSettingsDialog> {
 public:
  enum { IDD = IDD_STATUS_ICON_SETTING };
  bool HasUnappliedChanges() const;
  bool ApplyChanges();
  bool ConfirmClose();

 protected:
  BEGIN_MSG_MAP(StatusIconSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  MESSAGE_HANDLER(WM_CTLCOLORBTN, OnButtonColor)
  COMMAND_ID_HANDLER(IDC_STATUS_CHINESE_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_STATUS_ENGLISH_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_STATUS_CHINESE_CAPS_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_INPUT_METHOD_CHANGE, OnChooseInputMethodIcon)
  COMMAND_ID_HANDLER(IDC_INPUT_METHOD_RESTORE, OnRestoreInputMethodIcon)
  COMMAND_RANGE_HANDLER(IDC_STATUS_CAPS_AUTOMATIC,
                        IDC_STATUS_CAPS_CUSTOM,
                        OnScopeChanged)
  COMMAND_HANDLER(IDC_STATUS_SCHEMA_COMBO, CBN_SELCHANGE, OnSchemaChanged)
  COMMAND_RANGE_HANDLER(IDC_STATUS_CHINESE_INHERIT,
                        IDC_STATUS_CAPS_INHERIT,
                        OnUseGlobal)
  COMMAND_RANGE_HANDLER(IDC_STATUS_PREVIEW_CHINESE,
                        IDC_STATUS_PREVIEW_CHINESE_CAPS,
                        OnPreviewModeChanged)
  COMMAND_RANGE_HANDLER(IDC_STATUS_PREVIEW_LIGHT,
                        IDC_STATUS_PREVIEW_DARK,
                        OnPreviewThemeChanged)
  COMMAND_ID_HANDLER(IDC_STATUS_RESTORE, OnRestore)
  COMMAND_ID_HANDLER(IDC_STATUS_APPLY, OnApply)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  END_MSG_MAP()

  enum class PreviewMode { Chinese, Western, Caps };
  enum class EditScope { Global, Schema };

  struct SchemaEntry {
    std::wstring id;
    std::wstring name;
    weasel::SchemaStatusIconSettings initial;
    weasel::SchemaStatusIconSettings draft;
  };

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnStaticColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnButtonColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnChooseIcon(WORD, WORD id, HWND, BOOL&);
  LRESULT OnChooseInputMethodIcon(WORD, WORD, HWND, BOOL&);
  LRESULT OnRestoreInputMethodIcon(WORD, WORD, HWND, BOOL&);
  LRESULT OnScopeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnSchemaChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnUseGlobal(WORD, WORD id, HWND, BOOL&);
  LRESULT OnPreviewModeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnPreviewThemeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnRestore(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD id, HWND, BOOL&);

  void Localize();
  void PopulateSchemas();
  void RefreshScope();
  void RefreshPreviews();
  void RefreshApplyState();
  void SetPreviewIcon(UINT control, HICON icon);
  void DrawTaskbarPreview(const DRAWITEMSTRUCT& draw);
  HICON ResolveIcon(const std::wstring& custom, bool english, bool caps) const;
  HICON ResolveActiveIcon(size_t state) const;
  bool ChooseIconFile(std::wstring* path,
                      bool english,
                      bool caps,
                      bool inherit_global);
  bool Persist(std::wstring* error);
  std::wstring ImportIcon(const std::wstring& source,
                          std::wstring* error) const;
  bool ConfirmDiscard();
  std::wstring LocalText(const wchar_t* simplified,
                         const wchar_t* traditional,
                         const wchar_t* english) const;
  SchemaEntry* SelectedSchema();
  const SchemaEntry* SelectedSchema() const;
  bool HasSchemaChanges() const;

  weasel::StatusIconSettings initial_;
  weasel::StatusIconSettings draft_;
  weasel::InputMethodIconSettings initial_input_method_;
  weasel::InputMethodIconSettings draft_input_method_;
  std::vector<SchemaEntry> schemas_;
  EditScope edit_scope_ = EditScope::Global;
  PreviewMode preview_mode_ = PreviewMode::Chinese;
  bool preview_dark_ = false;
  ULONG_PTR graphics_token_ = 0;
  std::array<HICON, 5> preview_icons_{};
  CComboBox schema_combo_;
  CFont heading_font_;
};
