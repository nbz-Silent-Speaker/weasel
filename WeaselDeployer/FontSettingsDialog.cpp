#include "stdafx.h"
#include "FontSettingsDialog.h"

#include <WeaselUtility.h>

#include <algorithm>
#include <cwctype>
#include <set>

namespace {
bool IsSimplifiedChinese() {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE)
    return false;
  const WORD sublanguage = SUBLANGID(language);
  return sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
         sublanguage == SUBLANG_CHINESE_SINGAPORE;
}

bool IsChinese() {
  return PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
}

std::wstring Lower(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) {
    return static_cast<wchar_t>(std::towlower(value));
  });
  return text;
}

int CALLBACK CollectFont(const LOGFONTW* font,
                         const TEXTMETRICW*,
                         DWORD,
                         LPARAM parameter) {
  auto* names = reinterpret_cast<std::set<std::wstring>*>(parameter);
  if (font->lfFaceName[0] && font->lfFaceName[0] != L'@')
    names->insert(font->lfFaceName);
  return 1;
}

struct SupportQuery {
  bool regular = true;
  bool bold = false;
  bool italic = false;
};

int CALLBACK CollectStyle(const LOGFONTW* font,
                          const TEXTMETRICW*,
                          DWORD,
                          LPARAM parameter) {
  auto* query = reinterpret_cast<SupportQuery*>(parameter);
  if (font->lfItalic)
    query->italic = true;
  if (font->lfWeight >= FW_BOLD)
    query->bold = true;
  if (!font->lfItalic && font->lfWeight < FW_BOLD)
    query->regular = true;
  return 1;
}

HFONT CreatePreviewFont(const weasel::FontChoice& choice, HDC dc) {
  LOGFONTW font = {};
  font.lfHeight = -MulDiv(static_cast<int>(choice.point),
                          GetDeviceCaps(dc, LOGPIXELSY), 72);
  font.lfWeight = choice.shape == weasel::FontShape::Bold ? FW_BOLD : FW_NORMAL;
  font.lfItalic = choice.shape == weasel::FontShape::Italic;
  font.lfQuality = CLEARTYPE_QUALITY;
  wcsncpy_s(font.lfFaceName, choice.family.c_str(), _TRUNCATE);
  return CreateFontIndirectW(&font);
}

int DrawPreviewText(HDC dc,
                    int x,
                    int y,
                    const std::wstring& text,
                    const weasel::FontChoice& choice,
                    COLORREF color) {
  HFONT font = CreatePreviewFont(choice, dc);
  HGDIOBJ previous = SelectObject(dc, font);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
  SIZE size = {};
  GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
  SelectObject(dc, previous);
  DeleteObject(font);
  return size.cx;
}

void LayoutFontPage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_FONT_RESTORE, settings_navigation::kTopActionLeftDlu,
              settings_navigation::kTopActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_EDITOR_CARD, settings_navigation::kPageInsetDlu,
              settings_navigation::kFirstCardTopDlu,
              settings_navigation::kPageBodyWidthDlu, 110);
  MoveControl(dialog, IDC_FONT_ROLE_LABEL, 24, 44, 78, 11);
  MoveControl(dialog, IDC_FONT_ROLE, 24, 57, 78, 74);
  MoveControl(dialog, IDC_FONT_LANGUAGE_LABEL, 108, 44, 78, 11);
  MoveControl(dialog, IDC_FONT_LANGUAGE, 108, 57, 78, 74);
  MoveControl(dialog, IDC_FONT_FAMILY_LABEL, 192, 44, 180, 11);
  MoveControl(dialog, IDC_FONT_SEARCH, 192, 56, 180, 15);
  MoveControl(dialog, IDC_FONT_FAMILY, 192, 74, 180, 57);
  MoveControl(dialog, IDC_FONT_POINT_LABEL, 378, 44, 52, 11);
  MoveControl(dialog, IDC_FONT_POINT, 378, 57, 52, 74);
  MoveControl(dialog, IDC_FONT_SHAPE_LABEL, 436, 44, 76, 11);
  MoveControl(dialog, IDC_FONT_SHAPE_REGULAR, 436, 57, 76,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_SHAPE_BOLD, 436, 80, 76,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_SHAPE_ITALIC, 436, 103, 76,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_CARD, settings_navigation::kPageInsetDlu,
              152, settings_navigation::kPageBodyWidthDlu, 96);
  MoveControl(dialog, IDC_FONT_PREVIEW_HORIZONTAL, 24, 163, 68,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_VERTICAL, 96, 163, 68,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW, 24, 184, 488, 53);
}
}  // namespace

