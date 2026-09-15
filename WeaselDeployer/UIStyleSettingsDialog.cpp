#include "stdafx.h"
#include "WeaselDeployer.h"
#include "UIStyleSettingsDialog.h"
#include "Configurator.h"
#include "AppearancePreview.h"
#include <WeaselUtility.h>
#include <WeaselUserSettings.h>

namespace {
constexpr int kSettingsColumnWidthDlu = 198;
constexpr int kColumnGapDlu = 12;
constexpr int kPreviewColumnLeftDlu = settings_navigation::kPageInsetDlu +
                                      kSettingsColumnWidthDlu + kColumnGapDlu;
constexpr int kPreviewColumnWidthDlu = 302;
constexpr int kPaletteCardTopDlu = 90;
constexpr int kPaletteCardHeightDlu = 158;

HWND CreateControl(HWND dialog,
                   const wchar_t* class_name,
                   const std::wstring& text,
                   DWORD style,
                   WORD id) {
  return settings_navigation::Create(dialog, class_name, text, style, id, 0, 0,
                                     1, 1);
}

void EnsureAppearanceControls(HWND dialog) {
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_ACRYLIC_CARD))
    CreateControl(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
                  IDC_APPEARANCE_ACRYLIC_CARD);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_THEME_CARD))
    CreateControl(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
                  IDC_APPEARANCE_THEME_CARD);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_CARD))
    CreateControl(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
                  IDC_APPEARANCE_CARD);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_ACRYLIC_LABEL))
    CreateControl(dialog, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
                  IDC_APPEARANCE_ACRYLIC_LABEL);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_ACRYLIC_STATE))
    CreateControl(dialog, L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE,
                  IDC_APPEARANCE_ACRYLIC_STATE);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_THEME_LABEL))
    CreateControl(dialog, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
                  IDC_APPEARANCE_THEME_LABEL);
  if (!::GetDlgItem(dialog, IDC_APPEARANCE_THEME_MODE))
    CreateControl(dialog, L"COMBOBOX", L"",
                  CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS |
                      WS_VSCROLL | WS_TABSTOP,
                  IDC_APPEARANCE_THEME_MODE);
}

void LayoutAppearancePage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_RESTORE_APPEARANCE,
              settings_navigation::kBottomActionLeftDlu,
              settings_navigation::kBottomActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_APPEARANCE_ACRYLIC_CARD,
              settings_navigation::kPageInsetDlu,
              settings_navigation::kFirstCardTopDlu, kSettingsColumnWidthDlu,
              settings_navigation::kSingleRowCardHeightDlu);
  MoveControl(dialog, IDC_APPEARANCE_THEME_CARD,
              settings_navigation::kPageInsetDlu, 52, kSettingsColumnWidthDlu,
              settings_navigation::kSingleRowCardHeightDlu);
  MoveControl(dialog, IDC_APPEARANCE_CARD, settings_navigation::kPageInsetDlu,
              kPaletteCardTopDlu, kSettingsColumnWidthDlu,
              kPaletteCardHeightDlu);
  MoveControl(dialog, IDC_APPEARANCE_ACRYLIC_LABEL, 26, 24, 116, 12);
  MoveControl(dialog, IDC_APPEARANCE_ACRYLIC_STATE, 164, 24, 20, 12);
  MoveControl(dialog, IDC_ACRYLIC_ENABLED, 188, 22, 14, 16);
  MoveControl(dialog, IDC_APPEARANCE_THEME_LABEL, 26, 62, 70, 12);
  MoveControl(dialog, IDC_APPEARANCE_THEME_MODE, 114, 59, 88, 80);
  MoveControl(dialog, IDC_EDIT_GROUP, 26, 103, 54,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_EDIT_SINGLE,
              26 + 54 + settings_navigation::kToggleGapDlu, 103, 54,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_LIGHT_LABEL, 26, 157, 46, 12);
  MoveControl(dialog, IDC_DARK_LABEL, 26, 188, 46, 12);
  MoveControl(dialog, IDC_COLOR_FAMILY, 78, 154, 124, 100);
  MoveControl(dialog, IDC_COLOR_LIGHT, 78, 154, 124, 100);
  MoveControl(dialog, IDC_COLOR_DARK, 78, 185, 124, 100);
  MoveControl(dialog, IDC_SELECTION_HINT, 26, 222, 176, 18);
  MoveControl(dialog, IDC_PREVIEW_LIGHT, kPreviewColumnLeftDlu,
              settings_navigation::kFirstCardTopDlu, kPreviewColumnWidthDlu,
              113);
  MoveControl(dialog, IDC_PREVIEW_DARK, kPreviewColumnLeftDlu, 135,
              kPreviewColumnWidthDlu, 113);
}
}  // namespace

CString UIStyleSettingsDialog::Text(UINT id) const {
  CString text;
  text.LoadString(id);
  return text;
}

