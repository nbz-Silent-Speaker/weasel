#include "stdafx.h"
#include "WeaselDeployer.h"
#include "UIStyleSettingsDialog.h"
#include "Configurator.h"
#include "AppearancePreview.h"
#include <WeaselUtility.h>
#include <WeaselUserSettings.h>
#include <algorithm>

namespace {
void LayoutAppearancePage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_RESTORE_APPEARANCE,
              settings_navigation::kTopActionLeftDlu,
              settings_navigation::kTopActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_ACRYLIC_ENABLED, 28, 44, 484, 12);
  MoveControl(dialog, IDC_MATERIAL_HINT, 40, 59, 472, 11);
  MoveControl(dialog, IDC_EDIT_GROUP, 28, 76, 84,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_EDIT_SINGLE, 116, 76, 84,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_COLOR_FAMILY, 28, 100, 484, 140);
  MoveControl(dialog, IDC_LIGHT_LABEL, 28, 98, 234, 11);
  MoveControl(dialog, IDC_DARK_LABEL, 278, 98, 234, 11);
  MoveControl(dialog, IDC_COLOR_LIGHT, 28, 111, 234, 140);
  MoveControl(dialog, IDC_COLOR_DARK, 278, 111, 234, 140);
  MoveControl(dialog, IDC_EDITOR_HINT, 28, 128, 484, 11);
  MoveControl(dialog, IDC_SELECTION_HINT, 28, 140, 484, 11);
  MoveControl(dialog, IDC_APPEARANCE_LIGHT_PREVIEW_LABEL, 28, 151, 234, 11);
  MoveControl(dialog, IDC_APPEARANCE_DARK_PREVIEW_LABEL, 278, 151, 234, 11);
  MoveControl(dialog, IDC_PREVIEW_LIGHT, 28, 164, 234, 66);
  MoveControl(dialog, IDC_PREVIEW_DARK, 278, 164, 234, 66);
  MoveControl(dialog, IDC_PREVIEW_HINT, 28, 233, 484, 12);
}
}  // namespace

CString UIStyleSettingsDialog::Text(UINT id) const {
  CString text;
  text.LoadString(id);
  return text;
}

LRESULT UIStyleSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
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
  draft_.Load(settings_->ActiveAppearance(),
              weasel::UserSettings::Load().acrylic);
  for (size_t mode = 0; mode < single_.size(); ++mode) {
    const auto& colors = draft_.colors();
    single_[mode] =
        !std::any_of(settings_->groups().begin(), settings_->groups().end(),
                     [&](const auto& group) {
                       return group.light == colors[mode * 2] &&
                              group.dark == colors[mode * 2 + 1];
                     });
  }
  HWND card = settings_navigation::Create(
      m_hWnd, L"BUTTON", L"", BS_OWNERDRAW, IDC_APPEARANCE_CARD,
      settings_navigation::MapDialogUnits(
          m_hWnd, settings_navigation::kPageInsetDlu,
          settings_navigation::kFirstCardTopDlu, 0, 0)
          .left,
      settings_navigation::MapDialogUnits(
          m_hWnd, 0, settings_navigation::kFirstCardTopDlu, 0, 0)
          .top,
      settings_navigation::MapDialogUnits(
          m_hWnd, 0, 0, settings_navigation::kPageBodyWidthDlu, 0)
          .right,
      settings_navigation::MapDialogUnits(m_hWnd, 0, 0, 0, 214).bottom);
  ::SetWindowPos(card, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  ::SetDlgItemTextW(m_hWnd, IDC_RESTORE_APPEARANCE,
                    settings_navigation::LocalText(L"恢复默认", L"還原預設",
                                                   L"Restore defaults")
                        .c_str());
  settings_navigation::StyleCard(m_hWnd, IDC_APPEARANCE_CARD);
  settings_navigation::StyleActionButton(m_hWnd, IDC_RESTORE_APPEARANCE);
  settings_navigation::StyleToggle(m_hWnd, IDC_EDIT_GROUP);
  settings_navigation::StyleToggle(m_hWnd, IDC_EDIT_SINGLE);
  for (UINT id : {IDC_COLOR_FAMILY, IDC_COLOR_LIGHT, IDC_COLOR_DARK})
    settings_navigation::StyleCombo(m_hWnd, id);
  RECT unit{0, 0, 0, 14};
  MapDialogRect(&unit);
  item_height_ = unit.bottom;
  for (UINT id : {IDC_COLOR_FAMILY, IDC_COLOR_LIGHT, IDC_COLOR_DARK})
    CComboBox(GetDlgItem(id)).SetItemHeight(-1, item_height_);
  RefreshMode();
  ready_ = true;
  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Appearance,
      {IDC_APPLY,
       IDCANCEL,
       WeaselUserDataPath().wstring(),
       {IDOK, IDC_APPEARANCE_TITLE, IDC_SETTINGS_DIVIDER}});
  RefreshPreview();
  CenterWindow();
  return TRUE;
}

