#include "stdafx.h"
#include "WeaselDeployer.h"
#include "UIStyleSettingsDialog.h"
#include "Configurator.h"
#include <WeaselUtility.h>
#include <WeaselUserSettings.h>
#include <algorithm>

CString UIStyleSettingsDialog::Text(UINT id) const {
  CString text;
  text.LoadString(id);
  return text;
}

LRESULT UIStyleSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  if (!settings_->LoadAppearance()) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    EndDialog(IDCANCEL);
    return TRUE;
  }
  colors_ = settings_->ActiveAppearance();
  CTabCtrl pages(GetDlgItem(IDC_SETTINGS_PAGES));
  TCITEMW page{};
  page.mask = TCIF_TEXT;
  CString title = Text(IDS_APPEARANCE_TITLE);
  page.pszText = title.GetBuffer();
  pages.InsertItem(0, &page);
  title.ReleaseBuffer();
  acrylic_ = weasel::UserSettings::Load().acrylic;
  CheckDlgButton(IDC_ACRYLIC_ENABLED, acrylic_ ? BST_CHECKED : BST_UNCHECKED);
  CComboBox source(GetDlgItem(IDC_SCHEME_SOURCE));
  for (UINT id :
       {IDS_APPEARANCE_ALL, IDS_APPEARANCE_BASE, IDS_APPEARANCE_CUSTOM})
    source.AddString(Text(id));
  source.SetCurSel(0);
  FillGroups();
  FillSingles();
  ShowAdvanced(false);
  RefreshStatus();
  ready_ = true;
  CenterWindow();
  return TRUE;
}

void UIStyleSettingsDialog::FillGroups() {
  const int filter = CComboBox(GetDlgItem(IDC_SCHEME_SOURCE)).GetCurSel();
  const auto& groups = settings_->groups();
  for (int mode = 0; mode < 2; ++mode) {
    CComboBox combo(GetDlgItem(IDC_ACRYLIC_GROUP + mode));
    combo.ResetContent();
    combo.SetItemData(combo.AddString(Text(IDS_APPEARANCE_MIXED)),
                      static_cast<DWORD_PTR>(-1));
    int selected = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
      const auto& group = groups[i];
      const bool active = group.light == colors_[mode * 2] &&
                          group.dark == colors_[mode * 2 + 1];
      if (!active &&
          ((filter == 1 && group.custom) || (filter == 2 && !group.custom)))
        continue;
      CString label(u8tow(group.name).c_str());
      label += L"  \u00b7  ";
      label += Text(group.custom ? IDS_APPEARANCE_CUSTOM : IDS_APPEARANCE_BASE);
      const int item = combo.AddString(label);
      combo.SetItemData(item, i);
      if (active)
        selected = item;
    }
    combo.SetCurSel(selected);
  }
}

void UIStyleSettingsDialog::FillSingles() {
  const int filter = CComboBox(GetDlgItem(IDC_SCHEME_SOURCE)).GetCurSel();
  for (int slot = 0; slot < 4; ++slot) {
    CComboBox combo(GetDlgItem(IDC_ACRYLIC_LIGHT + slot));
    combo.ResetContent();
    combo.SetItemData(combo.AddString(Text(IDS_STR_SCHEME_FOLLOW_CONFIG)),
                      static_cast<DWORD_PTR>(-1));
    int selected = 0;
    for (size_t i = 0; i < settings_->schemes().size(); ++i) {
      const auto& scheme = settings_->schemes()[i];
      const bool active = scheme.color_scheme_id == colors_[slot];
      if (!active &&
          ((filter == 1 && scheme.custom) || (filter == 2 && !scheme.custom)))
        continue;
      CString label =
          Text(scheme.custom ? IDS_APPEARANCE_CUSTOM : IDS_APPEARANCE_BASE);
      label += L" \u00b7 ";
      label += u8tow(scheme.name).c_str();
      const int item = combo.AddString(label);
      combo.SetItemData(item, i);
      if (scheme.color_scheme_id == colors_[slot])
        selected = item;
    }
    combo.SetCurSel(selected);
  }
}

