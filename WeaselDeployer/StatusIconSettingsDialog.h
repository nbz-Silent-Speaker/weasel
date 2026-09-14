#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <WeaselUserSettings.h>

#include <array>
#include <string>

class StatusIconSettingsDialog : public CDialogImpl<StatusIconSettingsDialog> {
 public:
  enum { IDD = IDD_STATUS_ICON_SETTING };

 protected:
  BEGIN_MSG_MAP(StatusIconSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  MESSAGE_HANDLER(WM_CTLCOLORBTN, OnButtonColor)
  COMMAND_ID_HANDLER(IDC_STATUS_CHINESE_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_STATUS_ENGLISH_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_STATUS_CHINESE_CAPS_CHANGE, OnChooseIcon)
  COMMAND_ID_HANDLER(IDC_STATUS_ENGLISH_CAPS_CHANGE, OnChooseIcon)
  COMMAND_RANGE_HANDLER(IDC_STATUS_CAPS_AUTOMATIC,
                        IDC_STATUS_CAPS_CUSTOM,
                        OnCapsModeChanged)
  COMMAND_RANGE_HANDLER(IDC_STATUS_BADGE_LETTER,
                        IDC_STATUS_BADGE_DOT,
                        OnBadgeChanged)
  COMMAND_RANGE_HANDLER(IDC_STATUS_PREVIEW_CHINESE,
                        IDC_STATUS_PREVIEW_ENGLISH_CAPS,
                        OnPreviewModeChanged)
  COMMAND_ID_HANDLER(IDC_STATUS_RESTORE, OnRestore)
  COMMAND_ID_HANDLER(IDC_STATUS_APPLY, OnApply)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  END_MSG_MAP()

  enum class PreviewMode { Chinese, English, ChineseCaps, EnglishCaps };

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnStaticColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnButtonColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnChooseIcon(WORD, WORD id, HWND, BOOL&);
  LRESULT OnCapsModeChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnBadgeChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnPreviewModeChanged(WORD, WORD id, HWND, BOOL&);
  LRESULT OnRestore(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD id, HWND, BOOL&);

  void Localize();
  void RefreshMode();
  void RefreshPreviews();
  void RefreshApplyState();
  void SetPreviewIcon(UINT control, HICON icon);
  void DrawTaskbarPreview(const DRAWITEMSTRUCT& draw);
  HICON ResolveIcon(const std::wstring& custom, bool english, bool caps) const;
  bool ChooseIconFile(std::wstring* path);
  bool Persist(std::wstring* error);
  std::wstring ImportIcon(const std::wstring& source,
                          const wchar_t* name,
                          std::wstring* error) const;
  bool ConfirmDiscard();
  std::wstring LocalText(const wchar_t* simplified,
                         const wchar_t* traditional,
                         const wchar_t* english) const;

  weasel::StatusIconSettings initial_;
  weasel::StatusIconSettings draft_;
  PreviewMode preview_mode_ = PreviewMode::Chinese;
  ULONG_PTR graphics_token_ = 0;
  std::array<HICON, 7> preview_icons_{};
  CFont heading_font_;
};