std::vector<UIStyleSettingsDialog::PaletteEntry>&
UIStyleSettingsDialog::Entries(UINT id) {
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

void UIStyleSettingsDialog::RefreshMode() {
  CheckDlgButton(IDC_ACRYLIC_ENABLED,
                 draft_.acrylic() ? BST_CHECKED : BST_UNCHECKED);
  SetDlgItemText(IDC_MATERIAL_HINT,
                 Text(draft_.acrylic() ? IDS_APPEARANCE_ACRYLIC_HINT
                                       : IDS_APPEARANCE_NORMAL_HINT));
  SetDlgItemText(IDC_PREVIEW_HINT,
                 Text(draft_.acrylic() ? IDS_APPEARANCE_ACRYLIC_PREVIEW
                                       : IDS_APPEARANCE_NORMAL_PREVIEW));
  FillGroups();
  FillSingles();
  ShowEditor(single_[draft_.acrylic() ? 0 : 1]);
  RefreshPreview();
}

void UIStyleSettingsDialog::ShowEditor(bool single) {
  single_[draft_.acrylic() ? 0 : 1] = single;
  CheckDlgButton(IDC_EDIT_GROUP, single ? BST_UNCHECKED : BST_CHECKED);
  CheckDlgButton(IDC_EDIT_SINGLE, single ? BST_CHECKED : BST_UNCHECKED);
  ::ShowWindow(GetDlgItem(IDC_COLOR_FAMILY), single ? SW_HIDE : SW_SHOW);
  for (int id :
       {IDC_COLOR_LIGHT, IDC_COLOR_DARK, IDC_LIGHT_LABEL, IDC_DARK_LABEL})
    ::ShowWindow(GetDlgItem(id), single ? SW_SHOW : SW_HIDE);
  SetDlgItemText(IDC_EDITOR_HINT, Text(single ? IDS_PALETTE_SINGLE_HINT
                                              : IDS_PALETTE_GROUP_HINT));
  RefreshPreview();
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
  ::EnableWindow(GetDlgItem(IDC_APPLY), draft_.changed());
  settings_navigation::SetStatus(
      m_hWnd,
      draft_.changed()
          ? settings_navigation::LocalText(L"有更改待应用", L"有變更待套用",
                                           L"Changes ready to apply")
          : settings_navigation::LocalText(L"所有设置已应用", L"所有設定已套用",
                                           L"All settings applied"));
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_LIGHT), nullptr, FALSE);
  ::InvalidateRect(GetDlgItem(IDC_PREVIEW_DARK), nullptr, FALSE);
}

LRESULT UIStyleSettingsDialog::OnMaterial(WORD, WORD, HWND, BOOL&) {
  if (ready_) {
    draft_.SetAcrylic(IsDlgButtonChecked(IDC_ACRYLIC_ENABLED) == BST_CHECKED);
    RefreshMode();
  }
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
  draft_.ResetCurrent();
  RefreshMode();
  return 0;
}