std::wstring FontSettingsDialog::LocalText(const wchar_t* simplified,
                                           const wchar_t* traditional,
                                           const wchar_t* english) const {
  return IsChinese() ? (IsSimplifiedChinese() ? simplified : traditional)
                     : english;
}

LRESULT FontSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  LayoutFontPage(m_hWnd);
  initial_ = weasel::FontSettings::Load();
  draft_ = initial_;

  LOGFONTW heading{};
  ::GetObjectW(GetFont(), sizeof(heading), &heading);
  heading.lfWeight = FW_SEMIBOLD;
  if (heading_font_.CreateFontIndirect(&heading)) {
    for (UINT id :
         {IDC_FONT_ROLE_LABEL, IDC_FONT_LANGUAGE_LABEL, IDC_FONT_FAMILY_LABEL,
          IDC_FONT_POINT_LABEL, IDC_FONT_SHAPE_LABEL})
      CWindow(GetDlgItem(id)).SetFont(heading_font_);
  }

  Localize();
  EnumerateFonts();
  PopulateSelectors();
  for (UINT id : {IDC_FONT_EDITOR_CARD, IDC_FONT_PREVIEW_CARD})
    settings_navigation::PrepareCard(m_hWnd, id);
  settings_navigation::StyleActionButton(m_hWnd, IDC_FONT_RESTORE);
  for (UINT id :
       {IDC_FONT_PREVIEW_HORIZONTAL, IDC_FONT_PREVIEW_VERTICAL,
        IDC_FONT_SHAPE_REGULAR, IDC_FONT_SHAPE_BOLD, IDC_FONT_SHAPE_ITALIC})
    settings_navigation::StyleToggle(m_hWnd, id);
  settings_navigation::RoundDlu(m_hWnd, IDC_FONT_PREVIEW,
                                settings_navigation::kCardRadiusDlu);
  CheckRadioButton(IDC_FONT_PREVIEW_HORIZONTAL, IDC_FONT_PREVIEW_VERTICAL,
                   IDC_FONT_PREVIEW_HORIZONTAL);
  LoadCurrentChoice();
  RefreshApplyState();
  settings_navigation::Install(m_hWnd, settings_navigation::Page::Fonts,
                               {IDC_FONT_APPLY,
                                IDCANCEL,
                                WeaselUserDataPath().wstring(),
                                {IDC_FONT_TITLE, IDC_FONT_EDITOR_TITLE,
                                 IDC_FONT_PREVIEW_TITLE, IDC_FONT_MESSAGE}});
  RefreshApplyState();
  CenterWindow();
  return TRUE;
}

void FontSettingsDialog::Localize() {
  ::SetWindowTextW(
      m_hWnd,
      LocalText(L"小狼毫 - 字体", L"小狼毫 - 字型", L"Weasel - Fonts").c_str());
  const std::pair<UINT, std::wstring> labels[] = {
      {IDC_FONT_TITLE, LocalText(L"字体", L"字型", L"Fonts")},
      {IDC_FONT_RESTORE,
       LocalText(L"恢复默认", L"還原預設", L"Restore defaults")},
      {IDC_FONT_EDITOR_TITLE,
       LocalText(L"字体设置", L"字型設定", L"Font settings")},
      {IDC_FONT_ROLE_LABEL, LocalText(L"文字类型", L"文字類型", L"Text type")},
      {IDC_FONT_LANGUAGE_LABEL,
       LocalText(L"语言类型", L"語言類型", L"Language")},
      {IDC_FONT_FAMILY_LABEL, LocalText(L"字体", L"字型", L"Font")},
      {IDC_FONT_POINT_LABEL, LocalText(L"字号", L"字號", L"Size")},
      {IDC_FONT_SHAPE_LABEL, LocalText(L"字形", L"字形", L"Style")},
      {IDC_FONT_SHAPE_REGULAR, LocalText(L"常规", L"標準", L"Regular")},
      {IDC_FONT_SHAPE_BOLD, LocalText(L"粗体", L"粗體", L"Bold")},
      {IDC_FONT_SHAPE_ITALIC, LocalText(L"斜体", L"斜體", L"Italic")},
      {IDC_FONT_PREVIEW_TITLE, LocalText(L"预览", L"預覽", L"Preview")},
      {IDC_FONT_PREVIEW_HORIZONTAL, LocalText(L"横排", L"橫排", L"Horizontal")},
      {IDC_FONT_PREVIEW_VERTICAL, LocalText(L"竖排", L"直排", L"Vertical")},
      {IDC_FONT_APPLY, LocalText(L"应用", L"套用", L"Apply")},
      {IDCANCEL, LocalText(L"关闭", L"關閉", L"Close")},
  };
  for (const auto& [id, text] : labels)
    ::SetDlgItemTextW(m_hWnd, id, text.c_str());
  const auto search = LocalText(L"搜索字体", L"搜尋字型", L"Search fonts");
  SendDlgItemMessage(IDC_FONT_SEARCH, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(search.c_str()));
}