void UIStyleSettingsDialog::ShowAdvanced(bool show) {
  CheckDlgButton(IDC_ADVANCED_COLORS, show ? BST_CHECKED : BST_UNCHECKED);
  for (int id = IDC_ACRYLIC_LIGHT; id <= IDC_NORMAL_DARK; ++id)
    ::ShowWindow(GetDlgItem(id), show ? SW_SHOW : SW_HIDE);
  for (int id : {IDC_LIGHT_LABEL, IDC_DARK_LABEL, IDC_ADV_ACRYLIC_LABEL,
                 IDC_ADV_NORMAL_LABEL, IDC_ADVANCED_HINT})
    ::ShowWindow(GetDlgItem(id), show ? SW_SHOW : SW_HIDE);
}

void UIStyleSettingsDialog::RefreshStatus() {
  DWORD light = 1, size = sizeof(light);
  ::RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  const bool acrylic = weasel::UserSettings::Load().acrylic;
  auto actual = settings_->ActiveAppearance();
  const int slot = (acrylic ? 0 : 2) + (light ? 0 : 1);
  CString status = Text(IDS_APPEARANCE_CURRENT);
  status += Text(acrylic ? IDS_APPEARANCE_ACRYLIC : IDS_APPEARANCE_NORMAL);
  status += L" \u00b7 ";
  status += Text(light ? IDS_APPEARANCE_LIGHT : IDS_APPEARANCE_DARK);
  status += L" \u00b7 ";
  status += actual[slot].empty() ? Text(IDS_STR_SCHEME_FOLLOW_CONFIG)
                                 : CString(u8tow(actual[slot]).c_str());
  SetDlgItemText(IDC_ACTIVE_APPEARANCE, status);
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_LIGHT), nullptr, TRUE);
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_DARK), nullptr, TRUE);
}

