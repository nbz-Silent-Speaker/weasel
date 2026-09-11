#include "stdafx.h"
#include "WeaselDeployer.h"
#include "UIStyleSettingsDialog.h"
#include "UIStyleSettings.h"
#include "Configurator.h"
#include <WeaselUtility.h>

UIStyleSettingsDialog::UIStyleSettingsDialog(UIStyleSettings* settings)
    : settings_(settings), loaded_(false) {}

UIStyleSettingsDialog::~UIStyleSettingsDialog() {
  image_.Destroy();
}

void UIStyleSettingsDialog::Populate() {
  if (!settings_)
    return;
  std::string active(settings_->GetActiveColorScheme());
  int active_index = -1;
  settings_->GetPresetColorSchemes(&preset_);
  if (settings_->target() != weasel::ColorSchemeTarget::Default) {
    CString follow;
    follow.LoadString(IDS_STR_SCHEME_FOLLOW_CONFIG);
    preset_.insert(preset_.begin(), {"", wtou8(std::wstring(follow)), ""});
  }
  for (size_t i = 0; i < preset_.size(); ++i) {
    std::wstring txt = u8tow(preset_[i].name);
    color_schemes_.AddString(txt.c_str());
    if (preset_[i].color_scheme_id == active) {
      active_index = i;
    }
  }
  if (active_index < 0 &&
      settings_->target() != weasel::ColorSchemeTarget::Default)
    active_index = 0;
  if (active_index >= 0) {
    color_schemes_.SetCurSel(active_index);
    Preview(active_index);
  }
  loaded_ = true;
}

LRESULT UIStyleSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  color_schemes_.Attach(GetDlgItem(IDC_COLOR_SCHEME));
  preview_.Attach(GetDlgItem(IDC_PREVIEW));
  select_font_.Attach(GetDlgItem(IDC_SELECT_FONT));
  select_font_.EnableWindow(FALSE);
  if (settings_->target() != weasel::ColorSchemeTarget::Default) {
    CString title;
    UINT titleId = IDS_STR_SCHEME_NORMAL;
    switch (settings_->target()) {
      case weasel::ColorSchemeTarget::Acrylic:
        titleId = IDS_STR_SCHEME_ACRYLIC;
        break;
      case weasel::ColorSchemeTarget::AcrylicDark:
        titleId = IDS_STR_SCHEME_ACRYLIC_DARK;
        break;
      case weasel::ColorSchemeTarget::NormalDark:
        titleId = IDS_STR_SCHEME_NORMAL_DARK;
        break;
      default:
        break;
    }
    title.LoadString(titleId);
    SetWindowText(title);
    select_font_.ShowWindow(SW_HIDE);
  }

  Populate();

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT UIStyleSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnOK(WORD, WORD code, HWND, BOOL&) {
  if (code == IDOK) {
    const int index = color_schemes_.GetCurSel();
    if (index >= 0 && index < (int)preset_.size() &&
        !settings_->SelectColorScheme(preset_[index].color_scheme_id)) {
      MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
                 MB_OK | MB_ICONERROR);
      return 0;
    }
  }
  EndDialog(code);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnColorSchemeSelChange(WORD, WORD, HWND, BOOL&) {
  int index = color_schemes_.GetCurSel();
  if (index >= 0 && index < (int)preset_.size()) {
    Preview(index);
  }
  return 0;
}

void UIStyleSettingsDialog::Preview(int index) {
  if (index < 0 || index >= (int)preset_.size())
    return;
  preview_.SetBitmap(nullptr);
  image_.Destroy();
  if (preset_[index].color_scheme_id.empty())
    return;
  const std::string file_path(
      settings_->GetColorSchemePreview(preset_[index].color_scheme_id));
  if (file_path.empty())
    return;
  // it is from ansi coding, not utf8
  image_.Load(acptow(file_path).c_str());
  if (!image_.IsNull()) {
    preview_.SetBitmap(image_);
  }
}
