#include "stdafx.h"
#include "FontSettingsDialog.h"
#include "UIStyleSettings.h"

#include <WeaselUtility.h>

#include <algorithm>
#include <set>

namespace {
constexpr UINT_PTR kRolePreviewPulseTimer = 0x4650;
constexpr ULONGLONG kRolePreviewPulseDurationMs = 900;

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

LRESULT CALLBACK RoleListPulseProc(HWND window,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam,
                                   UINT_PTR,
                                   DWORD_PTR) {
  if (message == WM_LBUTTONDOWN) {
    const DWORD hit =
        static_cast<DWORD>(::SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam));
    const int clicked = static_cast<int>(LOWORD(hit));
    const bool outside = HIWORD(hit) != 0;
    const int selected =
        static_cast<int>(::SendMessageW(window, LB_GETCURSEL, 0, 0));
    if (!outside && clicked == selected) {
      ::PostMessageW(::GetParent(window), WM_COMMAND,
                     MAKEWPARAM(IDC_FONT_ROLE_LIST, LBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(window));
    }
  } else if (message == WM_NCDESTROY) {
    ::RemoveWindowSubclass(window, RoleListPulseProc, 17);
  }
  return ::DefSubclassProc(window, message, wparam, lparam);
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

SIZE MeasurePreviewText(HDC dc,
                        const std::wstring& text,
                        const weasel::FontChoice& choice) {
  SIZE size{};
  HFONT font = CreatePreviewFont(choice, dc);
  const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
  GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
  if (previous)
    SelectObject(dc, previous);
  if (font)
    DeleteObject(font);
  return size;
}

bool IsChinesePreviewCodePoint(UINT32 codepoint) {
  return (codepoint >= 0x2e80 && codepoint <= 0x303f) ||
         (codepoint >= 0x31c0 && codepoint <= 0x31ef) ||
         (codepoint >= 0x3400 && codepoint <= 0x4dbf) ||
         (codepoint >= 0x4e00 && codepoint <= 0x9fff) ||
         (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
         (codepoint >= 0x20000 && codepoint <= 0x2fa1f) ||
         (codepoint >= 0xff01 && codepoint <= 0xff60);
}

weasel::FontLanguage PreviewLanguageAt(const std::wstring& text, size_t index) {
  UINT32 codepoint = text[index];
  if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < text.size()) {
    const UINT32 low = text[index + 1];
    if (low >= 0xdc00 && low <= 0xdfff)
      codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
  }
  return IsChinesePreviewCodePoint(codepoint) ? weasel::FontLanguage::Chinese
                                              : weasel::FontLanguage::Latin;
}

TEXTMETRICW PreviewFontMetrics(HDC dc, const weasel::FontChoice& choice) {
  TEXTMETRICW metrics{};
  HFONT font = CreatePreviewFont(choice, dc);
  const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
  ::GetTextMetricsW(dc, &metrics);
  if (previous)
    ::SelectObject(dc, previous);
  if (font)
    ::DeleteObject(font);
  return metrics;
}

SIZE MeasureMixedPreviewText(HDC dc,
                             const std::wstring& text,
                             const weasel::FontChoice& chinese,
                             const weasel::FontChoice& latin) {
  SIZE result{};
  const TEXTMETRICW chinese_metrics = PreviewFontMetrics(dc, chinese);
  const TEXTMETRICW latin_metrics = PreviewFontMetrics(dc, latin);
  result.cy = (std::max)(chinese_metrics.tmAscent, latin_metrics.tmAscent) +
              (std::max)(chinese_metrics.tmDescent, latin_metrics.tmDescent);
  size_t start = 0;
  while (start < text.size()) {
    const auto language = PreviewLanguageAt(text, start);
    size_t end = start + 1;
    while (end < text.size() && PreviewLanguageAt(text, end) == language)
      ++end;
    const auto& choice =
        language == weasel::FontLanguage::Chinese ? chinese : latin;
    result.cx +=
        MeasurePreviewText(dc, text.substr(start, end - start), choice).cx;
    start = end;
  }
  return result;
}

int DrawMixedPreviewText(HDC dc,
                         int x,
                         int y,
                         const std::wstring& text,
                         const weasel::FontChoice& chinese,
                         const weasel::FontChoice& latin,
                         COLORREF color) {
  const TEXTMETRICW chinese_metrics = PreviewFontMetrics(dc, chinese);
  const TEXTMETRICW latin_metrics = PreviewFontMetrics(dc, latin);
  const LONG ascent =
      (std::max)(chinese_metrics.tmAscent, latin_metrics.tmAscent);
  const int origin = x;
  size_t start = 0;
  while (start < text.size()) {
    const auto language = PreviewLanguageAt(text, start);
    size_t end = start + 1;
    while (end < text.size() && PreviewLanguageAt(text, end) == language)
      ++end;
    const bool is_chinese = language == weasel::FontLanguage::Chinese;
    const auto& choice = is_chinese ? chinese : latin;
    const auto& metrics = is_chinese ? chinese_metrics : latin_metrics;
    x += DrawPreviewText(dc, x, y + ascent - metrics.tmAscent,
                         text.substr(start, end - start), choice, color);
    start = end;
  }
  return x - origin;
}

void DrawPreviewRoleCue(HDC dc, RECT bounds, BYTE alpha) {
  if (!alpha || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    return;
  const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
  const int inset_x = (std::max)(2, ::MulDiv(3, dpi, 96));
  const int inset_y = (std::max)(1, ::MulDiv(2, dpi, 96));
  ::InflateRect(&bounds, inset_x, inset_y);
  const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
  Gdiplus::Graphics canvas(dc);
  canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const Gdiplus::RectF shape(
      static_cast<Gdiplus::REAL>(bounds.left) + 0.5f,
      static_cast<Gdiplus::REAL>(bounds.top) + 0.5f,
      static_cast<Gdiplus::REAL>(bounds.right - bounds.left - 1),
      static_cast<Gdiplus::REAL>(bounds.bottom - bounds.top - 1));
  const Gdiplus::REAL radius =
      static_cast<Gdiplus::REAL>((std::max)(3, ::MulDiv(4, dpi, 96)));
  Gdiplus::GraphicsPath path;
  settings_navigation::AddControlPath(path, shape, radius, true, true);
  Gdiplus::SolidBrush fill(Gdiplus::Color(
      alpha, GetRValue(accent), GetGValue(accent), GetBValue(accent)));
  Gdiplus::Pen border(
      Gdiplus::Color((std::min)(255, static_cast<int>(alpha) + 48),
                     GetRValue(accent), GetGValue(accent), GetBValue(accent)),
      1.0f);
  canvas.FillPath(&fill, &path);
  canvas.DrawPath(&border, &path);
}

void CreateFontPageControls(HWND dialog) {
  using settings_navigation::Create;
  Create(dialog, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
         IDC_FONT_LANGUAGE_CHINESE, 0, 0, 1, 1);
  Create(dialog, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
         IDC_FONT_LANGUAGE_LATIN, 0, 0, 1, 1);
  const DWORD combo_style = CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE |
                            CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP;
  Create(dialog, L"COMBOBOX", L"", combo_style, IDC_FONT_FAMILY_CHINESE, 0, 0,
         1, 1);
  Create(dialog, L"COMBOBOX", L"", combo_style, IDC_FONT_FAMILY_LATIN, 0, 0, 1,
         1);
  Create(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
         IDC_FONT_STYLE_FRAME, 0, 0, 1, 1);
  Create(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
         IDC_FONT_SIZE_FRAME, 0, 0, 1, 1);
  Create(dialog, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS,
         IDC_FONT_ROLE_FRAME, 0, 0, 1, 1);
  const DWORD list_style = LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                           LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP |
                           WS_CLIPSIBLINGS;
  Create(dialog, L"LISTBOX", L"", list_style, IDC_FONT_STYLE_LIST, 0, 0, 1, 1);
  Create(dialog, L"LISTBOX", L"", list_style, IDC_FONT_SIZE_LIST, 0, 0, 1, 1);
  Create(dialog, L"LISTBOX", L"", list_style & ~WS_VSCROLL, IDC_FONT_ROLE_LIST,
         0, 0, 1, 1);
  Create(dialog, L"BUTTON", L"", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
         IDC_FONT_PREVIEW_LIGHT, 0, 0, 1, 1);
  Create(dialog, L"BUTTON", L"", BS_AUTORADIOBUTTON | WS_TABSTOP,
         IDC_FONT_PREVIEW_DARK, 0, 0, 1, 1);
  Create(dialog, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
         IDC_FONT_PREVIEW_HINT, 0, 0, 1, 1);
}

void LayoutFontPage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_FONT_RESTORE,
              settings_navigation::kBottomActionLeftDlu,
              settings_navigation::kBottomActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_FONT_EDITOR_CARD, settings_navigation::kPageInsetDlu,
              settings_navigation::kFirstCardTopDlu,
              settings_navigation::kPageBodyWidthDlu, 82);
  MoveControl(dialog, IDC_FONT_PREVIEW_CARD, settings_navigation::kPageInsetDlu,
              104, settings_navigation::kPageBodyWidthDlu,
              settings_navigation::kPageCardsBottomDlu - 104);
  constexpr int kPreviewToggleLeftDlu = 24;
  constexpr int kPreviewToggleWidthDlu = 46;
  constexpr int kPreviewToggleGroupGapDlu = 8;
  MoveControl(dialog, IDC_FONT_PREVIEW_LIGHT, kPreviewToggleLeftDlu, 114,
              kPreviewToggleWidthDlu,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_DARK,
              kPreviewToggleLeftDlu + kPreviewToggleWidthDlu, 114,
              kPreviewToggleWidthDlu,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_HORIZONTAL,
              kPreviewToggleLeftDlu + kPreviewToggleWidthDlu * 2 +
                  kPreviewToggleGroupGapDlu,
              114, kPreviewToggleWidthDlu,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_VERTICAL,
              kPreviewToggleLeftDlu + kPreviewToggleWidthDlu * 3 +
                  kPreviewToggleGroupGapDlu,
              114, kPreviewToggleWidthDlu,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW_HINT, 226, 114, 286,
              settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_FONT_PREVIEW, 24, 133, 488, 105);

  constexpr int kHeaderTopDlu = 24;
  constexpr int kListTopDlu = 37;
  constexpr int kSecondHeaderTopDlu = 59;
  constexpr int kSecondComboTopDlu = 72;
  constexpr int kListHeightDlu = 53;
  MoveControl(dialog, IDC_FONT_ROLE_LABEL, 24, kHeaderTopDlu, 80, 10);
  MoveControl(dialog, IDC_FONT_ROLE_FRAME, 24, kListTopDlu, 80, kListHeightDlu);
  MoveControl(dialog, IDC_FONT_ROLE_LIST, 26, kListTopDlu + 2, 76,
              kListHeightDlu - 4);
  MoveControl(dialog, IDC_FONT_LANGUAGE_CHINESE, 114, kHeaderTopDlu, 210, 10);
  MoveControl(dialog, IDC_FONT_FAMILY_CHINESE, 114, kListTopDlu, 210, 90);
  MoveControl(dialog, IDC_FONT_LANGUAGE_LATIN, 114, kSecondHeaderTopDlu, 210,
              10);
  MoveControl(dialog, IDC_FONT_FAMILY_LATIN, 114, kSecondComboTopDlu, 210, 90);
  MoveControl(dialog, IDC_FONT_SHAPE_LABEL, 334, kHeaderTopDlu, 100, 10);
  MoveControl(dialog, IDC_FONT_STYLE_FRAME, 334, kListTopDlu, 100,
              kListHeightDlu);
  MoveControl(dialog, IDC_FONT_STYLE_LIST, 336, kListTopDlu + 14, 96,
              kListHeightDlu - 16);
  MoveControl(dialog, IDC_FONT_POINT_LABEL, 444, kHeaderTopDlu, 68, 10);
  MoveControl(dialog, IDC_FONT_SIZE_FRAME, 444, kListTopDlu, 68,
              kListHeightDlu);
  MoveControl(dialog, IDC_FONT_SIZE_LIST, 446, kListTopDlu + 14, 64,
              kListHeightDlu - 16);

  HWND western_font = ::GetDlgItem(dialog, IDC_FONT_FAMILY_LATIN);
  RECT western_bounds{};
  if (western_font && ::GetWindowRect(western_font, &western_bounds)) {
    ::MapWindowPoints(HWND_DESKTOP, dialog,
                      reinterpret_cast<POINT*>(&western_bounds), 2);
    const int target_bottom = western_bounds.bottom;
    const int bottom_inset =
        settings_navigation::MapDialogUnits(dialog, 0, 0, 0, 2).bottom;
    for (UINT id :
         {IDC_FONT_ROLE_FRAME, IDC_FONT_STYLE_FRAME, IDC_FONT_SIZE_FRAME}) {
      HWND frame = ::GetDlgItem(dialog, id);
      RECT bounds{};
      if (frame && ::GetWindowRect(frame, &bounds)) {
        ::MapWindowPoints(HWND_DESKTOP, dialog,
                          reinterpret_cast<POINT*>(&bounds), 2);
        ::SetWindowPos(
            frame, nullptr, 0, 0, bounds.right - bounds.left,
            (std::max)(1, static_cast<int>(target_bottom - bounds.top)),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
    for (UINT id :
         {IDC_FONT_ROLE_LIST, IDC_FONT_STYLE_LIST, IDC_FONT_SIZE_LIST}) {
      HWND list = ::GetDlgItem(dialog, id);
      RECT bounds{};
      if (list && ::GetWindowRect(list, &bounds)) {
        ::MapWindowPoints(HWND_DESKTOP, dialog,
                          reinterpret_cast<POINT*>(&bounds), 2);
        ::SetWindowPos(
            list, nullptr, 0, 0, bounds.right - bounds.left,
            (std::max)(
                1, static_cast<int>(target_bottom - bottom_inset - bounds.top)),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
  }

  HWND size_list = ::GetDlgItem(dialog, IDC_FONT_SIZE_LIST);
  RECT list_bounds{};
  if (size_list && ::GetClientRect(size_list, &list_bounds)) {
    HDC dc = ::GetDC(size_list);
    const HFONT font =
        reinterpret_cast<HFONT>(::SendMessageW(size_list, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    ::GetTextMetricsW(dc, &metrics);
    if (previous)
      ::SelectObject(dc, previous);
    ::ReleaseDC(size_list, dc);
    const int row_height = (std::max)(
        1, static_cast<int>(metrics.tmHeight) +
               settings_navigation::ScaledLogicalPixels(size_list, 4));
    for (const auto& [id, maximum_rows] :
         {std::pair<UINT, int>{IDC_FONT_ROLE_LIST, 4},
          std::pair<UINT, int>{IDC_FONT_STYLE_LIST, 3},
          std::pair<UINT, int>{IDC_FONT_SIZE_LIST, INT_MAX}}) {
      HWND list = ::GetDlgItem(dialog, id);
      RECT bounds{};
      if (!list || !::GetClientRect(list, &bounds))
        continue;
      const int available_height = bounds.bottom - bounds.top;
      const int visible_rows = (std::max)(
          1, (std::min)(maximum_rows, available_height / row_height));
      ::SendMessageW(list, LB_SETITEMHEIGHT, 0, row_height);
      RECT window_bounds{};
      ::GetWindowRect(list, &window_bounds);
      ::SetWindowPos(list, nullptr, 0, 0,
                     window_bounds.right - window_bounds.left,
                     visible_rows * row_height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
}

void DrawFontListFrame(const DRAWITEMSTRUCT& draw) {
  RECT bounds = draw.rcItem;
  ::FillRect(draw.hDC, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
  bounds.right -= 1;
  bounds.bottom -= 1;
  const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF border = settings_navigation::Mix(
      settings_theme::GetColor(COLOR_3DSHADOW), surface, 76);
  HBRUSH brush = ::CreateSolidBrush(surface);
  HPEN pen = ::CreatePen(PS_SOLID, 1, border);
  const HGDIOBJ previous_brush = ::SelectObject(draw.hDC, brush);
  const HGDIOBJ previous_pen = ::SelectObject(draw.hDC, pen);
  const int diameter =
      settings_navigation::ControlCornerDiameter(draw.hwndItem);
  ::RoundRect(draw.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
              diameter, diameter);
  const int current_height = settings_navigation::MapDialogUnits(
                                 ::GetParent(draw.hwndItem), 0, 0, 0, 13)
                                 .bottom;
  const int separator_y = draw.rcItem.top + current_height;
  ::MoveToEx(draw.hDC, draw.rcItem.left + 1, separator_y, nullptr);
  ::LineTo(draw.hDC, draw.rcItem.right - 1, separator_y);
  ::SelectObject(draw.hDC, previous_pen);
  ::SelectObject(draw.hDC, previous_brush);
  ::DeleteObject(pen);
  ::DeleteObject(brush);

  wchar_t value[128] = {};
  ::GetWindowTextW(draw.hwndItem, value, _countof(value));
  RECT text_bounds = draw.rcItem;
  text_bounds.left +=
      settings_navigation::ScaledLogicalPixels(draw.hwndItem, 4);
  text_bounds.right -=
      settings_navigation::ScaledLogicalPixels(draw.hwndItem, 4);
  text_bounds.bottom = separator_y;
  ::SetBkMode(draw.hDC, TRANSPARENT);
  ::SetTextColor(draw.hDC, settings_theme::GetColor(COLOR_WINDOWTEXT));
  ::DrawTextW(
      draw.hDC, value, -1, &text_bounds,
      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void DrawFontListItem(const DRAWITEMSTRUCT& draw) {
  RECT bounds = draw.rcItem;
  const bool selected = (draw.itemState & ODS_SELECTED) != 0;
  const COLORREF background = selected
                                  ? settings_theme::GetColor(COLOR_HIGHLIGHT)
                                  : settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF text = selected ? settings_theme::GetColor(COLOR_HIGHLIGHTTEXT)
                                 : settings_theme::GetColor(COLOR_WINDOWTEXT);
  HBRUSH brush = ::CreateSolidBrush(background);
  ::FillRect(draw.hDC, &bounds, brush);
  ::DeleteObject(brush);
  if (draw.itemID != static_cast<UINT>(-1)) {
    const int length = static_cast<int>(
        ::SendMessageW(draw.hwndItem, LB_GETTEXTLEN, draw.itemID, 0));
    if (length >= 0) {
      std::wstring value(static_cast<size_t>(length) + 1, L'\0');
      ::SendMessageW(draw.hwndItem, LB_GETTEXT, draw.itemID,
                     reinterpret_cast<LPARAM>(value.data()));
      value.resize(static_cast<size_t>(length));
      ::SetBkMode(draw.hDC, TRANSPARENT);
      ::SetTextColor(draw.hDC, text);
      bounds.left += settings_navigation::ScaledLogicalPixels(draw.hwndItem, 4);
      ::DrawTextW(
          draw.hDC, value.c_str(), length, &bounds,
          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
  }
  if ((draw.itemState & ODS_FOCUS) != 0)
    ::DrawFocusRect(draw.hDC, &draw.rcItem);
}
}  // namespace

std::wstring FontSettingsDialog::LocalText(const wchar_t* simplified,
                                           const wchar_t* traditional,
                                           const wchar_t* english) const {
  return IsChinese() ? (IsSimplifiedChinese() ? simplified : traditional)
                     : english;
}

LRESULT FontSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  CreateFontPageControls(m_hWnd);
  LayoutFontPage(m_hWnd);
  if (!appearance_settings_ || !appearance_settings_->LoadAppearance()) {
    LOG(ERROR) << "Font preview could not load the effective appearance "
                  "configuration.";
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法读取当前外观配置。", L"無法讀取目前外觀設定。",
                  L"The current appearance configuration could not be loaded.")
            .c_str(),
        LocalText(L"小狼毫", L"小狼毫", L"Weasel").c_str(),
        MB_OK | MB_ICONERROR);
    EndDialog(IDCANCEL);
    return settings_navigation::HostedPageInitResult();
  }
  const auto user_settings = weasel::UserSettings::Load();
  preview_dark_ = weasel::ResolveAppearanceDarkMode(
      user_settings.appearance_theme_mode, IsUserDarkMode() != FALSE);
  initial_ = weasel::FontSettings::Load();
  if (!initial_.enabled)
    initial_ = AppearanceDefaults();
  draft_ = initial_;

  Localize();
  for (UINT id :
       {IDC_FONT_ROLE_LABEL, IDC_FONT_SHAPE_LABEL, IDC_FONT_POINT_LABEL}) {
    HWND label = GetDlgItem(id);
    ::SetWindowLongPtrW(label, GWL_STYLE,
                        ::GetWindowLongPtrW(label, GWL_STYLE) & ~SS_CENTER);
  }
  EnumerateFonts();
  PopulateSelectors();
  for (UINT id : {IDC_FONT_EDITOR_CARD, IDC_FONT_PREVIEW_CARD}) {
    settings_navigation::PrepareCard(m_hWnd, id);
    HWND card = ::GetDlgItem(m_hWnd, id);
    ::SetWindowLongPtrW(card, GWL_STYLE,
                        ::GetWindowLongPtrW(card, GWL_STYLE) | WS_CLIPSIBLINGS);
  }
  settings_navigation::StyleActionButton(m_hWnd, IDC_FONT_RESTORE);
  for (UINT id : {IDC_FONT_FAMILY_CHINESE, IDC_FONT_FAMILY_LATIN})
    settings_navigation::StyleCombo(m_hWnd, id);
  for (UINT id : {IDC_FONT_PREVIEW_HORIZONTAL})
    settings_navigation::StyleSegmentedToggle(
        m_hWnd, id, settings_navigation::ToggleState::Segment::Left);
  for (UINT id : {IDC_FONT_PREVIEW_VERTICAL})
    settings_navigation::StyleSegmentedToggle(
        m_hWnd, id, settings_navigation::ToggleState::Segment::Right);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_FONT_PREVIEW_LIGHT,
      settings_navigation::ToggleState::Segment::Left);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_FONT_PREVIEW_DARK,
      settings_navigation::ToggleState::Segment::Right);
  settings_navigation::RoundDlu(m_hWnd, IDC_FONT_PREVIEW,
                                settings_navigation::kCardRadiusDlu);
  ::SetWindowSubclass(GetDlgItem(IDC_FONT_ROLE_LIST), RoleListPulseProc, 17, 0);
  CheckRadioButton(IDC_FONT_PREVIEW_HORIZONTAL, IDC_FONT_PREVIEW_VERTICAL,
                   IDC_FONT_PREVIEW_HORIZONTAL);
  CheckRadioButton(
      IDC_FONT_PREVIEW_LIGHT, IDC_FONT_PREVIEW_DARK,
      preview_dark_ ? IDC_FONT_PREVIEW_DARK : IDC_FONT_PREVIEW_LIGHT);
  LoadRoleChoices();
  RefreshApplyState();
  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Fonts,
      {IDC_FONT_APPLY,
       IDCANCEL,
       WeaselDisplayUserDataPath().wstring(),
       {IDC_FONT_TITLE, IDC_FONT_EDITOR_TITLE, IDC_FONT_ROLE,
        IDC_FONT_LANGUAGE_LABEL, IDC_FONT_LANGUAGE, IDC_FONT_FAMILY_LABEL,
        IDC_FONT_SEARCH, IDC_FONT_FAMILY, IDC_FONT_POINT,
        IDC_FONT_SHAPE_REGULAR, IDC_FONT_SHAPE_BOLD, IDC_FONT_SHAPE_ITALIC,
        IDC_FONT_PREVIEW_TITLE, IDC_FONT_MESSAGE}});
  // The cards are background siblings, not parents of the controls they
  // visually contain.  Keep them behind every list, button and preview after
  // Install has created and positioned the full page, otherwise a later card
  // repaint covers those controls until the mouse invalidates them again.
  for (UINT id : {IDC_FONT_EDITOR_CARD, IDC_FONT_PREVIEW_CARD}) {
    ::SetWindowPos(::GetDlgItem(m_hWnd, id), HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  for (const auto& [frame_id, list_id] :
       {std::pair<UINT, UINT>{IDC_FONT_ROLE_FRAME, IDC_FONT_ROLE_LIST},
        std::pair<UINT, UINT>{IDC_FONT_STYLE_FRAME, IDC_FONT_STYLE_LIST},
        std::pair<UINT, UINT>{IDC_FONT_SIZE_FRAME, IDC_FONT_SIZE_LIST}}) {
    HWND frame = ::GetDlgItem(m_hWnd, frame_id);
    HWND list = ::GetDlgItem(m_hWnd, list_id);
    if (frame && list)
      ::SetWindowPos(frame, list, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  RefreshApplyState();
  CenterWindow();
  return settings_navigation::HostedPageInitResult();
}

void FontSettingsDialog::Localize() {
  ::SetWindowTextW(
      m_hWnd,
      LocalText(L"小狼毫 - 字体", L"小狼毫 - 字型", L"Weasel - Fonts").c_str());
  const std::pair<UINT, std::wstring> labels[] = {
      {IDC_FONT_TITLE, LocalText(L"字体", L"字型", L"Fonts")},
      {IDC_FONT_RESTORE,
       LocalText(L"恢复本页默认", L"還原本頁預設", L"Restore this page")},
      {IDC_FONT_EDITOR_TITLE,
       LocalText(L"字体设置", L"字型設定", L"Font settings")},
      {IDC_FONT_ROLE_LABEL, LocalText(L"文字类型", L"文字類型", L"Text type")},
      {IDC_FONT_LANGUAGE_LABEL, LocalText(L"字号", L"字號", L"Size")},
      {IDC_FONT_FAMILY_LABEL, LocalText(L"字形", L"字形", L"Style")},
      {IDC_FONT_POINT_LABEL, LocalText(L"字号", L"字號", L"Size")},
      {IDC_FONT_SHAPE_LABEL, LocalText(L"字形", L"字形", L"Style")},
      {IDC_FONT_SHAPE_REGULAR, LocalText(L"常规", L"標準", L"Regular")},
      {IDC_FONT_SHAPE_BOLD, LocalText(L"粗体", L"粗體", L"Bold")},
      {IDC_FONT_SHAPE_ITALIC, LocalText(L"斜体", L"斜體", L"Italic")},
      {IDC_FONT_PREVIEW_TITLE, LocalText(L"预览", L"預覽", L"Preview")},
      {IDC_FONT_PREVIEW_LIGHT, LocalText(L"浅色", L"淺色", L"Light")},
      {IDC_FONT_PREVIEW_DARK, LocalText(L"深色", L"深色", L"Dark")},
      {IDC_FONT_PREVIEW_HINT,
       LocalText(L"仅影响预览，不会保存或应用到实际候选窗",
                 L"僅影響預覽，不會儲存或套用到實際候選窗",
                 L"Preview only; changes are not saved or applied")},
      {IDC_FONT_PREVIEW_HORIZONTAL, LocalText(L"横排", L"橫排", L"Horizontal")},
      {IDC_FONT_PREVIEW_VERTICAL, LocalText(L"竖排", L"直排", L"Vertical")},
      {IDC_FONT_LANGUAGE_CHINESE,
       LocalText(L"中文字体", L"中文字型", L"Chinese font")},
      {IDC_FONT_LANGUAGE_LATIN,
       LocalText(L"西文字体", L"西文字型", L"Western font")},
      {IDC_FONT_APPLY, LocalText(L"应用", L"套用", L"Apply")},
      {IDCANCEL, LocalText(L"关闭", L"關閉", L"Close")},
  };
  for (const auto& [id, text] : labels)
    ::SetDlgItemTextW(m_hWnd, id, text.c_str());
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
  CListBox roles(GetDlgItem(IDC_FONT_ROLE_LIST));
  for (const auto& text : {LocalText(L"输入码", L"輸入碼", L"Preedit"),
                           LocalText(L"候选文字", L"候選文字", L"Candidates"),
                           LocalText(L"候选序号", L"候選序號", L"Labels"),
                           LocalText(L"注释文字", L"註釋文字", L"Comments")})
    roles.AddString(text.c_str());
  CComboBox chinese_fonts(GetDlgItem(IDC_FONT_FAMILY_CHINESE));
  CComboBox latin_fonts(GetDlgItem(IDC_FONT_FAMILY_LATIN));
  for (const auto& family : fonts_) {
    chinese_fonts.AddString(family.c_str());
    latin_fonts.AddString(family.c_str());
  }
  CListBox styles(GetDlgItem(IDC_FONT_STYLE_LIST));
  const std::pair<std::wstring, weasel::FontShape> shapes[] = {
      {LocalText(L"常规", L"標準", L"Regular"), weasel::FontShape::Regular},
      {LocalText(L"斜体", L"斜體", L"Italic"), weasel::FontShape::Italic},
      {LocalText(L"粗体", L"粗體", L"Bold"), weasel::FontShape::Bold},
  };
  for (const auto& [text, shape] : shapes) {
    const int index = styles.AddString(text.c_str());
    if (index >= 0)
      styles.SetItemData(index, static_cast<DWORD_PTR>(shape));
  }
  CListBox points(GetDlgItem(IDC_FONT_SIZE_LIST));
  for (int point = 6; point <= 72; ++point) {
    points.AddString(std::to_wstring(point).c_str());
  }
  loading_ = false;
  LoadRoleChoices();
}

void FontSettingsDialog::LoadRoleChoices() {
  CListBox(GetDlgItem(IDC_FONT_ROLE_LIST)).SetCurSel(static_cast<int>(role_));
  LoadLanguageChoice(static_cast<size_t>(weasel::FontLanguage::Chinese));
  LoadLanguageChoice(static_cast<size_t>(weasel::FontLanguage::Latin));
  LoadSharedChoice();
  RefreshPreview();
}

void FontSettingsDialog::LoadLanguageChoice(size_t language) {
  const bool was_loading = loading_;
  loading_ = true;
  const auto& choice = draft_.choices[role_ * 2 + language];
  const UINT family_id =
      language == 0 ? IDC_FONT_FAMILY_CHINESE : IDC_FONT_FAMILY_LATIN;
  CComboBox family(GetDlgItem(family_id));
  int family_index = family.FindStringExact(-1, choice.family.c_str());
  if (family_index == CB_ERR && !choice.family.empty())
    family_index = family.AddString(choice.family.c_str());
  family.SetCurSel(family_index);
  loading_ = was_loading;
}

void FontSettingsDialog::LoadSharedChoice() {
  const bool was_loading = loading_;
  loading_ = true;
  const auto& chinese = draft_.At(static_cast<weasel::FontRole>(role_),
                                  weasel::FontLanguage::Chinese);
  const auto& latin = draft_.At(static_cast<weasel::FontRole>(role_),
                                weasel::FontLanguage::Latin);
  CListBox points(GetDlgItem(IDC_FONT_SIZE_LIST));
  const int point_index =
      static_cast<int>((std::min)(72ul, (std::max)(6ul, chinese.point)) - 6);
  points.SetCurSel(chinese.point == latin.point ? point_index : -1);
  RECT point_bounds{};
  points.GetClientRect(&point_bounds);
  const int item_height = points.GetItemHeight(0);
  const int visible_rows =
      (std::max)(1, static_cast<int>((point_bounds.bottom - point_bounds.top) /
                                     (std::max)(1, item_height)));
  const int max_top = (std::max)(0, points.GetCount() - visible_rows);
  points.SetTopIndex(
      (std::min)(max_top, (std::max)(0, point_index - visible_rows / 2)));
  if (chinese.point == latin.point) {
    wchar_t point_text[64] = {};
    points.GetText(point_index, point_text);
    ::SetWindowTextW(GetDlgItem(IDC_FONT_SIZE_FRAME), point_text);
  } else {
    ::SetWindowTextW(GetDlgItem(IDC_FONT_SIZE_FRAME), L"");
  }

  CListBox styles(GetDlgItem(IDC_FONT_STYLE_LIST));
  int style_index = -1;
  if (chinese.shape == latin.shape) {
    for (int index = 0; index < styles.GetCount(); ++index) {
      if (styles.GetItemData(index) == static_cast<DWORD_PTR>(chinese.shape)) {
        style_index = index;
        break;
      }
    }
  }
  styles.SetCurSel(style_index);
  if (style_index >= 0) {
    wchar_t style_text[64] = {};
    styles.GetText(style_index, style_text);
    ::SetWindowTextW(GetDlgItem(IDC_FONT_STYLE_FRAME), style_text);
  } else {
    ::SetWindowTextW(GetDlgItem(IDC_FONT_STYLE_FRAME), L"");
  }
  ::InvalidateRect(GetDlgItem(IDC_FONT_STYLE_FRAME), nullptr, TRUE);
  ::InvalidateRect(GetDlgItem(IDC_FONT_SIZE_FRAME), nullptr, TRUE);
  loading_ = was_loading;
}

void FontSettingsDialog::StoreLanguageChoice(size_t language, WORD changed_id) {
  if (loading_)
    return;
  auto& choice = draft_.choices[role_ * 2 + language];
  const UINT family_id =
      language == 0 ? IDC_FONT_FAMILY_CHINESE : IDC_FONT_FAMILY_LATIN;
  CComboBox fonts(GetDlgItem(family_id));
  const int font = fonts.GetCurSel();
  if (changed_id == family_id && font >= 0) {
    const int length = fonts.GetLBTextLen(font);
    std::wstring family(length + 1, L'\0');
    fonts.GetLBText(font, family.data());
    family.resize(length);
    choice.family = family;
  }
  draft_.enabled = true;
}

void FontSettingsDialog::RefreshApplyState() {
  const bool pending = draft_ != initial_;
  settings_navigation::SetUnappliedChanges(
      m_hWnd, settings_navigation::Page::Fonts, pending);
  CWindow(GetDlgItem(IDC_FONT_APPLY))
      .EnableWindow(settings_navigation::HasAnyUnappliedChanges());
  ::SetDlgItemTextW(
      m_hWnd, IDC_FONT_MESSAGE,
      (draft_ != initial_ ? LocalText(L"字体更改等待应用", L"字型變更等待套用",
                                      L"Font changes are ready to apply")
                          : L"")
          .c_str());
}

void FontSettingsDialog::RefreshPreview() {
  HWND preview = GetDlgItem(IDC_FONT_PREVIEW);
  if (!preview)
    return;
  // The preview is owner-drawn and fully double-buffered.  Erasing its
  // rectangular bounds before WM_DRAWITEM briefly paints over the rounded
  // corners when a sibling button initiates the refresh.
  ::RedrawWindow(preview, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void FontSettingsDialog::BeginRolePreviewPulse() {
  role_preview_pulse_started_ = ::GetTickCount64();
  ::SetTimer(m_hWnd, kRolePreviewPulseTimer, 30, nullptr);
  RefreshPreview();
}

LRESULT FontSettingsDialog::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
  if (id != kRolePreviewPulseTimer) {
    handled = FALSE;
    return 0;
  }
  const ULONGLONG elapsed = ::GetTickCount64() - role_preview_pulse_started_;
  if (elapsed >= kRolePreviewPulseDurationMs) {
    ::KillTimer(m_hWnd, kRolePreviewPulseTimer);
    role_preview_pulse_started_ = 0;
  }
  RefreshPreview();
  return 0;
}

weasel::FontSettings FontSettingsDialog::AppearanceDefaults() const {
  weasel::FontSettings result;
  const std::wstring general_family =
      appearance_settings_->PreviewStyleString("font_face", L"Microsoft YaHei");
  const DWORD general_point = static_cast<DWORD>(
      appearance_settings_->PreviewStyleInt("font_point", 11));
  const std::wstring label_family = appearance_settings_->PreviewStyleString(
      "label_font_face", general_family.c_str());
  const DWORD label_point = static_cast<DWORD>(
      appearance_settings_->PreviewStyleInt("label_font_point", 9));
  const std::wstring comment_family = appearance_settings_->PreviewStyleString(
      "comment_font_face", general_family.c_str());
  const DWORD comment_point = static_cast<DWORD>(
      appearance_settings_->PreviewStyleInt("comment_font_point", 10));
  for (size_t role = 0; role < weasel::FontSettings::kRoleCount; ++role) {
    std::wstring family = general_family;
    DWORD point = general_point;
    if (role == static_cast<size_t>(weasel::FontRole::Label)) {
      family = label_family;
      point = label_point;
    } else if (role == static_cast<size_t>(weasel::FontRole::Comment)) {
      family = comment_family;
      point = comment_point;
    }
    for (size_t language = 0; language < weasel::FontSettings::kLanguageCount;
         ++language) {
      result.choices[role * weasel::FontSettings::kLanguageCount + language] = {
          family, (std::max)(6ul, (std::min)(72ul, point)),
          weasel::FontShape::Regular};
    }
  }
  result.enabled = false;
  return result;
}

LRESULT FontSettingsDialog::OnRoleChanged(WORD, WORD id, HWND, BOOL&) {
  if (loading_)
    return 0;
  const int selected = CListBox(GetDlgItem(id)).GetCurSel();
  if (selected < 0)
    return 0;
  role_ = static_cast<size_t>(selected);
  LoadRoleChoices();
  BeginRolePreviewPulse();
  return 0;
}

LRESULT FontSettingsDialog::OnLanguageValueChanged(WORD, WORD id, HWND, BOOL&) {
  const size_t language =
      id == IDC_FONT_FAMILY_CHINESE
          ? static_cast<size_t>(weasel::FontLanguage::Chinese)
          : static_cast<size_t>(weasel::FontLanguage::Latin);
  StoreLanguageChoice(language, id);
  RefreshPreview();
  RefreshApplyState();
  return 0;
}

LRESULT FontSettingsDialog::OnSharedValueChanged(WORD, WORD id, HWND, BOOL&) {
  if (loading_)
    return 0;
  const auto role = static_cast<weasel::FontRole>(role_);
  if (id == IDC_FONT_SIZE_LIST) {
    const int selected = CListBox(GetDlgItem(id)).GetCurSel();
    if (selected >= 0) {
      const DWORD point = static_cast<DWORD>(selected + 6);
      draft_.At(role, weasel::FontLanguage::Chinese).point = point;
      draft_.At(role, weasel::FontLanguage::Latin).point = point;
    }
  } else {
    CListBox styles(GetDlgItem(id));
    const int selected = styles.GetCurSel();
    if (selected >= 0) {
      const auto shape =
          static_cast<weasel::FontShape>(styles.GetItemData(selected));
      draft_.At(role, weasel::FontLanguage::Chinese).shape = shape;
      draft_.At(role, weasel::FontLanguage::Latin).shape = shape;
    }
  }
  draft_.enabled = true;
  LoadSharedChoice();
  RefreshPreview();
  RefreshApplyState();
  return 0;
}

LRESULT FontSettingsDialog::OnPreviewLayoutChanged(WORD, WORD id, HWND, BOOL&) {
  preview_vertical_ = id == IDC_FONT_PREVIEW_VERTICAL;
  CheckRadioButton(IDC_FONT_PREVIEW_HORIZONTAL, IDC_FONT_PREVIEW_VERTICAL, id);
  RefreshPreview();
  return 0;
}

LRESULT FontSettingsDialog::OnPreviewThemeChanged(WORD, WORD id, HWND, BOOL&) {
  preview_dark_ = id == IDC_FONT_PREVIEW_DARK;
  CheckRadioButton(IDC_FONT_PREVIEW_LIGHT, IDC_FONT_PREVIEW_DARK, id);
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
  const auto user_settings = weasel::UserSettings::Load();
  const bool dark = preview_dark_;
  const auto active = appearance_settings_->ActiveAppearance();
  const size_t scheme_index =
      (user_settings.acrylic ? 0u : 2u) + (dark ? 1u : 0u);
  std::string scheme = active[scheme_index];
  if (scheme.empty() && user_settings.acrylic)
    scheme = active[2u + (dark ? 1u : 0u)];
  const auto configured_color = [&](const char* key, COLORREF light,
                                    COLORREF dark_color) {
    return appearance_settings_->PreviewColor(scheme, key,
                                              dark ? dark_color : light);
  };
  const COLORREF background = settings_navigation::Mix(
      dark ? RGB(38, 38, 38) : GetSysColor(COLOR_BTNFACE),
      dark ? RGB(22, 22, 22) : GetSysColor(COLOR_WINDOW), 84);
  const COLORREF configured_panel =
      configured_color("back_color", RGB(249, 249, 249), RGB(44, 44, 44));
  const COLORREF panel =
      user_settings.acrylic
          ? settings_navigation::Mix(configured_panel, background, 176)
          : configured_panel;
  const COLORREF border =
      configured_color("border_color", RGB(213, 213, 213), RGB(80, 80, 80));
  const COLORREF preedit_color =
      configured_color("text_color", RGB(75, 75, 75), RGB(220, 220, 220));
  const COLORREF candidate_color = configured_color(
      "candidate_text_color", RGB(32, 32, 32), RGB(242, 242, 242));
  const COLORREF label_color =
      configured_color("label_color", RGB(104, 104, 104), RGB(176, 176, 176));
  const COLORREF comment_color = configured_color(
      "comment_text_color", RGB(104, 104, 104), RGB(176, 176, 176));
  const COLORREF selected = configured_color(
      "hilited_candidate_back_color", RGB(229, 229, 229), RGB(66, 66, 66));
  const COLORREF selected_candidate_color = configured_color(
      "hilited_candidate_text_color", RGB(17, 17, 17), RGB(255, 255, 255));
  const COLORREF selected_label_color = configured_color(
      "hilited_label_color", RGB(0, 103, 192), RGB(96, 205, 255));
  const COLORREF selected_comment_color = configured_color(
      "hilited_comment_text_color", RGB(84, 84, 84), RGB(210, 210, 210));
  const COLORREF accent = configured_color("hilited_mark_color",
                                           RGB(0, 103, 192), RGB(96, 205, 255));
  BYTE role_cue_alpha = 0;
  if (role_preview_pulse_started_) {
    const ULONGLONG elapsed = ::GetTickCount64() - role_preview_pulse_started_;
    constexpr int kMaximumAlpha = 52;
    if (elapsed < 120) {
      role_cue_alpha =
          static_cast<BYTE>(kMaximumAlpha * static_cast<int>(elapsed) / 120);
    } else if (elapsed < 520) {
      role_cue_alpha = kMaximumAlpha;
    } else if (elapsed < kRolePreviewPulseDurationMs) {
      role_cue_alpha = static_cast<BYTE>(
          kMaximumAlpha *
          static_cast<int>(kRolePreviewPulseDurationMs - elapsed) /
          static_cast<int>(kRolePreviewPulseDurationMs - 520));
    }
  }
  // DRAWITEM can provide a parent-owned DC whose clip is wider than the child
  // window region.  Build the rounded canvas into the buffer itself so the
  // result stays rounded regardless of whether a button or the preview window
  // initiated this paint.
  HBRUSH card_surface =
      CreateSolidBrush(settings_theme::GetColor(COLOR_WINDOW));
  FillRect(dc, &bounds, card_surface);
  DeleteObject(card_surface);
  const int outer_diameter = (std::max)(
      4, static_cast<int>(settings_navigation::MapDialogUnits(
                              m_hWnd, 0, 0, settings_navigation::kCardRadiusDlu,
                              settings_navigation::kCardRadiusDlu)
                              .right));
  const int saved_outer_dc = ::SaveDC(dc);
  HRGN outer_clip =
      ::CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1,
                           bounds.bottom + 1, outer_diameter, outer_diameter);
  if (outer_clip) {
    ::SelectClipRgn(dc, outer_clip);
    ::DeleteObject(outer_clip);
  }
  HBRUSH fill = CreateSolidBrush(background);
  FillRect(dc, &bounds, fill);
  DeleteObject(fill);

  const std::wstring configured_font =
      appearance_settings_->PreviewStyleString("font_face", L"Microsoft YaHei");
  const int configured_font_point =
      appearance_settings_->PreviewStyleInt("font_point", 11);
  const std::wstring configured_label_font =
      appearance_settings_->PreviewStyleString("label_font_face",
                                               configured_font.c_str());
  const int configured_label_point =
      appearance_settings_->PreviewStyleInt("label_font_point", 9);
  const std::wstring configured_comment_font =
      appearance_settings_->PreviewStyleString("comment_font_face",
                                               configured_font.c_str());
  const int configured_comment_point =
      appearance_settings_->PreviewStyleInt("comment_font_point", 10);
  const auto choice = [&](weasel::FontRole role,
                          weasel::FontLanguage language) {
    if (draft_.enabled)
      return draft_.At(role, language);
    weasel::FontChoice result;
    if (role == weasel::FontRole::Label) {
      result.family = configured_label_font;
      result.point = configured_label_point;
    } else if (role == weasel::FontRole::Comment) {
      result.family = configured_comment_font;
      result.point = configured_comment_point;
    } else {
      result.family = configured_font;
      result.point = configured_font_point;
    }
    return result;
  };
  const auto preedit_chinese =
      choice(weasel::FontRole::Preedit, weasel::FontLanguage::Chinese);
  const auto preedit_latin =
      choice(weasel::FontRole::Preedit, weasel::FontLanguage::Latin);
  const auto label_choice =
      choice(weasel::FontRole::Label, weasel::FontLanguage::Latin);
  const auto candidate_chinese =
      choice(weasel::FontRole::Candidate, weasel::FontLanguage::Chinese);
  const auto candidate_latin =
      choice(weasel::FontRole::Candidate, weasel::FontLanguage::Latin);
  const auto comment_chinese =
      choice(weasel::FontRole::Comment, weasel::FontLanguage::Chinese);
  const auto comment_latin =
      choice(weasel::FontRole::Comment, weasel::FontLanguage::Latin);
  constexpr size_t kPreviewCandidateLimit = 5;
  const std::array<const wchar_t*, kPreviewCandidateLimit> candidates = {
      L"天涯是我的爱", L"填鸭式", L"天涯石", L"天涯", L"tian ya"};
  const std::array<const wchar_t*, kPreviewCandidateLimit> comments = {
      L"", L"", L"", L"tian ya", L"天涯"};
  const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  const auto pixels = [dpi](int value) {
    return (std::max)(0, MulDiv(value, dpi, 96));
  };
  const int outer_margin = pixels(8);
  const int hilite_padding_x =
      pixels(appearance_settings_->PreviewLayoutInt("hilite_padding_x", 8));
  const int hilite_padding_y =
      pixels(appearance_settings_->PreviewLayoutInt("hilite_padding_y", 4));
  const int panel_padding_x = pixels((std::max)(
      appearance_settings_->PreviewLayoutInt("margin_x", 11),
      appearance_settings_->PreviewLayoutInt("hilite_padding_x", 8)));
  const int panel_padding_y = pixels((std::max)(
      appearance_settings_->PreviewLayoutInt("margin_y", 7),
      appearance_settings_->PreviewLayoutInt("hilite_padding_y", 4)));
  const int label_gap =
      pixels(appearance_settings_->PreviewLayoutInt("hilite_spacing", 5));
  const int comment_gap = label_gap;
  const int item_gap =
      pixels(appearance_settings_->PreviewLayoutInt("candidate_spacing", 6));
  const int preedit_gap =
      pixels(appearance_settings_->PreviewLayoutInt("spacing", 5));
  const int panel_radius =
      pixels(appearance_settings_->PreviewLayoutInt("corner_radius", 11));
  const int highlight_radius =
      pixels(appearance_settings_->PreviewLayoutInt("round_corner", 8));
  const int border_width =
      pixels(appearance_settings_->PreviewLayoutInt("border_width", 1));

  const std::wstring preedit = L"苍茫的 tian ya shi wo de ai";
  const SIZE preedit_size =
      MeasureMixedPreviewText(dc, preedit, preedit_chinese, preedit_latin);
  std::array<SIZE, kPreviewCandidateLimit> label_sizes{};
  std::array<SIZE, kPreviewCandidateLimit> candidate_sizes{};
  std::array<SIZE, kPreviewCandidateLimit> comment_sizes{};
  std::array<int, kPreviewCandidateLimit> item_widths{};
  std::array<int, kPreviewCandidateLimit> item_heights{};
  int widest_item = 0;
  int total_item_width = 0;
  int total_item_height = 0;
  int horizontal_height = 0;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const std::wstring number = std::to_wstring(index + 1) + L".";
    label_sizes[index] = MeasurePreviewText(dc, number, label_choice);
    candidate_sizes[index] = MeasureMixedPreviewText(
        dc, candidates[index], candidate_chinese, candidate_latin);
    if (*comments[index])
      comment_sizes[index] = MeasureMixedPreviewText(
          dc, comments[index], comment_chinese, comment_latin);
    item_widths[index] =
        hilite_padding_x * 2 + label_sizes[index].cx + label_gap +
        candidate_sizes[index].cx +
        (*comments[index] ? comment_gap + comment_sizes[index].cx : 0);
    item_heights[index] =
        (std::max)(label_sizes[index].cy, (std::max)(candidate_sizes[index].cy,
                                                     comment_sizes[index].cy)) +
        hilite_padding_y * 2;
    widest_item = (std::max)(widest_item, item_widths[index]);
    total_item_width += item_widths[index];
    total_item_height += item_heights[index];
    horizontal_height = (std::max)(horizontal_height, item_heights[index]);
  }
  total_item_width += item_gap * static_cast<int>(kPreviewCandidateLimit - 1);
  total_item_height += item_gap * static_cast<int>(kPreviewCandidateLimit - 1);

  const int content_width = preview_vertical_ ? widest_item : total_item_width;
  const int rows_height =
      preview_vertical_ ? total_item_height : horizontal_height;
  int panel_width = (std::max)(
      pixels(appearance_settings_->PreviewLayoutInt("min_width", 130)),
      (std::max)(static_cast<int>(preedit_size.cx), content_width) +
          panel_padding_x * 2);
  const int configured_max_width =
      pixels(appearance_settings_->PreviewLayoutInt("max_width", 0));
  if (configured_max_width > 0)
    panel_width = (std::min)(panel_width, configured_max_width);
  int panel_height =
      panel_padding_y * 2 + preedit_size.cy + preedit_gap + rows_height;
  panel_width = (std::min)(panel_width, width - outer_margin * 2);
  panel_height = (std::min)(panel_height, height - outer_margin * 2);
  const int panel_left = bounds.left + (width - panel_width) / 2;
  const int panel_top = bounds.top + (height - panel_height) / 2;
  RECT panel_bounds{panel_left, panel_top, panel_left + panel_width,
                    panel_top + panel_height};

  const int panel_diameter = panel_radius * 2;
  const int highlight_diameter = highlight_radius * 2;
  HBRUSH shadow_brush = CreateSolidBrush(settings_navigation::Mix(
      dark ? RGB(0, 0, 0) : GetSysColor(COLOR_3DSHADOW), background, 28));
  HPEN no_pen = CreatePen(PS_NULL, 0, background);
  HGDIOBJ old_pen = SelectObject(dc, no_pen);
  HGDIOBJ old_brush = SelectObject(dc, shadow_brush);
  RoundRect(dc, panel_bounds.left + 1, panel_bounds.top + 2,
            panel_bounds.right + 1, panel_bounds.bottom + 2, panel_diameter,
            panel_diameter);
  SelectObject(dc, old_brush);
  DeleteObject(shadow_brush);
  HBRUSH panel_brush = CreateSolidBrush(panel);
  HPEN panel_pen = border_width > 0 ? CreatePen(PS_SOLID, border_width, border)
                                    : CreatePen(PS_NULL, 0, border);
  SelectObject(dc, panel_pen);
  old_brush = SelectObject(dc, panel_brush);
  RoundRect(dc, panel_bounds.left, panel_bounds.top, panel_bounds.right,
            panel_bounds.bottom, panel_diameter, panel_diameter);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(panel_pen);
  DeleteObject(panel_brush);
  DeleteObject(no_pen);

  // Keep the configured point sizes exact.  When an unusually large font is
  // selected, clip the real-size rendering to the candidate panel instead of
  // scaling it down and giving a misleading preview.
  const int saved_preview_dc = ::SaveDC(dc);
  HRGN panel_clip = ::CreateRoundRectRgn(
      panel_bounds.left, panel_bounds.top, panel_bounds.right,
      panel_bounds.bottom, panel_diameter, panel_diameter);
  if (panel_clip) {
    ::SelectClipRgn(dc, panel_clip);
    ::DeleteObject(panel_clip);
  }

  int text_x = panel_bounds.left + panel_padding_x;
  int text_y = panel_bounds.top + panel_padding_y;
  if (role_cue_alpha &&
      role_ == static_cast<size_t>(weasel::FontRole::Preedit)) {
    DrawPreviewRoleCue(dc,
                       RECT{text_x, text_y, text_x + preedit_size.cx,
                            text_y + preedit_size.cy},
                       role_cue_alpha);
  }
  DrawMixedPreviewText(dc, text_x, text_y, preedit, preedit_chinese,
                       preedit_latin, preedit_color);
  const int rows_top = text_y + preedit_size.cy + preedit_gap;
  int item_left = panel_bounds.left + panel_padding_x;
  int item_top = rows_top;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const int item_width = item_widths[index];
    const int item_height =
        preview_vertical_ ? item_heights[index] : horizontal_height;
    if (index == 0) {
      HBRUSH highlight_brush = CreateSolidBrush(selected);
      HPEN highlight_pen = CreatePen(PS_NULL, 0, selected);
      const HGDIOBJ previous_brush = SelectObject(dc, highlight_brush);
      const HGDIOBJ previous_pen = SelectObject(dc, highlight_pen);
      RoundRect(dc, item_left, item_top, item_left + item_width,
                item_top + item_height, highlight_diameter, highlight_diameter);
      SelectObject(dc, previous_pen);
      SelectObject(dc, previous_brush);
      DeleteObject(highlight_pen);
      DeleteObject(highlight_brush);
      HBRUSH marker = CreateSolidBrush(accent);
      HPEN marker_pen = CreatePen(PS_NULL, 0, accent);
      const HGDIOBJ previous_marker_brush = SelectObject(dc, marker);
      const HGDIOBJ previous_marker_pen = SelectObject(dc, marker_pen);
      const int marker_width = (std::max)(2, pixels(3));
      RECT marker_bounds{item_left + pixels(2), item_top + hilite_padding_y,
                         item_left + pixels(2) + marker_width,
                         item_top + item_height - hilite_padding_y};
      RoundRect(dc, marker_bounds.left, marker_bounds.top, marker_bounds.right,
                marker_bounds.bottom, marker_width, marker_width);
      SelectObject(dc, previous_marker_pen);
      SelectObject(dc, previous_marker_brush);
      DeleteObject(marker_pen);
      DeleteObject(marker);
    }

    int x = item_left + hilite_padding_x;
    const std::wstring number = std::to_wstring(index + 1) + L".";
    const int label_y = item_top + (item_height - label_sizes[index].cy) / 2;
    if (role_cue_alpha &&
        role_ == static_cast<size_t>(weasel::FontRole::Label)) {
      DrawPreviewRoleCue(dc,
                         RECT{x, label_y, x + label_sizes[index].cx,
                              label_y + label_sizes[index].cy},
                         role_cue_alpha);
    }
    x += DrawPreviewText(dc, x, label_y, number, label_choice,
                         index == 0 ? selected_label_color : label_color) +
         label_gap;
    const int candidate_y =
        item_top + (item_height - candidate_sizes[index].cy) / 2;
    if (role_cue_alpha &&
        role_ == static_cast<size_t>(weasel::FontRole::Candidate)) {
      DrawPreviewRoleCue(dc,
                         RECT{x, candidate_y, x + candidate_sizes[index].cx,
                              candidate_y + candidate_sizes[index].cy},
                         role_cue_alpha);
    }
    x += DrawMixedPreviewText(
             dc, x, candidate_y, candidates[index], candidate_chinese,
             candidate_latin,
             index == 0 ? selected_candidate_color : candidate_color) +
         comment_gap;
    if (*comments[index]) {
      const int comment_y =
          item_top + (item_height - comment_sizes[index].cy) / 2;
      if (role_cue_alpha &&
          role_ == static_cast<size_t>(weasel::FontRole::Comment)) {
        DrawPreviewRoleCue(dc,
                           RECT{x, comment_y, x + comment_sizes[index].cx,
                                comment_y + comment_sizes[index].cy},
                           role_cue_alpha);
      }
      DrawMixedPreviewText(dc, x, comment_y, comments[index], comment_chinese,
                           comment_latin,
                           index == 0 ? selected_comment_color : comment_color);
    }

    if (preview_vertical_)
      item_top += item_height + item_gap;
    else
      item_left += item_width + item_gap;
  }
  if (saved_preview_dc)
    ::RestoreDC(dc, saved_preview_dc);
  if (saved_outer_dc)
    ::RestoreDC(dc, saved_outer_dc);
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
  if (draw->CtlID == IDC_FONT_FAMILY_CHINESE ||
      draw->CtlID == IDC_FONT_FAMILY_LATIN) {
    settings_navigation::DrawComboItem(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_FONT_EDITOR_CARD ||
      draw->CtlID == IDC_FONT_PREVIEW_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_FONT_ROLE_FRAME ||
      draw->CtlID == IDC_FONT_STYLE_FRAME ||
      draw->CtlID == IDC_FONT_SIZE_FRAME) {
    DrawFontListFrame(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_FONT_ROLE_LIST || draw->CtlID == IDC_FONT_STYLE_LIST ||
      draw->CtlID == IDC_FONT_SIZE_LIST) {
    DrawFontListItem(*draw);
    return TRUE;
  }
  if (draw->CtlID != IDC_FONT_PREVIEW) {
    handled = FALSE;
    return 0;
  }
  DrawPreview(*draw);
  return TRUE;
}

LRESULT FontSettingsDialog::OnMeasureItem(UINT,
                                          WPARAM,
                                          LPARAM parameter,
                                          BOOL& handled) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  if (!measure || (measure->CtlID != IDC_FONT_FAMILY_CHINESE &&
                   measure->CtlID != IDC_FONT_FAMILY_LATIN)) {
    handled = FALSE;
    return 0;
  }
  measure->itemHeight = settings_navigation::MeasureComboItemHeight(m_hWnd);
  handled = TRUE;
  return TRUE;
}

LRESULT FontSettingsDialog::OnStaticColor(UINT,
                                          WPARAM dc,
                                          LPARAM window,
                                          BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id < IDC_FONT_ROLE_LABEL || id > IDC_FONT_PREVIEW) &&
      id != IDC_FONT_LANGUAGE_CHINESE && id != IDC_FONT_LANGUAGE_LATIN &&
      id != IDC_FONT_PREVIEW_HINT) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkMode(context, TRANSPARENT);
  if (id == IDC_FONT_PREVIEW_HINT)
    ::SetTextColor(context, settings_theme::GetColor(COLOR_GRAYTEXT));
  return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
}

LRESULT FontSettingsDialog::OnButtonColor(UINT,
                                          WPARAM dc,
                                          LPARAM window,
                                          BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id < IDC_FONT_PREVIEW_HORIZONTAL || id > IDC_FONT_PREVIEW_VERTICAL) &&
      (id < IDC_FONT_PREVIEW_LIGHT || id > IDC_FONT_PREVIEW_DARK)) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkColor(context, settings_theme::GetColor(COLOR_WINDOW));
  return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
}

LRESULT FontSettingsDialog::OnRestore(WORD, WORD, HWND, BOOL&) {
  draft_ = AppearanceDefaults();
  LoadRoleChoices();
  RefreshPreview();
  RefreshApplyState();
  return 0;
}

bool FontSettingsDialog::ApplyChanges() {
  if (draft_.Save() != ERROR_SUCCESS ||
      weasel::FontSettings::Load() != draft_) {
    ::MessageBoxW(m_hWnd,
                  LocalText(L"无法保存字体设置。", L"無法儲存字型設定。",
                            L"Could not save font settings.")
                      .c_str(),
                  LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
                  MB_OK | MB_ICONERROR);
    return false;
  }
  initial_ = draft_;
  weasel::NotifyUserSettingsChanged();
  RefreshApplyState();
  return true;
}

LRESULT FontSettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  if (settings_navigation::RequestApply(m_hWnd))
    return 0;
  ApplyChanges();
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

bool FontSettingsDialog::HasUnappliedChanges() const {
  return draft_ != initial_;
}

bool FontSettingsDialog::ConfirmClose() {
  return ConfirmDiscard();
}

LRESULT FontSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (ConfirmClose())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT FontSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (ConfirmClose())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT FontSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::Fonts)
    return 0;
  if (settings_navigation::RequestNavigate(m_hWnd, id))
    return 0;
  if (!ConfirmDiscard())
    return 0;
  EndDialog(id);
  return 0;
}