void FontSettingsDialog::EnumerateFonts() {
  static std::vector<std::wstring> cached_fonts;
  if (!cached_fonts.empty()) {
    fonts_ = cached_fonts;
    return;
  }
  std::set<std::wstring> names;
  HDC dc = ::GetDC(m_hWnd);
  LOGFONTW query = {};
  query.lfCharSet = DEFAULT_CHARSET;
  EnumFontFamiliesExW(dc, &query, CollectFont, reinterpret_cast<LPARAM>(&names),
                      0);
  ::ReleaseDC(m_hWnd, dc);
  fonts_.assign(names.begin(), names.end());
  cached_fonts = fonts_;
}

void FontSettingsDialog::PopulateSelectors() {
  loading_ = true;
  CListBox roles(GetDlgItem(IDC_FONT_ROLE));
  for (const auto& text : {LocalText(L"输入码", L"輸入碼", L"Preedit"),
                           LocalText(L"候选文字", L"候選文字", L"Candidates"),
                           LocalText(L"候选序号", L"候選序號", L"Labels"),
                           LocalText(L"注释文字", L"註釋文字", L"Comments")}) {
    roles.AddString(text.c_str());
  }
  roles.SetCurSel(static_cast<int>(role_));

  CListBox languages(GetDlgItem(IDC_FONT_LANGUAGE));
  languages.AddString(LocalText(L"中文", L"中文", L"Chinese").c_str());
  languages.AddString(
      LocalText(L"英文与数字", L"英文與數字", L"English & numbers").c_str());
  languages.SetCurSel(static_cast<int>(language_));

  CListBox points(GetDlgItem(IDC_FONT_POINT));
  for (int point = 6; point <= 72; ++point)
    points.AddString(std::to_wstring(point).c_str());
  loading_ = false;
  PopulateFontList();
}

void FontSettingsDialog::PopulateFontList() {
  wchar_t filter[128] = {};
  GetDlgItemText(IDC_FONT_SEARCH, filter, _countof(filter));
  const std::wstring needle = Lower(filter);
  const auto& selected = draft_.choices[role_ * 2 + language_].family;
  CListBox list(GetDlgItem(IDC_FONT_FAMILY));
  loading_ = true;
  list.ResetContent();
  int selection = -1;
  for (const auto& family : fonts_) {
    if (!needle.empty() && Lower(family).find(needle) == std::wstring::npos)
      continue;
    const int index = list.AddString(family.c_str());
    if (_wcsicmp(family.c_str(), selected.c_str()) == 0)
      selection = index;
  }
  if (selection < 0 && !selected.empty() && needle.empty()) {
    selection = list.AddString(selected.c_str());
  }
  list.SetCurSel(selection);
  loading_ = false;
}

void FontSettingsDialog::LoadCurrentChoice() {
  loading_ = true;
  const auto& choice = draft_.choices[role_ * 2 + language_];
  PopulateFontList();
  CListBox points(GetDlgItem(IDC_FONT_POINT));
  points.SetCurSel(static_cast<int>((std::max)(6ul, choice.point) - 6));
  CheckRadioButton(IDC_FONT_SHAPE_REGULAR, IDC_FONT_SHAPE_ITALIC,
                   IDC_FONT_SHAPE_REGULAR + static_cast<UINT>(choice.shape));
  loading_ = false;
  RefreshShapeSupport();
}

void FontSettingsDialog::StoreCurrentChoice() {
  if (loading_)
    return;
  auto& choice = draft_.choices[role_ * 2 + language_];
  CListBox fonts(GetDlgItem(IDC_FONT_FAMILY));
  const int font = fonts.GetCurSel();
  if (font >= 0) {
    const int length = fonts.GetTextLen(font);
    std::wstring family(length + 1, L'\0');
    fonts.GetText(font, family.data());
    family.resize(length);
    choice.family = family;
  }
  const int point = CListBox(GetDlgItem(IDC_FONT_POINT)).GetCurSel();
  if (point >= 0)
    choice.point = point + 6;
  draft_.enabled = true;
}