LRESULT UIStyleSettingsDialog::OnGroup(WORD notification,
                                       WORD id,
                                       HWND,
                                       BOOL&) {
  if (!ready_ || notification != CBN_SELCHANGE)
    return 0;
  CComboBox combo(GetDlgItem(id));
  const auto item = combo.GetItemData(combo.GetCurSel());
  if (item == static_cast<DWORD_PTR>(-1)) {
    ShowAdvanced(true);
    return 0;
  }
  if (item >= settings_->groups().size())
    return 0;
  const int slot = (id - IDC_ACRYLIC_GROUP) * 2;
  const auto& group = settings_->groups()[item];
  colors_[slot] = group.light;
  colors_[slot + 1] = group.dark;
  FillSingles();
  RefreshStatus();
  return 0;
}
LRESULT UIStyleSettingsDialog::OnSingle(WORD notification,
                                        WORD id,
                                        HWND,
                                        BOOL&) {
  if (!ready_ || notification != CBN_SELCHANGE)
    return 0;
  CComboBox combo(GetDlgItem(id));
  const auto item = combo.GetItemData(combo.GetCurSel());
  colors_[id - IDC_ACRYLIC_LIGHT] =
      item < settings_->schemes().size()
          ? settings_->schemes()[item].color_scheme_id
          : "";
  FillGroups();
  RefreshStatus();
  return 0;
}
LRESULT UIStyleSettingsDialog::OnSource(WORD, WORD, HWND, BOOL&) {
  FillGroups();
  FillSingles();
  return 0;
}
LRESULT UIStyleSettingsDialog::OnAdvanced(WORD, WORD, HWND, BOOL&) {
  ShowAdvanced(IsDlgButtonChecked(IDC_ADVANCED_COLORS) == BST_CHECKED);
  return 0;
}
LRESULT UIStyleSettingsDialog::OnReset(WORD, WORD, HWND, BOOL&) {
  colors_ = {"Fluent_light", "Fluent_dark", "Fluent_light", "Fluent_dark"};
  CheckDlgButton(IDC_ACRYLIC_ENABLED, BST_CHECKED);
  CComboBox(GetDlgItem(IDC_SCHEME_SOURCE)).SetCurSel(0);
  FillGroups();
  FillSingles();
  RefreshStatus();
  return 0;
}
LRESULT UIStyleSettingsDialog::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
  if (ready_)
    RefreshStatus();
  return 0;
}
LRESULT UIStyleSettingsDialog::OnCancel(WORD, WORD, HWND, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}
LRESULT UIStyleSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnSave(WORD, WORD id, HWND, BOOL&) {
  const bool desired = IsDlgButtonChecked(IDC_ACRYLIC_ENABLED) == BST_CHECKED;
  const bool before = weasel::UserSettings::Load().acrylic;
  const bool changeMaterial = desired != acrylic_;
  if ((changeMaterial &&
       weasel::UserSettingsStore().WriteBool(weasel::kAcrylicEnabledSetting,
                                             desired) != ERROR_SUCCESS) ||
      !settings_->SaveAppearance(colors_)) {
    if (changeMaterial)
      weasel::UserSettingsStore().WriteBool(weasel::kAcrylicEnabledSetting,
                                            before);
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    return 0;
  }
  saved_ = true;
  weasel::NotifyUserSettingsChanged();
  if (settings_->configuration_changed()) {
    if (Configurator().UpdateWorkspace(true) != 0)
      return 0;
    deployed_ = true;
  }
  auto api = static_cast<RimeLeversApi*>(
      rime_get_api()->find_module("levers")->get_api());
  if (!api->load_settings(settings_->settings()) ||
      !settings_->LoadAppearance()) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    EndDialog(IDOK);
    return 0;
  }
  const auto applied = settings_->ActiveAppearance();
  for (size_t i = 0; i < colors_.size(); ++i) {
    if (!colors_[i].empty() && applied[i] != colors_[i]) {
      MSG_BY_IDS(IDS_STR_SCHEME_DEPLOY_FAILED, IDS_STR_WEASEL,
                 MB_OK | MB_ICONERROR);
      return 0;
    }
  }
  acrylic_ = weasel::UserSettings::Load().acrylic;
  colors_ = settings_->ActiveAppearance();
  CheckDlgButton(IDC_ACRYLIC_ENABLED, acrylic_ ? BST_CHECKED : BST_UNCHECKED);
  FillGroups();
  FillSingles();
  RefreshStatus();
  if (id == IDOK)
    EndDialog(IDOK);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnDrawItem(UINT,
                                          WPARAM,
                                          LPARAM lParam,
                                          BOOL& handled) {
  const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
  if (draw->CtlID != IDC_PREVIEW_LIGHT && draw->CtlID != IDC_PREVIEW_DARK) {
    handled = FALSE;
    return 0;
  }
  const bool dark = draw->CtlID == IDC_PREVIEW_DARK;
  // Palette previews intentionally do not simulate a live desktop backdrop.
  const int saved = ::SaveDC(draw->hDC);
  ::FillRect(draw->hDC, &draw->rcItem, ::GetSysColorBrush(COLOR_WINDOW));
  const int height = (draw->rcItem.bottom - draw->rcItem.top) / 2;
  for (int mode = 0; mode < 2; ++mode) {
    const auto& scheme = colors_[mode * 2 + (dark ? 1 : 0)];
    RECT area = draw->rcItem;
    area.top += mode * height;
    area.bottom = area.top + height;
    ::InflateRect(&area, -6, -6);
    auto brush = ::CreateSolidBrush(settings_->PreviewColor(
        scheme, "back_color", dark ? RGB(40, 40, 40) : RGB(249, 249, 249)));
    ::FillRect(draw->hDC, &area, brush);
    ::DeleteObject(brush);
    ::SetBkMode(draw->hDC, TRANSPARENT);
    ::SetTextColor(draw->hDC, settings_->PreviewColor(
                                  scheme, "text_color",
                                  dark ? RGB(240, 240, 240) : RGB(32, 32, 32)));
    ::SelectObject(draw->hDC, GetFont());
    ::InflateRect(&area, -10, -4);
    ::DrawTextW(
        draw->hDC,
        Text(mode == 0 ? IDS_APPEARANCE_ACRYLIC : IDS_APPEARANCE_NORMAL), -1,
        &area, DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    TEXTMETRICW metrics{};
    ::GetTextMetricsW(draw->hDC, &metrics);
    area.top += metrics.tmHeight + 8;
    area.bottom = (std::min)(area.bottom, area.top + metrics.tmHeight + 12);
    brush = ::CreateSolidBrush(
        settings_->PreviewColor(scheme, "hilited_candidate_back_color",
                                dark ? RGB(66, 66, 66) : RGB(229, 229, 229)));
    ::FillRect(draw->hDC, &area, brush);
    ::DeleteObject(brush);
    ::SetTextColor(draw->hDC, settings_->PreviewColor(
                                  scheme, "hilited_candidate_text_color",
                                  dark ? RGB(255, 255, 255) : RGB(17, 17, 17)));
    ::DrawTextW(draw->hDC, Text(IDS_APPEARANCE_SAMPLE), -1, &area,
                DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  }
  ::RestoreDC(draw->hDC, saved);
  return TRUE;
}