LRESULT UIStyleSettingsDialog::OnCancel(WORD, WORD, HWND, BOOL&) {
  if (ConfirmDiscard())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (ConfirmDiscard())
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
  EndDialog(id);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
  if (graphics_token_)
    Gdiplus::GdiplusShutdown(graphics_token_);
  graphics_token_ = 0;
  handled = FALSE;
  return 0;
}

LRESULT UIStyleSettingsDialog::OnSave(WORD, WORD id, HWND, BOOL&) {
  const bool desired = draft_.acrylic();
  const bool before = weasel::UserSettings::Load().acrylic;
  const bool changeMaterial = desired != draft_.saved_acrylic();
  if ((changeMaterial &&
       weasel::UserSettingsStore().WriteBool(weasel::kAcrylicEnabledSetting,
                                             desired) != ERROR_SUCCESS) ||
      !settings_->SaveAppearance(draft_.colors())) {
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
  auto api = reinterpret_cast<RimeLeversApi*>(
      rime_get_api()->find_module("levers")->get_api());
  if (!api->load_settings(settings_->settings()) ||
      !settings_->LoadAppearance()) {
    MSG_BY_IDS(IDS_STR_SCHEME_SAVE_FAILED, IDS_STR_WEASEL,
               MB_OK | MB_ICONERROR);
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
  draft_.Load(applied, weasel::UserSettings::Load().acrylic);
  RefreshMode();
  if (id == IDOK)
    EndDialog(IDOK);
  return 0;
}

LRESULT UIStyleSettingsDialog::OnMeasureItem(UINT,
                                             WPARAM,
                                             LPARAM param,
                                             BOOL& handled) {
  const auto measure = reinterpret_cast<MEASUREITEMSTRUCT*>(param);
  if (measure->CtlID != IDC_COLOR_FAMILY && measure->CtlID != IDC_COLOR_LIGHT &&
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
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    ::FillRect(draw.hDC, &row,
               ::GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    ::SetTextColor(draw.hDC, ::GetSysColor(selected ? COLOR_HIGHLIGHTTEXT
                                                    : COLOR_WINDOWTEXT));
    RECT label = row;
    label.left += field ? 5 : 13;
    label.right -= 5;
    ::DrawTextW(draw.hDC, entry.label, -1, &label,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((draw.itemState & ODS_FOCUS) && !(draw.itemState & ODS_NOFOCUSRECT))
      ::DrawFocusRect(draw.hDC, &row);
  }
  ::RestoreDC(draw.hDC, saved);
}

LRESULT UIStyleSettingsDialog::OnDrawItem(UINT,
                                          WPARAM,
                                          LPARAM param,
                                          BOOL& handled) {
  const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(param);
  if (draw->CtlID == IDC_APPEARANCE_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_COLOR_FAMILY || draw->CtlID == IDC_COLOR_LIGHT ||
      draw->CtlID == IDC_COLOR_DARK) {
    DrawCombo(*draw);
    return TRUE;
  }
  if (draw->CtlID != IDC_PREVIEW_LIGHT && draw->CtlID != IDC_PREVIEW_DARK) {
    handled = FALSE;
    return 0;
  }
  if (!graphics_token_)
    return TRUE;
  weasel::AppearancePreview preview{};
  preview.dark = draw->CtlID == IDC_PREVIEW_DARK;
  preview.acrylic = draft_.acrylic();
  auto scheme = draft_.current(preview.dark);
  if (scheme.empty()) {
    // "Follow configuration" still previews the effective original choice.
    const auto actual = settings_->ActiveAppearance();
    scheme = actual[draft_.offset() + (preview.dark ? 1 : 0)];
    if (scheme.empty() && preview.acrylic)
      scheme = actual[preview.dark ? 3 : 2];
  }
  const auto color = [&](const char* key, COLORREF light, COLORREF dark) {
    return settings_->PreviewColor(scheme, key, preview.dark ? dark : light);
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
  preview.candidates = {static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE)),
                        static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE_2)),
                        static_cast<LPCWSTR>(Text(IDS_APPEARANCE_SAMPLE_3))};
  weasel::DrawAppearancePreview(draw->hDC, draw->rcItem, GetFont(), preview);
  return TRUE;
}