void FontSettingsDialog::RefreshShapeSupport() {
  const auto& family = draft_.choices[role_ * 2 + language_].family;
  LOGFONTW query = {};
  query.lfCharSet = DEFAULT_CHARSET;
  wcsncpy_s(query.lfFaceName, family.c_str(), _TRUNCATE);
  SupportQuery result;
  HDC dc = ::GetDC(m_hWnd);
  EnumFontFamiliesExW(dc, &query, CollectStyle,
                      reinterpret_cast<LPARAM>(&result), 0);
  ::ReleaseDC(m_hWnd, dc);
  CWindow(GetDlgItem(IDC_FONT_SHAPE_REGULAR)).EnableWindow(result.regular);
  CWindow(GetDlgItem(IDC_FONT_SHAPE_BOLD)).EnableWindow(result.bold);
  CWindow(GetDlgItem(IDC_FONT_SHAPE_ITALIC)).EnableWindow(result.italic);
}

void FontSettingsDialog::RefreshApplyState() {
  const bool pending = draft_ != initial_;
  CWindow(GetDlgItem(IDC_FONT_APPLY)).EnableWindow(pending);
  ::SetDlgItemTextW(
      m_hWnd, IDC_FONT_MESSAGE,
      (draft_ != initial_ ? LocalText(L"字体更改等待应用", L"字型變更等待套用",
                                      L"Font changes are ready to apply")
                          : L"")
          .c_str());
  settings_navigation::SetStatus(
      m_hWnd, pending ? LocalText(L"有更改待应用", L"有變更待套用",
                                  L"Changes ready to apply")
                      : LocalText(L"所有设置已应用", L"所有設定已套用",
                                  L"All settings applied"));
}

void FontSettingsDialog::RefreshPreview() {
  ::InvalidateRect(GetDlgItem(IDC_FONT_PREVIEW), nullptr, TRUE);
}

LRESULT FontSettingsDialog::OnSelectionChanged(WORD, WORD, HWND, BOOL&) {
  if (loading_)
    return 0;
  const int role = CListBox(GetDlgItem(IDC_FONT_ROLE)).GetCurSel();
  const int language = CListBox(GetDlgItem(IDC_FONT_LANGUAGE)).GetCurSel();
  if (role >= 0)
    role_ = role;
  if (language >= 0)
    language_ = language;
  LoadCurrentChoice();
  return 0;
}

LRESULT FontSettingsDialog::OnValueChanged(WORD, WORD, HWND, BOOL&) {
  StoreCurrentChoice();
  RefreshShapeSupport();
  RefreshPreview();
  RefreshApplyState();
  return 0;
}

LRESULT FontSettingsDialog::OnSearchChanged(WORD, WORD, HWND, BOOL&) {
  if (!loading_)
    PopulateFontList();
  return 0;
}

LRESULT FontSettingsDialog::OnShapeChanged(WORD, WORD id, HWND, BOOL&) {
  if (!loading_) {
    draft_.choices[role_ * 2 + language_].shape =
        static_cast<weasel::FontShape>(id - IDC_FONT_SHAPE_REGULAR);
    draft_.enabled = true;
    RefreshPreview();
    RefreshApplyState();
  }
  return 0;
}

LRESULT FontSettingsDialog::OnPreviewLayoutChanged(WORD, WORD id, HWND, BOOL&) {
  preview_vertical_ = id == IDC_FONT_PREVIEW_VERTICAL;
  RefreshPreview();
  return 0;
}