LRESULT UIStyleSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  EnsureAppearanceControls(m_hWnd);
  LayoutAppearancePage(m_hWnd);
  Gdiplus::GdiplusStartupInput startup;
  if (Gdiplus::GdiplusStartup(&graphics_token_, &startup, nullptr) !=
          Gdiplus::Ok ||
      !settings_->LoadAppearance()) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    EndDialog(IDCANCEL);
    return TRUE;
  }
  const auto user_settings = weasel::UserSettings::Load();
  draft_.Load(settings_->ActiveAppearance(), user_settings.acrylic,
              user_settings.appearance_theme_mode);
  single_.fill(user_settings.appearance_theme_mode !=
               weasel::AppearanceThemeMode::FollowSystem);
  ::SetDlgItemTextW(m_hWnd, IDC_RESTORE_APPEARANCE,
                    settings_navigation::LocalText(
                        L"恢复本页默认", L"還原本頁預設", L"Restore this page")
                        .c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_APPEARANCE_ACRYLIC_LABEL,
      settings_navigation::LocalText(L"亚克力磨砂效果", L"壓克力毛玻璃效果",
                                     L"Acrylic effect")
          .c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_APPEARANCE_THEME_LABEL,
                    settings_navigation::LocalText(L"界面模式", L"介面模式",
                                                   L"Interface mode")
                        .c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_ACRYLIC_ENABLED, L"");
  ::SetDlgItemTextW(
      m_hWnd, IDC_EDIT_GROUP,
      settings_navigation::LocalText(L"成组设置", L"成組設定", L"Paired")
          .c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_EDIT_SINGLE,
      settings_navigation::LocalText(L"单独设置", L"個別設定", L"Separate")
          .c_str());
  for (WORD id : {IDC_APPEARANCE_ACRYLIC_CARD, IDC_APPEARANCE_THEME_CARD,
                  IDC_APPEARANCE_CARD}) {
    settings_navigation::StyleCard(m_hWnd, id);
    ::SetWindowPos(::GetDlgItem(m_hWnd, id), HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  settings_navigation::StyleActionButton(m_hWnd, IDC_RESTORE_APPEARANCE);
  settings_navigation::StyleSwitch(m_hWnd, IDC_ACRYLIC_ENABLED);
  settings_navigation::StyleSegmentedToggle(m_hWnd, IDC_EDIT_GROUP);
  settings_navigation::StyleSegmentedToggle(m_hWnd, IDC_EDIT_SINGLE);
  for (UINT id : {IDC_APPEARANCE_THEME_MODE, IDC_COLOR_FAMILY, IDC_COLOR_LIGHT,
                  IDC_COLOR_DARK})
    settings_navigation::StyleCombo(m_hWnd, id);
  for (WORD id : {IDC_PREVIEW_LIGHT, IDC_PREVIEW_DARK}) {
    LONG_PTR style = ::GetWindowLongPtrW(::GetDlgItem(m_hWnd, id), GWL_STYLE);
    ::SetWindowLongPtrW(::GetDlgItem(m_hWnd, id), GWL_STYLE,
                        style & ~static_cast<LONG_PTR>(WS_BORDER));
    settings_navigation::StyleCard(m_hWnd, id);
  }
  RECT unit{0, 0, 0, 14};
  MapDialogRect(&unit);
  item_height_ = unit.bottom;
  for (UINT id : {IDC_APPEARANCE_THEME_MODE, IDC_COLOR_FAMILY, IDC_COLOR_LIGHT,
                  IDC_COLOR_DARK})
    CComboBox(GetDlgItem(id)).SetItemHeight(-1, item_height_);
  RefreshMode();
  ready_ = true;
  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Appearance,
      {IDC_APPLY,
       IDCANCEL,
       WeaselUserDataPath().wstring(),
       {IDOK, IDC_APPEARANCE_TITLE, IDC_SETTINGS_DIVIDER, IDC_MATERIAL_HINT,
        IDC_EDITOR_HINT, IDC_PREVIEW_HINT, IDC_APPEARANCE_LIGHT_PREVIEW_LABEL,
        IDC_APPEARANCE_DARK_PREVIEW_LABEL}});
  RefreshPreview();
  ::RedrawWindow(m_hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  CenterWindow();
  return TRUE;
}

std::vector<UIStyleSettingsDialog::PaletteEntry>&
UIStyleSettingsDialog::Entries(UINT id) {
  if (id == IDC_APPEARANCE_THEME_MODE)
    return theme_modes_;
  return id == IDC_COLOR_FAMILY ? groups_ : singles_[id - IDC_COLOR_LIGHT];
}

void UIStyleSettingsDialog::AddEntry(CComboBox& combo, PaletteEntry entry) {
  auto& entries = Entries(combo.GetDlgCtrlID());
  // Insert the description first: CB_ADDSTRING may synchronously measure it.
  entries.push_back(entry);
  const int item = combo.AddString(entry.label);
  if (item >= 0)
    combo.SetItemHeight(item, item_height_ * (entry.heading ? 2 : 1));
}

void UIStyleSettingsDialog::FillGroups() {
  CComboBox combo(GetDlgItem(IDC_COLOR_FAMILY));
  combo.ResetContent();
  groups_.clear();
  const auto& groups = settings_->groups();
  int active = -1;
  for (size_t i = 0; i < groups.size(); ++i) {
    if (groups[i].light == draft_.current(false) &&
        groups[i].dark == draft_.current(true)) {
      active = static_cast<int>(i);
      break;
    }
  }
  int selected = 0;
  if (active < 0)
    AddEntry(combo, {Text(IDS_APPEARANCE_MIXED), -1});
  for (bool custom : {false, true}) {
    bool first = true;
    for (size_t i = 0; i < groups.size(); ++i) {
      const auto& group = groups[i];
      if (group.custom != custom)
        continue;
      if (static_cast<int>(i) == active)
        selected = static_cast<int>(groups_.size());
      AddEntry(
          combo,
          {CString(u8tow(group.name).c_str()), static_cast<int>(i),
           first ? (custom ? IDS_APPEARANCE_CUSTOM : IDS_APPEARANCE_BASE) : 0});
      first = false;
    }
  }
  combo.SetCurSel(selected);
}

void UIStyleSettingsDialog::FillSingles() {
  for (int dark = 0; dark < 2; ++dark) {
    CComboBox combo(GetDlgItem(IDC_COLOR_LIGHT + dark));
    combo.ResetContent();
    singles_[dark].clear();
    int selected = -1;
    const auto current = draft_.current(dark != 0);
    for (bool custom : {false, true}) {
      bool first = true;
      for (size_t i = 0; i < settings_->schemes().size(); ++i) {
        const auto& scheme = settings_->schemes()[i];
        const bool isCurrent = scheme.color_scheme_id == current;
        const bool matches =
            weasel::PaletteMatchesTheme(scheme.theme, dark != 0);
        if (scheme.custom != custom || (!matches && !isCurrent))
          continue;
        if (isCurrent)
          selected = static_cast<int>(singles_[dark].size());
        CString label(u8tow(scheme.name).c_str());
        if (!matches)
          label += Text(scheme.theme == weasel::PaletteTheme::Dark
                            ? IDS_PALETTE_CURRENT_DARK
                            : IDS_PALETTE_CURRENT_LIGHT);
        AddEntry(combo,
                 {label, static_cast<int>(i),
                  first ? (custom ? IDS_APPEARANCE_CUSTOM : IDS_APPEARANCE_BASE)
                        : 0});
        first = false;
      }
    }
    if (selected < 0) {
      selected = static_cast<int>(singles_[dark].size());
      CString label = current.empty()
                          ? Text(IDS_PALETTE_CHOOSE)
                          : CString(u8tow(weasel::PaletteId(current)).c_str()) +
                                Text(IDS_PALETTE_UNAVAILABLE);
      AddEntry(combo, {label, -1});
    }
    combo.SetCurSel(selected);
  }
}

void UIStyleSettingsDialog::FillThemeMode() {
  CComboBox combo(GetDlgItem(IDC_APPEARANCE_THEME_MODE));
  combo.ResetContent();
  theme_modes_.clear();
  const wchar_t* labels[][3] = {
      {L"跟随系统", L"跟隨系統", L"Follow system"},
      {L"始终浅色", L"始終淺色", L"Always light"},
      {L"始终深色", L"始終深色", L"Always dark"},
  };
  for (int index = 0; index < 3; ++index) {
    AddEntry(combo,
             {CString(settings_navigation::LocalText(
                          labels[index][0], labels[index][1], labels[index][2])
                          .c_str()),
              index});
  }
  combo.SetCurSel(static_cast<int>(draft_.theme_mode()));
}

void UIStyleSettingsDialog::RefreshMode() {
  CheckDlgButton(IDC_ACRYLIC_ENABLED,
                 draft_.acrylic() ? BST_CHECKED : BST_UNCHECKED);
  ::SetDlgItemTextW(
      m_hWnd, IDC_APPEARANCE_ACRYLIC_STATE,
      settings_navigation::LocalText(draft_.acrylic() ? L"开" : L"关",
                                     draft_.acrylic() ? L"開" : L"關",
                                     draft_.acrylic() ? L"On" : L"Off")
          .c_str());
  SetDlgItemText(IDC_MATERIAL_HINT,
                 Text(draft_.acrylic() ? IDS_APPEARANCE_ACRYLIC_HINT
                                       : IDS_APPEARANCE_NORMAL_HINT));
  SetDlgItemText(IDC_PREVIEW_HINT,
                 Text(draft_.acrylic() ? IDS_APPEARANCE_ACRYLIC_PREVIEW
                                       : IDS_APPEARANCE_NORMAL_PREVIEW));
  FillGroups();
  FillSingles();
  FillThemeMode();
  ShowEditor(single_[draft_.acrylic() ? 0 : 1]);
  RefreshPreview();
}

void UIStyleSettingsDialog::ShowEditor(bool single) {
  single_[draft_.acrylic() ? 0 : 1] = single;
  CheckDlgButton(IDC_EDIT_GROUP, single ? BST_UNCHECKED : BST_CHECKED);
  CheckDlgButton(IDC_EDIT_SINGLE, single ? BST_CHECKED : BST_UNCHECKED);
  ::SetDlgItemTextW(
      m_hWnd, IDC_LIGHT_LABEL,
      settings_navigation::LocalText(single ? L"浅色" : L"浅色/深色",
                                     single ? L"淺色" : L"淺色/深色",
                                     single ? L"Light" : L"Light / dark")
          .c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_DARK_LABEL,
      settings_navigation::LocalText(L"深色", L"深色", L"Dark").c_str());
  HWND family = GetDlgItem(IDC_COLOR_FAMILY);
  HWND light = GetDlgItem(IDC_COLOR_LIGHT);
  HWND dark = GetDlgItem(IDC_COLOR_DARK);
  HWND light_label = GetDlgItem(IDC_LIGHT_LABEL);
  HWND dark_label = GetDlgItem(IDC_DARK_LABEL);
  for (HWND combo : {family, light, dark})
    ::ShowWindow(combo, SW_HIDE);
  ::ShowWindow(single ? light : family, SW_SHOW);
  if (single)
    ::ShowWindow(dark, SW_SHOW);
  ::ShowWindow(light_label, SW_SHOW);
  ::ShowWindow(dark_label, single ? SW_SHOW : SW_HIDE);
  RefreshThemeAvailability();
  RefreshPreview();
  HWND card = GetDlgItem(IDC_APPEARANCE_CARD);
  if (card) {
    ::SetWindowPos(card, HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::RedrawWindow(card, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
  }
  const HWND controls[] = {light_label, dark_label, family, light, dark};
  for (HWND control : controls) {
    if (::IsWindowVisible(control))
      ::RedrawWindow(control, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
  }
}

void UIStyleSettingsDialog::RefreshThemeAvailability() {
  const bool single = IsDlgButtonChecked(IDC_EDIT_SINGLE) == BST_CHECKED;
  const auto mode = draft_.theme_mode();
  const bool light_enabled =
      !single || mode != weasel::AppearanceThemeMode::Dark;
  const bool dark_enabled =
      !single || mode != weasel::AppearanceThemeMode::Light;
  ::EnableWindow(GetDlgItem(IDC_COLOR_LIGHT), light_enabled);
  ::EnableWindow(GetDlgItem(IDC_COLOR_DARK), dark_enabled);
}
void UIStyleSettingsDialog::RefreshPreview() {
  bool mismatch = false;
  for (bool dark : {false, true}) {
    const auto current = draft_.current(dark);
    for (const auto& scheme : settings_->schemes()) {
      if (scheme.color_scheme_id == current &&
          !weasel::PaletteMatchesTheme(scheme.theme, dark))
        mismatch = true;
    }
  }
  CString message;
  if (mismatch)
    message = Text(IDS_PALETTE_MISMATCH);
  SetDlgItemText(IDC_SELECTION_HINT, message);
  ::ShowWindow(GetDlgItem(IDC_SELECTION_HINT), mismatch ? SW_SHOW : SW_HIDE);
  ::EnableWindow(GetDlgItem(IDC_APPLY), draft_.changed());
  settings_navigation::SetStatus(
      m_hWnd,
      draft_.changed()
          ? settings_navigation::LocalText(L"有更改待应用", L"有變更待套用",
                                           L"Changes ready to apply")
          : settings_navigation::LocalText(L"所有设置已应用", L"所有設定已套用",
                                           L"All settings applied"));
  if (ready_)
    PreparePreviews();
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_LIGHT), nullptr, FALSE);
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_DARK), nullptr, FALSE);
}