void FontSettingsDialog::DrawPreview(const DRAWITEMSTRUCT& draw) {
  const int width = draw.rcItem.right - draw.rcItem.left;
  const int height = draw.rcItem.bottom - draw.rcItem.top;
  if (width <= 0 || height <= 0)
    return;
  HDC buffer = ::CreateCompatibleDC(draw.hDC);
  HBITMAP bitmap =
      buffer ? ::CreateCompatibleBitmap(draw.hDC, width, height) : nullptr;
  const HGDIOBJ previous_bitmap =
      bitmap ? ::SelectObject(buffer, bitmap) : static_cast<HGDIOBJ>(nullptr);
  HDC dc = previous_bitmap ? buffer : draw.hDC;
  RECT bounds = previous_bitmap ? RECT{0, 0, width, height} : draw.rcItem;
  const COLORREF background = GetSysColor(COLOR_WINDOW);
  const COLORREF foreground = GetSysColor(COLOR_WINDOWTEXT);
  const COLORREF secondary = GetSysColor(COLOR_GRAYTEXT);
  const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
  const COLORREF selected = settings_navigation::Mix(accent, background, 36);
  HBRUSH fill = CreateSolidBrush(background);
  FillRect(dc, &bounds, fill);
  DeleteObject(fill);
  HPEN border = CreatePen(
      PS_SOLID, 1,
      settings_navigation::Mix(GetSysColor(COLOR_3DSHADOW), background, 72));
  HGDIOBJ old_pen = SelectObject(dc, border);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  RoundRect(dc, bounds.left, bounds.top, bounds.right - 1, bounds.bottom - 1,
            14, 14);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(border);

  HRGN clip = ::CreateRoundRectRgn(bounds.left, bounds.top, bounds.right,
                                   bounds.bottom, 14, 14);
  ::SelectClipRgn(dc, clip);

  const auto choice = [&](weasel::FontRole role,
                          weasel::FontLanguage language) {
    return draft_.At(role, language);
  };
  const int padding = (std::max)(8, height / 16);
  int x = bounds.left + padding;
  int y = bounds.top + padding / 2;
  DrawPreviewText(
      dc, x, y, L"ni hao",
      choice(weasel::FontRole::Preedit, weasel::FontLanguage::Latin),
      secondary);
  const wchar_t* candidates[] = {L"你好", L"你号", L"hello", L"拟好", L"泥好"};
  const wchar_t* comments[] = {L"", L"常用", L"English", L"", L""};
  if (preview_vertical_) {
    const int row_top = bounds.top + (std::max)(22, height / 5);
    const int measured_row_height =
        static_cast<int>((bounds.bottom - padding - row_top) / 5);
    const int row_height = measured_row_height > 1 ? measured_row_height : 1;
    for (int index = 0; index < 5; ++index) {
      y = row_top + index * row_height;
      if (index == 0) {
        RECT highlight{bounds.left + padding / 2, y, bounds.right - padding / 2,
                       y + row_height};
        HBRUSH highlight_brush = ::CreateSolidBrush(selected);
        HPEN highlight_pen = ::CreatePen(PS_NULL, 0, selected);
        const HGDIOBJ previous_brush = ::SelectObject(dc, highlight_brush);
        const HGDIOBJ previous_pen = ::SelectObject(dc, highlight_pen);
        ::RoundRect(dc, highlight.left, highlight.top, highlight.right,
                    highlight.bottom, 10, 10);
        ::SelectObject(dc, previous_pen);
        ::SelectObject(dc, previous_brush);
        ::DeleteObject(highlight_pen);
        ::DeleteObject(highlight_brush);
      }
      const auto language = index == 2 ? weasel::FontLanguage::Latin
                                       : weasel::FontLanguage::Chinese;
      const int label_height =
          MulDiv(static_cast<int>(choice(weasel::FontRole::Label,
                                         weasel::FontLanguage::Latin)
                                      .point),
                 GetDeviceCaps(dc, LOGPIXELSY), 72);
      const int candidate_height = MulDiv(
          static_cast<int>(choice(weasel::FontRole::Candidate, language).point),
          GetDeviceCaps(dc, LOGPIXELSY), 72);
      x = bounds.left + padding;
      x += DrawPreviewText(
               dc, x, y + (std::max)(0, (row_height - label_height) / 2),
               std::to_wstring(index + 1) + L".",
               choice(weasel::FontRole::Label, weasel::FontLanguage::Latin),
               index == 0 ? accent : secondary) +
           padding / 2;
      x += DrawPreviewText(
               dc, x, y + (std::max)(0, (row_height - candidate_height) / 2),
               candidates[index], choice(weasel::FontRole::Candidate, language),
               foreground) +
           padding;
      if (*comments[index]) {
        const auto comment_language = index == 2
                                          ? weasel::FontLanguage::Latin
                                          : weasel::FontLanguage::Chinese;
        DrawPreviewText(dc, x, y, comments[index],
                        choice(weasel::FontRole::Comment, comment_language),
                        secondary);
      }
    }
  } else {
    const int row_top = bounds.top + height / 2;
    const int row_height = bounds.bottom - padding - row_top;
    for (int index = 0; index < 5; ++index) {
      if (index == 0) {
        RECT highlight{x - padding / 2, row_top, x + width / 6,
                       row_top + row_height};
        HBRUSH highlight_brush = ::CreateSolidBrush(selected);
        HPEN highlight_pen = ::CreatePen(PS_NULL, 0, selected);
        const HGDIOBJ previous_brush = ::SelectObject(dc, highlight_brush);
        const HGDIOBJ previous_pen = ::SelectObject(dc, highlight_pen);
        ::RoundRect(dc, highlight.left, highlight.top, highlight.right,
                    highlight.bottom, 10, 10);
        ::SelectObject(dc, previous_pen);
        ::SelectObject(dc, previous_brush);
        ::DeleteObject(highlight_pen);
        ::DeleteObject(highlight_brush);
      }
      x += DrawPreviewText(
               dc, x, row_top, std::to_wstring(index + 1) + L".",
               choice(weasel::FontRole::Label, weasel::FontLanguage::Latin),
               index == 0 ? accent : secondary) +
           5;
      const auto language = index == 2 ? weasel::FontLanguage::Latin
                                       : weasel::FontLanguage::Chinese;
      x += DrawPreviewText(dc, x, row_top, candidates[index],
                           choice(weasel::FontRole::Candidate, language),
                           foreground) +
           18;
    }
  }
  ::SelectClipRgn(dc, nullptr);
  ::DeleteObject(clip);
  if (previous_bitmap) {
    ::BitBlt(draw.hDC, draw.rcItem.left, draw.rcItem.top, width, height, buffer,
             0, 0, SRCCOPY);
    ::SelectObject(buffer, previous_bitmap);
  }
  if (bitmap)
    ::DeleteObject(bitmap);
  if (buffer)
    ::DeleteDC(buffer);
}

LRESULT FontSettingsDialog::OnDrawItem(UINT,
                                       WPARAM,
                                       LPARAM parameter,
                                       BOOL& handled) {
  const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
  if (draw->CtlID == IDC_FONT_EDITOR_CARD ||
      draw->CtlID == IDC_FONT_PREVIEW_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID != IDC_FONT_PREVIEW) {
    handled = FALSE;
    return 0;
  }
  DrawPreview(*draw);
  return TRUE;
}

LRESULT FontSettingsDialog::OnStaticColor(UINT,
                                          WPARAM dc,
                                          LPARAM window,
                                          BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if (id < IDC_FONT_ROLE_LABEL || id > IDC_FONT_PREVIEW) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkMode(context, TRANSPARENT);
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

LRESULT FontSettingsDialog::OnButtonColor(UINT,
                                          WPARAM dc,
                                          LPARAM window,
                                          BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id < IDC_FONT_SHAPE_REGULAR || id > IDC_FONT_SHAPE_ITALIC) &&
      (id < IDC_FONT_PREVIEW_HORIZONTAL || id > IDC_FONT_PREVIEW_VERTICAL)) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkColor(context, ::GetSysColor(COLOR_WINDOW));
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

LRESULT FontSettingsDialog::OnRestore(WORD, WORD, HWND, BOOL&) {
  draft_ = weasel::FontSettings::Defaults();
  draft_.enabled = false;
  LoadCurrentChoice();
  RefreshPreview();
  RefreshApplyState();
  return 0;
}

LRESULT FontSettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  if (draft_.Save() != ERROR_SUCCESS) {
    ::MessageBoxW(m_hWnd,
                  LocalText(L"无法保存字体设置。", L"無法儲存字型設定。",
                            L"Could not save font settings.")
                      .c_str(),
                  LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
                  MB_OK | MB_ICONERROR);
    return 0;
  }
  initial_ = draft_;
  weasel::NotifyUserSettingsChanged();
  RefreshApplyState();
  return 0;
}

bool FontSettingsDialog::ConfirmDiscard() {
  if (draft_ == initial_)
    return true;
  return ::MessageBoxW(
             m_hWnd,
             LocalText(L"字体设置尚未应用。是否放弃这些更改？",
                       L"字型設定尚未套用。是否放棄這些變更？",
                       L"Font changes have not been applied. Discard them?")
                 .c_str(),
             LocalText(L"未应用的设置", L"未套用的設定", L"Unapplied settings")
                 .c_str(),
             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

LRESULT FontSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (ConfirmDiscard())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT FontSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (ConfirmDiscard())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT FontSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::Fonts)
    return 0;
  if (!ConfirmDiscard())
    return 0;
  EndDialog(id);
  return 0;
}