LRESULT UIStyleSettingsDialog::OnMaterial(WORD, WORD, HWND, BOOL&) {
  if (ready_) {
    draft_.SetAcrylic(IsDlgButtonChecked(IDC_ACRYLIC_ENABLED) == BST_CHECKED);
    InvalidatePreviewCache();
    RefreshMode();
  }
  return 0;
}

LRESULT UIStyleSettingsDialog::OnThemeMode(WORD notification,
                                           WORD,
                                           HWND,
                                           BOOL&) {
  if (!ready_ || notification != CBN_SELCHANGE)
    return 0;
  const int selected =
      CComboBox(GetDlgItem(IDC_APPEARANCE_THEME_MODE)).GetCurSel();
  if (selected < 0 || selected > 2)
    return 0;
  draft_.SetThemeMode(static_cast<weasel::AppearanceThemeMode>(selected));
  const bool single =
      selected != static_cast<int>(weasel::AppearanceThemeMode::FollowSystem);
  single_.fill(single);
  ShowEditor(single);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnGroup(WORD, WORD, HWND, BOOL&) {
  if (!ready_)
    return 0;
  const int selected = CComboBox(GetDlgItem(IDC_COLOR_FAMILY)).GetCurSel();
  if (selected < 0 || static_cast<size_t>(selected) >= groups_.size())
    return 0;
  const int index = groups_[selected].index;
  if (index < 0) {
    ShowEditor(true);
    return 0;
  }
  const auto& group = settings_->groups()[index];
  draft_.SelectPair(group.light, group.dark);
  InvalidatePreviewCache();
  FillGroups();
  FillSingles();
  RefreshPreview();
  return 0;
}

LRESULT UIStyleSettingsDialog::OnSingle(WORD notification,
                                        WORD id,
                                        HWND,
                                        BOOL&) {
  if (!ready_ || notification != CBN_SELCHANGE)
    return 0;
  CComboBox combo(GetDlgItem(id));
  const int selected = combo.GetCurSel();
  const auto& entries = Entries(id);
  if (selected < 0 || static_cast<size_t>(selected) >= entries.size())
    return 0;
  const int index = entries[selected].index;
  if (index < 0)
    return 0;
  draft_.SelectSingle(id == IDC_COLOR_DARK,
                      settings_->schemes()[index].color_scheme_id);
  InvalidatePreviewCache();
  FillGroups();
  FillSingles();
  RefreshPreview();
  return 0;
}

LRESULT UIStyleSettingsDialog::OnEditor(WORD, WORD id, HWND, BOOL&) {
  ShowEditor(id == IDC_EDIT_SINGLE);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnReset(WORD, WORD, HWND, BOOL&) {
  draft_.Reset();
  single_.fill(false);
  InvalidatePreviewCache();
  RefreshMode();
  return 0;
}

LRESULT UIStyleSettingsDialog::OnCancel(WORD, WORD, HWND, BOOL&) {
  if (ConfirmDiscard() && !settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (ConfirmDiscard() && !settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

bool UIStyleSettingsDialog::ConfirmDiscard() const {
  if (!draft_.changed())
    return true;
  return ::MessageBoxW(
             m_hWnd,
             settings_navigation::LocalText(
                 L"候选框设置尚未应用。是否放弃这些更改？",
                 L"候選框設定尚未套用。是否放棄這些變更？",
                 L"Candidate window changes have not been applied. Discard "
                 L"them?")
                 .c_str(),
             settings_navigation::LocalText(L"未应用的设置", L"未套用的設定",
                                            L"Unapplied settings")
                 .c_str(),
             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

LRESULT UIStyleSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::Appearance)
    return 0;
  if (!ConfirmDiscard()) {
    return 0;
  }
  if (settings_navigation::RequestNavigate(m_hWnd, id))
    return 0;
  EndDialog(id);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
  for (HBITMAP& bitmap : preview_bitmaps_) {
    if (bitmap)
      ::DeleteObject(bitmap);
    bitmap = nullptr;
  }
  if (graphics_token_)
    Gdiplus::GdiplusShutdown(graphics_token_);
  graphics_token_ = 0;
  handled = FALSE;
  return 0;
}

LRESULT UIStyleSettingsDialog::OnSave(WORD, WORD id, HWND, BOOL&) {
  const bool desired = draft_.acrylic();
  const auto desired_theme = draft_.theme_mode();
  const auto before = weasel::UserSettings::Load();
  const bool change_material = desired != draft_.saved_acrylic();
  const bool change_theme = desired_theme != draft_.saved_theme_mode();
  weasel::UserSettingsStore store;
  const bool settings_saved =
      (!change_material || store.WriteBool(weasel::kAcrylicEnabledSetting,
                                           desired) == ERROR_SUCCESS) &&
      (!change_theme ||
       store.WriteDword(weasel::kAppearanceThemeModeSetting,
                        static_cast<DWORD>(desired_theme)) == ERROR_SUCCESS);
  if (!settings_saved || !settings_->SaveAppearance(draft_.colors())) {
    if (change_material)
      store.WriteBool(weasel::kAcrylicEnabledSetting, before.acrylic);
    if (change_theme)
      store.WriteDword(weasel::kAppearanceThemeModeSetting,
                       static_cast<DWORD>(before.appearance_theme_mode));
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
  auto api = reinterpret_cast<RimeLeversApi*>(
      rime_get_api()->find_module("levers")->get_api());
  if (!api->load_settings(settings_->settings()) ||
      !settings_->LoadAppearance()) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
    if (!settings_navigation::RequestClose(m_hWnd, IDOK))
      EndDialog(IDOK);
    return 0;
  }
  const auto applied = settings_->ActiveAppearance();
  for (size_t i = 0; i < draft_.colors().size(); ++i) {
    if (!draft_.colors()[i].empty() && applied[i] != draft_.colors()[i]) {
      MSG_BY_IDS(IDS_STR_SCHEME_DEPLOY_FAILED, IDS_STR_WEASEL,
                 MB_OK | MB_ICONERROR);
      return 0;
    }
  }
  const auto saved_settings = weasel::UserSettings::Load();
  draft_.Load(applied, saved_settings.acrylic,
              saved_settings.appearance_theme_mode);
  InvalidatePreviewCache();
  RefreshMode();
  if (id == IDOK && !settings_navigation::RequestClose(m_hWnd, IDOK))
    EndDialog(IDOK);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnMeasureItem(UINT,
                                             WPARAM,
                                             LPARAM param,
                                             BOOL& handled) {
  const auto measure = reinterpret_cast<MEASUREITEMSTRUCT*>(param);
  if (measure->CtlID != IDC_APPEARANCE_THEME_MODE &&
      measure->CtlID != IDC_COLOR_FAMILY && measure->CtlID != IDC_COLOR_LIGHT &&
      measure->CtlID != IDC_COLOR_DARK) {
    handled = FALSE;
    return 0;
  }
  const auto& entries = Entries(measure->CtlID);
  const bool heading =
      measure->itemID < entries.size() && entries[measure->itemID].heading != 0;
  measure->itemHeight = item_height_ * (heading ? 2 : 1);
  return TRUE;
}

LRESULT UIStyleSettingsDialog::OnStaticColor(UINT,
                                             WPARAM dc,
                                             LPARAM window,
                                             BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  const bool content = id == IDC_MATERIAL_HINT || id == IDC_EDITOR_HINT ||
                       id == IDC_PREVIEW_HINT || id == IDC_SELECTION_HINT ||
                       id == IDC_LIGHT_LABEL || id == IDC_DARK_LABEL ||
                       id == IDC_APPEARANCE_ACRYLIC_LABEL ||
                       id == IDC_APPEARANCE_ACRYLIC_STATE ||
                       id == IDC_APPEARANCE_THEME_LABEL ||
                       id == IDC_APPEARANCE_LIGHT_PREVIEW_LABEL ||
                       id == IDC_APPEARANCE_DARK_PREVIEW_LABEL;
  if (!content) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  if (id == IDC_MATERIAL_HINT || id == IDC_EDITOR_HINT ||
      id == IDC_PREVIEW_HINT || id == IDC_SELECTION_HINT)
    ::SetTextColor(context, ::GetSysColor(COLOR_GRAYTEXT));
  if ((id == IDC_LIGHT_LABEL &&
       !::IsWindowEnabled(GetDlgItem(IDC_COLOR_LIGHT))) ||
      (id == IDC_DARK_LABEL && !::IsWindowEnabled(GetDlgItem(IDC_COLOR_DARK))))
    ::SetTextColor(context, ::GetSysColor(COLOR_GRAYTEXT));
  ::SetBkMode(context, TRANSPARENT);
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

LRESULT UIStyleSettingsDialog::OnButtonColor(UINT,
                                             WPARAM dc,
                                             LPARAM window,
                                             BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if (id != IDC_ACRYLIC_ENABLED && id != IDC_EDIT_GROUP &&
      id != IDC_EDIT_SINGLE) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkColor(context, ::GetSysColor(COLOR_WINDOW));
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

void UIStyleSettingsDialog::DrawCombo(const DRAWITEMSTRUCT& draw) {
  const auto& entries = Entries(draw.CtlID);
  const int saved = ::SaveDC(draw.hDC);
  ::FillRect(draw.hDC, &draw.rcItem, ::GetSysColorBrush(COLOR_WINDOW));
  if (draw.itemID < entries.size()) {
    const auto& entry = entries[draw.itemID];
    RECT row = draw.rcItem;
    const bool field = (draw.itemState & ODS_COMBOBOXEDIT) != 0;
    ::SelectObject(draw.hDC, GetFont());
    ::SetBkMode(draw.hDC, TRANSPARENT);
    // Headings belong to the first selectable row in each group, so keyboard
    // navigation cannot accidentally select a heading as a palette.
    if (!field && entry.heading) {
      RECT heading = row;
      heading.bottom = heading.top + item_height_;
      heading.left += 7;
      ::SetTextColor(draw.hDC, ::GetSysColor(COLOR_GRAYTEXT));
      ::DrawTextW(draw.hDC, Text(entry.heading), -1, &heading,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
      row.top += item_height_;
    }
    const bool selected = (draw.itemState & ODS_SELECTED) != 0 && !field;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const COLORREF surface = ::GetSysColor(COLOR_WINDOW);
    const COLORREF selected_fill =
        settings_navigation::Mix(::GetSysColor(COLOR_3DSHADOW), surface, 25);
    ::FillRect(draw.hDC, &row, ::GetSysColorBrush(COLOR_WINDOW));
    RECT selection = row;
    if (selected) {
      selection.left += 3;
      selection.top += 1;
      selection.right -= 3;
      selection.bottom -= 1;
      HBRUSH selection_brush = ::CreateSolidBrush(selected_fill);
      HPEN selection_pen = ::CreatePen(PS_NULL, 0, selected_fill);
      const HGDIOBJ old_brush = ::SelectObject(draw.hDC, selection_brush);
      const HGDIOBJ old_pen = ::SelectObject(draw.hDC, selection_pen);
      const int radius =
          (std::max)(6, ::MulDiv(8, ::GetDeviceCaps(draw.hDC, LOGPIXELSX), 96));
      ::RoundRect(draw.hDC, selection.left, selection.top, selection.right,
                  selection.bottom, radius, radius);
      ::SelectObject(draw.hDC, old_pen);
      ::SelectObject(draw.hDC, old_brush);
      ::DeleteObject(selection_pen);
      ::DeleteObject(selection_brush);
    }
    ::SetTextColor(draw.hDC,
                   ::GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    if (selected) {
      const int marker_width =
          (std::max)(2, ::MulDiv(3, ::GetDeviceCaps(draw.hDC, LOGPIXELSX), 96));
      RECT marker{selection.left + 3,
                  selection.top + (selection.bottom - selection.top) / 4,
                  selection.left + 3 + marker_width,
                  row.bottom - (row.bottom - row.top) / 4};
      HBRUSH marker_brush = ::CreateSolidBrush(::GetSysColor(COLOR_HIGHLIGHT));
      HRGN marker_region =
          ::CreateRoundRectRgn(marker.left, marker.top, marker.right,
                               marker.bottom, marker_width, marker_width);
      if (marker_region) {
        ::FillRgn(draw.hDC, marker_region, marker_brush);
        ::DeleteObject(marker_region);
      }
      ::DeleteObject(marker_brush);
    }
    RECT label = row;
    label.left += field ? 5 : 13;
    label.right -= 5;
    ::DrawTextW(draw.hDC, entry.label, -1, &label,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  ::RestoreDC(draw.hDC, saved);
}

void UIStyleSettingsDialog::InvalidatePreviewCache() {
  preview_dirty_.fill(true);
}

weasel::AppearancePreview UIStyleSettingsDialog::PreviewStyle(bool dark) {
  weasel::AppearancePreview preview{};
  preview.dark = dark;
  preview.acrylic = draft_.acrylic();
  preview.horizontal = settings_->PreviewStyleBool("horizontal", false);
  auto scheme = draft_.current(preview.dark);
  if (scheme.empty()) {
    // "Follow configuration" still previews the effective original choice.
    const auto actual = settings_->ActiveAppearance();
    scheme = actual[draft_.offset() + (preview.dark ? 1 : 0)];
    if (scheme.empty() && preview.acrylic)
      scheme = actual[preview.dark ? 3 : 2];
  }
  const auto color = [&](const char* key, COLORREF light, COLORREF dark_color) {
    return settings_->PreviewColor(scheme, key,
                                   preview.dark ? dark_color : light);
  };
  preview.background = color("back_color", RGB(249, 249, 249), RGB(44, 44, 44));
  preview.border = color("border_color", RGB(213, 213, 213), RGB(80, 80, 80));
  preview.text =
      color("candidate_text_color", RGB(32, 32, 32), RGB(242, 242, 242));
  preview.label = color("label_color", RGB(104, 104, 104), RGB(176, 176, 176));
  preview.highlight = color("hilited_candidate_back_color", RGB(229, 229, 229),
                            RGB(66, 66, 66));
  preview.highlighted_text = color("hilited_candidate_text_color",
                                   RGB(17, 17, 17), RGB(255, 255, 255));
  preview.highlighted_label =
      color("hilited_label_color", RGB(0, 103, 192), RGB(96, 205, 255));
  preview.mark =
      color("hilited_mark_color", RGB(0, 103, 192), RGB(96, 205, 255));
  preview.radius =
      static_cast<float>(settings_->PreviewLayoutInt("corner_radius", 11));
  preview.highlight_radius =
      static_cast<float>(settings_->PreviewLayoutInt("round_corner", 8));
  preview.border_width =
      static_cast<float>(settings_->PreviewLayoutInt("border_width", 1));
  preview.min_width = settings_->PreviewLayoutInt("min_width", 130);
  preview.max_width = settings_->PreviewLayoutInt("max_width", 0);
  preview.margin_x = settings_->PreviewLayoutInt("margin_x", 11);
  preview.margin_y = settings_->PreviewLayoutInt("margin_y", 7);
  preview.spacing = settings_->PreviewLayoutInt("spacing", 5);
  preview.candidate_spacing =
      settings_->PreviewLayoutInt("candidate_spacing", 6);
  preview.hilite_spacing = settings_->PreviewLayoutInt("hilite_spacing", 5);
  preview.hilite_padding_x = settings_->PreviewLayoutInt("hilite_padding_x", 8);
  preview.hilite_padding_y = settings_->PreviewLayoutInt("hilite_padding_y", 4);
  preview.font_point = settings_->PreviewStyleInt("font_point", 11);
  preview.label_font_point = settings_->PreviewStyleInt("label_font_point", 9);
  preview.font_face =
      settings_->PreviewStyleString("font_face", L"Microsoft YaHei");
  preview.label_font_face = settings_->PreviewStyleString(
      "label_font_face", preview.font_face.c_str());
  preview.title = settings_navigation::LocalText(
      dark ? L"深色预览" : L"浅色预览", dark ? L"深色預覽" : L"淺色預覽",
      dark ? L"Dark preview" : L"Light preview");
  preview.candidates = {
      static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE)),
      static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE_2)),
      static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE_3)),
      settings_navigation::LocalText(L"泥好", L"泥好", L"Hello")};
  return preview;
}

void UIStyleSettingsDialog::PreparePreview(size_t index) {
  if (!graphics_token_ || index >= preview_bitmaps_.size())
    return;
  HWND control = GetDlgItem(index ? IDC_PREVIEW_DARK : IDC_PREVIEW_LIGHT);
  RECT bounds{};
  if (!control || !::GetClientRect(control, &bounds))
    return;
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  if (width <= 0 || height <= 0)
    return;
  if (!preview_dirty_[index] && preview_bitmaps_[index] &&
      preview_sizes_[index].cx == width && preview_sizes_[index].cy == height)
    return;

  HDC target = ::GetDC(control);
  HDC buffer = target ? ::CreateCompatibleDC(target) : nullptr;
  HBITMAP bitmap =
      buffer ? ::CreateCompatibleBitmap(target, width, height) : nullptr;
  if (buffer && bitmap) {
    const HGDIOBJ previous = ::SelectObject(buffer, bitmap);
    const RECT preview_bounds{0, 0, width, height};
    weasel::DrawAppearancePreview(buffer, preview_bounds, GetFont(),
                                  PreviewStyle(index != 0));
    ::SelectObject(buffer, previous);
    if (preview_bitmaps_[index])
      ::DeleteObject(preview_bitmaps_[index]);
    preview_bitmaps_[index] = bitmap;
    preview_sizes_[index] = {width, height};
    preview_dirty_[index] = false;
    bitmap = nullptr;
  }
  if (bitmap)
    ::DeleteObject(bitmap);
  if (buffer)
    ::DeleteDC(buffer);
  if (target)
    ::ReleaseDC(control, target);
}

void UIStyleSettingsDialog::PreparePreviews() {
  PreparePreview(0);
  PreparePreview(1);
}

LRESULT UIStyleSettingsDialog::OnDrawItem(UINT,
                                          WPARAM,
                                          LPARAM param,
                                          BOOL& handled) {
  const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(param);
  if (draw->CtlID == IDC_APPEARANCE_CARD ||
      draw->CtlID == IDC_APPEARANCE_ACRYLIC_CARD ||
      draw->CtlID == IDC_APPEARANCE_THEME_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_APPEARANCE_THEME_MODE ||
      draw->CtlID == IDC_COLOR_FAMILY || draw->CtlID == IDC_COLOR_LIGHT ||
      draw->CtlID == IDC_COLOR_DARK) {
    DrawCombo(*draw);
    return TRUE;
  }
  if (draw->CtlID != IDC_PREVIEW_LIGHT && draw->CtlID != IDC_PREVIEW_DARK) {
    handled = FALSE;
    return 0;
  }
  const size_t index = draw->CtlID == IDC_PREVIEW_DARK ? 1 : 0;
  PreparePreview(index);
  if (!preview_bitmaps_[index])
    return TRUE;
  HDC buffer = ::CreateCompatibleDC(draw->hDC);
  if (buffer) {
    const HGDIOBJ previous = ::SelectObject(buffer, preview_bitmaps_[index]);
    ::BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top,
             draw->rcItem.right - draw->rcItem.left,
             draw->rcItem.bottom - draw->rcItem.top, buffer, 0, 0, SRCCOPY);
    ::SelectObject(buffer, previous);
    ::DeleteDC(buffer);
  }
  return TRUE;
}
