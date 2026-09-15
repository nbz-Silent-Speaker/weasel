#include "stdafx.h"
#include "StatusIconSettingsDialog.h"

#include <WeaselUtility.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <gdiplus.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {
constexpr UINT kServerEnglishIcon = 101;
constexpr UINT kServerChineseIcon = 102;

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

HICON LoadIconFile(const std::wstring& path) {
  if (path.empty())
    return nullptr;
  HICON icon = reinterpret_cast<HICON>(
      ::LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
  if (icon)
    return icon;
  Gdiplus::Bitmap image(path.c_str());
  if (image.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  return image.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
}

HICON LoadBuiltInIcon(bool english) {
  wchar_t module_path[MAX_PATH] = {};
  if (::GetModuleFileNameW(nullptr, module_path, MAX_PATH)) {
    std::filesystem::path server(module_path);
    server.replace_filename(L"WeaselServer.exe");
    HMODULE module =
        ::LoadLibraryExW(server.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module) {
      const UINT resource = english ? kServerEnglishIcon : kServerChineseIcon;
      HICON icon = reinterpret_cast<HICON>(::LoadImageW(
          module, MAKEINTRESOURCEW(resource), IMAGE_ICON, 32, 32, 0));
      ::FreeLibrary(module);
      if (icon)
        return icon;
    }
  }
  return ::CopyIcon(::LoadIconW(nullptr, IDI_APPLICATION));
}

HICON AddBadge(HICON base, weasel::StatusIconCapsBadge badge) {
  if (!base)
    return nullptr;
  constexpr INT size = 32;
  Gdiplus::Bitmap canvas(size, size, PixelFormat32bppARGB);
  Gdiplus::Graphics graphics(&canvas);
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
  graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
  Gdiplus::Bitmap source(base);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.DrawImage(&source, Gdiplus::Rect(0, 0, size, size));
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  Gdiplus::SolidBrush fill(Gdiplus::Color(255, 213, 67, 62));
  Gdiplus::Pen outline(Gdiplus::Color(235, 255, 255, 255), 2.0f);
  if (badge == weasel::StatusIconCapsBadge::Dot) {
    const Gdiplus::RectF dot(20.0f, 4.0f, 8.0f, 8.0f);
    graphics.FillEllipse(&fill, dot);
    graphics.DrawEllipse(&outline, dot);
  } else {
    const Gdiplus::RectF circle(16.0f, 0.0f, 16.0f, 16.0f);
    graphics.FillEllipse(&fill, circle);
    graphics.DrawEllipse(&outline, circle);
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleBold,
                       Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(L"A", 1, &font, circle, &format, &text);
  }
  HICON result = nullptr;
  return canvas.GetHICON(&result) == Gdiplus::Ok ? result : nullptr;
}

bool IsSupportedIcon(const std::filesystem::path& path) {
  std::wstring extension = path.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t character) {
                   return static_cast<wchar_t>(std::towlower(character));
                 });
  return extension == L".png" || extension == L".ico";
}

void AddRoundedRectangle(Gdiplus::GraphicsPath* path,
                         const Gdiplus::Rect& rectangle,
                         int radius) {
  const int diameter = radius * 2;
  path->AddArc(rectangle.X, rectangle.Y, diameter, diameter, 180, 90);
  path->AddArc(rectangle.GetRight() - diameter, rectangle.Y, diameter, diameter,
               270, 90);
  path->AddArc(rectangle.GetRight() - diameter,
               rectangle.GetBottom() - diameter, diameter, diameter, 0, 90);
  path->AddArc(rectangle.X, rectangle.GetBottom() - diameter, diameter,
               diameter, 90, 90);
  path->CloseFigure();
}

void LayoutStatusIconPage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_STATUS_RESTORE,
              settings_navigation::kBottomActionLeftDlu,
              settings_navigation::kBottomActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_BASE_CARD, settings_navigation::kPageInsetDlu,
              settings_navigation::kFirstCardTopDlu,
              settings_navigation::kPageBodyWidthDlu, 62);
  MoveControl(dialog, IDC_STATUS_BASE_TITLE, 26, 22, 210, 12);
  MoveControl(dialog, IDC_STATUS_CHINESE_LABEL, 28, 39, 120, 12);
  MoveControl(dialog, IDC_STATUS_CHINESE_ICON, 396, 34, 20, 20);
  MoveControl(dialog, IDC_STATUS_CHINESE_CHANGE, 432, 35,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_ENGLISH_LABEL, 28, 58, 120, 12);
  MoveControl(dialog, IDC_STATUS_ENGLISH_ICON, 396, 53, 20, 20);
  MoveControl(dialog, IDC_STATUS_ENGLISH_CHANGE, 432, 54,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);

  MoveControl(dialog, IDC_STATUS_CAPS_CARD, settings_navigation::kPageInsetDlu,
              84, settings_navigation::kPageBodyWidthDlu, 76);
  MoveControl(dialog, IDC_STATUS_CAPS_TITLE, 26, 92, 160, 12);
  MoveControl(dialog, IDC_STATUS_CAPS_AUTOMATIC, 286, 89, 108,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_CAPS_CUSTOM, 398, 89, 114,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_BADGE_LETTER, 28, 113, 112, 16);
  MoveControl(dialog, IDC_STATUS_BADGE_DOT, 28, 135, 112, 16);
  MoveControl(dialog, IDC_STATUS_AUTO_CHINESE_ICON, 396, 110, 20, 20);
  MoveControl(dialog, IDC_STATUS_AUTO_CHINESE_LABEL, 424, 114, 88, 12);
  MoveControl(dialog, IDC_STATUS_AUTO_ENGLISH_ICON, 396, 132, 20, 20);
  MoveControl(dialog, IDC_STATUS_AUTO_ENGLISH_LABEL, 424, 136, 88, 12);
  MoveControl(dialog, IDC_STATUS_CHINESE_CAPS_LABEL, 28, 114, 160, 12);
  MoveControl(dialog, IDC_STATUS_CHINESE_CAPS_ICON, 396, 109, 20, 20);
  MoveControl(dialog, IDC_STATUS_CHINESE_CAPS_CHANGE, 432, 110,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_ENGLISH_CAPS_LABEL, 28, 136, 160, 12);
  MoveControl(dialog, IDC_STATUS_ENGLISH_CAPS_ICON, 396, 131, 20, 20);
  MoveControl(dialog, IDC_STATUS_ENGLISH_CAPS_CHANGE, 432, 132,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);

  MoveControl(dialog, IDC_STATUS_TASKBAR_CARD,
              settings_navigation::kPageInsetDlu, 168,
              settings_navigation::kPageBodyWidthDlu, 60);
  MoveControl(dialog, IDC_STATUS_TASKBAR_TITLE, 26, 176, 100, 12);
  MoveControl(dialog, IDC_STATUS_PREVIEW_CHINESE, 150, 173, 56,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_ENGLISH, 210, 173, 56,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_CHINESE_CAPS, 270, 173, 78,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_ENGLISH_CAPS, 352, 173, 78,
              settings_navigation::kButtonHeightDlu);
}
}  // namespace

std::wstring StatusIconSettingsDialog::LocalText(const wchar_t* simplified,
                                                 const wchar_t* traditional,
                                                 const wchar_t* english) const {
  return IsChinese() ? (IsSimplifiedChinese() ? simplified : traditional)
                     : english;
}

LRESULT StatusIconSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  LayoutStatusIconPage(m_hWnd);
  Gdiplus::GdiplusStartupInput startup;
  if (Gdiplus::GdiplusStartup(&graphics_token_, &startup, nullptr) !=
      Gdiplus::Ok) {
    EndDialog(IDCANCEL);
    return TRUE;
  }
  initial_ = weasel::StatusIconSettings::Load();
  draft_ = initial_;

  LOGFONTW title{};
  ::GetObjectW(GetFont(), sizeof(title), &title);
  title.lfWeight = FW_SEMIBOLD;
  if (heading_font_.CreateFontIndirect(&title)) {
    for (UINT id : {IDC_STATUS_BASE_TITLE, IDC_STATUS_CAPS_TITLE,
                    IDC_STATUS_TASKBAR_TITLE}) {
      CWindow(GetDlgItem(id)).SetFont(heading_font_);
    }
  }

  Localize();
  for (UINT id :
       {IDC_STATUS_BASE_CARD, IDC_STATUS_CAPS_CARD, IDC_STATUS_TASKBAR_CARD})
    settings_navigation::PrepareCard(m_hWnd, id);
  for (UINT id : {IDC_STATUS_RESTORE, IDC_STATUS_CHINESE_CHANGE,
                  IDC_STATUS_ENGLISH_CHANGE, IDC_STATUS_CHINESE_CAPS_CHANGE,
                  IDC_STATUS_ENGLISH_CAPS_CHANGE})
    settings_navigation::StyleActionButton(m_hWnd, id);
  for (UINT id :
       {IDC_STATUS_CAPS_AUTOMATIC, IDC_STATUS_CAPS_CUSTOM,
        IDC_STATUS_PREVIEW_CHINESE, IDC_STATUS_PREVIEW_ENGLISH,
        IDC_STATUS_PREVIEW_CHINESE_CAPS, IDC_STATUS_PREVIEW_ENGLISH_CAPS})
    settings_navigation::StyleToggle(m_hWnd, id);
  ::ShowWindow(GetDlgItem(IDC_STATUS_TASKBAR_ICON), SW_HIDE);
  CheckRadioButton(IDC_STATUS_CAPS_AUTOMATIC, IDC_STATUS_CAPS_CUSTOM,
                   draft_.caps_mode == weasel::StatusIconCapsMode::Automatic
                       ? IDC_STATUS_CAPS_AUTOMATIC
                       : IDC_STATUS_CAPS_CUSTOM);
  CheckRadioButton(IDC_STATUS_BADGE_LETTER, IDC_STATUS_BADGE_DOT,
                   draft_.caps_badge == weasel::StatusIconCapsBadge::LetterA
                       ? IDC_STATUS_BADGE_LETTER
                       : IDC_STATUS_BADGE_DOT);
  CheckRadioButton(IDC_STATUS_PREVIEW_CHINESE, IDC_STATUS_PREVIEW_ENGLISH_CAPS,
                   IDC_STATUS_PREVIEW_CHINESE);
  RefreshMode();
  RefreshPreviews();
  RefreshApplyState();
  settings_navigation::Install(m_hWnd, settings_navigation::Page::StatusIcons,
                               {IDC_STATUS_APPLY,
                                IDCANCEL,
                                WeaselUserDataPath().wstring(),
                                {IDC_STATUS_TITLE, IDC_STATUS_MESSAGE}});
  RefreshApplyState();
  CenterWindow();
  return TRUE;
}

LRESULT StatusIconSettingsDialog::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
  for (HICON& icon : preview_icons_) {
    if (icon) {
      ::DestroyIcon(icon);
      icon = nullptr;
    }
  }
  if (graphics_token_)
    Gdiplus::GdiplusShutdown(graphics_token_);
  graphics_token_ = 0;
  return 0;
}

LRESULT StatusIconSettingsDialog::OnDrawItem(UINT,
                                             WPARAM,
                                             LPARAM parameter,
                                             BOOL& handled) {
  const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
  if (draw->CtlID == IDC_STATUS_TASKBAR_CARD) {
    DrawTaskbarPreview(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_STATUS_BASE_CARD ||
      draw->CtlID == IDC_STATUS_CAPS_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  handled = FALSE;
  return 0;
}

LRESULT StatusIconSettingsDialog::OnStaticColor(UINT,
                                                WPARAM dc,
                                                LPARAM window,
                                                BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if (id < IDC_STATUS_BASE_TITLE || id > IDC_STATUS_TASKBAR_TITLE) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkMode(context, TRANSPARENT);
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

LRESULT StatusIconSettingsDialog::OnButtonColor(UINT,
                                                WPARAM dc,
                                                LPARAM window,
                                                BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if (id < IDC_STATUS_CHINESE_CHANGE || id > IDC_STATUS_PREVIEW_ENGLISH_CAPS) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkColor(context, ::GetSysColor(COLOR_WINDOW));
  return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
}

void StatusIconSettingsDialog::DrawTaskbarPreview(const DRAWITEMSTRUCT& draw) {
  settings_navigation::DrawCard(draw);
  const int width = draw.rcItem.right - draw.rcItem.left;
  const int height = draw.rcItem.bottom - draw.rcItem.top;
  const RECT units = settings_navigation::MapDialogUnits(m_hWnd, 0, 0, 10, 26);
  const int padding = units.right;
  const int top = units.bottom;
  if (width <= padding * 2 || height <= top + padding / 2)
    return;

  using namespace Gdiplus;
  Graphics canvas(draw.hDC);
  canvas.SetSmoothingMode(SmoothingModeAntiAlias);
  const Rect scene(draw.rcItem.left + padding, draw.rcItem.top + top,
                   width - padding * 2, height - top - padding / 2);
  GraphicsPath scene_path;
  AddRoundedRectangle(&scene_path, scene, (std::max)(4, scene.Height / 8));
  canvas.SetClip(&scene_path);
  LinearGradientBrush desktop(scene, Color(255, 53, 113, 142),
                              Color(255, 112, 78, 126), 28.0f);
  canvas.FillRectangle(&desktop, scene);
  const int taskbar_height = (std::max)(18, scene.Height * 2 / 5);
  const Rect taskbar(scene.X, scene.GetBottom() - taskbar_height, scene.Width,
                     taskbar_height);
  SolidBrush taskbar_fill(Color(224, 28, 30, 34));
  canvas.FillRectangle(&taskbar_fill, taskbar);

  const int icon_size = (std::min)(16, taskbar_height - 4);
  int right = taskbar.GetRight() - 8;
  FontFamily family(L"Segoe UI");
  Font time_font(&family, 8.0f, FontStyleRegular, UnitPixel);
  SolidBrush foreground(Color(255, 245, 245, 245));
  StringFormat format;
  format.SetAlignment(StringAlignmentFar);
  format.SetLineAlignment(StringAlignmentCenter);
  SYSTEMTIME now{};
  ::GetLocalTime(&now);
  wchar_t clock_text[32]{};
  swprintf_s(clock_text, L"%02d:%02d  %04d/%02d/%02d", now.wHour, now.wMinute,
             now.wYear, now.wMonth, now.wDay);
  const RectF clock(static_cast<REAL>(right - 66), static_cast<REAL>(taskbar.Y),
                    64.0f, static_cast<REAL>(taskbar.Height));
  canvas.DrawString(clock_text, -1, &time_font, clock, &format, &foreground);
  right -= 76;

  Pen glyph(Color(255, 238, 238, 238), 1.2f);
  const int center_y = taskbar.Y + taskbar.Height / 2;
  canvas.DrawEllipse(&glyph, right - 12, center_y - 5, 9, 9);
  canvas.DrawLine(&glyph, right - 8, center_y - 8, right - 8, center_y - 6);
  right -= 22;
  for (int bar = 0; bar < 3; ++bar) {
    canvas.DrawLine(&glyph, right - 12 + bar * 4, center_y + 5,
                    right - 12 + bar * 4, center_y + 2 - bar * 3);
  }
  canvas.Flush(FlushIntentionSync);
  right -= 24;
  if (preview_icons_[6]) {
    ::DrawIconEx(draw.hDC, right - icon_size, center_y - icon_size / 2,
                 preview_icons_[6], icon_size, icon_size, 0, nullptr,
                 DI_NORMAL);
  }
  canvas.ResetClip();
  Pen scene_border(Color(120, 255, 255, 255), 1.0f);
  canvas.DrawPath(&scene_border, &scene_path);
}

void StatusIconSettingsDialog::Localize() {
  ::SetWindowTextW(m_hWnd, LocalText(L"小狼毫 - 状态图标", L"小狼毫 - 狀態圖示",
                                     L"Weasel - Status icons")
                               .c_str());
  const std::pair<UINT, std::wstring> labels[] = {
      {IDC_STATUS_TITLE, LocalText(L"状态图标", L"狀態圖示", L"Status icons")},
      {IDC_STATUS_RESTORE,
       LocalText(L"恢复默认", L"還原預設", L"Restore defaults")},
      {IDC_STATUS_BASE_TITLE, LocalText(L"基础图标 · 必需", L"基礎圖示 · 必需",
                                        L"Base icons · Required")},
      {IDC_STATUS_CHINESE_LABEL, LocalText(L"中文", L"中文", L"Chinese")},
      {IDC_STATUS_ENGLISH_LABEL, LocalText(L"英文", L"英文", L"English")},
      {IDC_STATUS_CHINESE_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_ENGLISH_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_CAPS_TITLE,
       LocalText(L"大写锁定图标", L"大寫鎖定圖示", L"Caps Lock icons")},
      {IDC_STATUS_CAPS_AUTOMATIC,
       LocalText(L"自动角标", L"自動角標", L"Automatic badge")},
      {IDC_STATUS_CAPS_CUSTOM,
       LocalText(L"自定义图标", L"自訂圖示", L"Custom icons")},
      {IDC_STATUS_BADGE_LETTER,
       LocalText(L"大写字母 A", L"大寫字母 A", L"Letter A")},
      {IDC_STATUS_BADGE_DOT, LocalText(L"状态点", L"狀態點", L"Status dot")},
      {IDC_STATUS_AUTO_CHINESE_LABEL,
       LocalText(L"中文 · 大写锁定", L"中文 · 大寫鎖定",
                 L"Chinese · Caps Lock")},
      {IDC_STATUS_AUTO_ENGLISH_LABEL,
       LocalText(L"英文 · 大写锁定", L"英文 · 大寫鎖定",
                 L"English · Caps Lock")},
      {IDC_STATUS_CHINESE_CAPS_LABEL,
       LocalText(L"中文 · 大写锁定", L"中文 · 大寫鎖定",
                 L"Chinese · Caps Lock")},
      {IDC_STATUS_ENGLISH_CAPS_LABEL,
       LocalText(L"英文 · 大写锁定", L"英文 · 大寫鎖定",
                 L"English · Caps Lock")},
      {IDC_STATUS_CHINESE_CAPS_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_ENGLISH_CAPS_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_TASKBAR_TITLE,
       LocalText(L"任务栏预览", L"工作列預覽", L"Taskbar preview")},
      {IDC_STATUS_PREVIEW_CHINESE, LocalText(L"中文", L"中文", L"Chinese")},
      {IDC_STATUS_PREVIEW_ENGLISH, LocalText(L"英文", L"英文", L"English")},
      {IDC_STATUS_PREVIEW_CHINESE_CAPS,
       LocalText(L"中文大写", L"中文大寫", L"Chinese Caps")},
      {IDC_STATUS_PREVIEW_ENGLISH_CAPS,
       LocalText(L"英文大写", L"英文大寫", L"English Caps")},
      {IDC_STATUS_APPLY, LocalText(L"应用", L"套用", L"Apply")},
      {IDCANCEL, LocalText(L"关闭", L"關閉", L"Close")},
  };
  for (const auto& [id, text] : labels)
    ::SetDlgItemTextW(m_hWnd, id, text.c_str());
}

void StatusIconSettingsDialog::RefreshMode() {
  draft_.caps_mode = IsDlgButtonChecked(IDC_STATUS_CAPS_CUSTOM) == BST_CHECKED
                         ? weasel::StatusIconCapsMode::Custom
                         : weasel::StatusIconCapsMode::Automatic;
  const bool automatic =
      draft_.caps_mode == weasel::StatusIconCapsMode::Automatic;
  for (UINT id :
       {IDC_STATUS_BADGE_LETTER, IDC_STATUS_BADGE_DOT,
        IDC_STATUS_AUTO_CHINESE_ICON, IDC_STATUS_AUTO_CHINESE_LABEL,
        IDC_STATUS_AUTO_ENGLISH_ICON, IDC_STATUS_AUTO_ENGLISH_LABEL}) {
    CWindow(GetDlgItem(id)).ShowWindow(automatic ? SW_SHOW : SW_HIDE);
  }
  for (UINT id :
       {IDC_STATUS_CHINESE_CAPS_LABEL, IDC_STATUS_CHINESE_CAPS_ICON,
        IDC_STATUS_CHINESE_CAPS_CHANGE, IDC_STATUS_ENGLISH_CAPS_LABEL,
        IDC_STATUS_ENGLISH_CAPS_ICON, IDC_STATUS_ENGLISH_CAPS_CHANGE}) {
    CWindow(GetDlgItem(id)).ShowWindow(automatic ? SW_HIDE : SW_SHOW);
  }
  RefreshPreviews();
  RefreshApplyState();
}

HICON StatusIconSettingsDialog::ResolveIcon(const std::wstring& custom,
                                            bool english,
                                            bool caps) const {
  HICON icon = LoadIconFile(custom);
  if (!icon)
    icon = LoadBuiltInIcon(english);
  if (!caps)
    return icon;
  HICON badged = AddBadge(icon, draft_.caps_badge);
  if (icon)
    ::DestroyIcon(icon);
  return badged;
}

void StatusIconSettingsDialog::SetPreviewIcon(UINT control, HICON icon) {
  const size_t index = [control]() -> size_t {
    switch (control) {
      case IDC_STATUS_CHINESE_ICON:
        return 0;
      case IDC_STATUS_ENGLISH_ICON:
        return 1;
      case IDC_STATUS_AUTO_CHINESE_ICON:
        return 2;
      case IDC_STATUS_AUTO_ENGLISH_ICON:
        return 3;
      case IDC_STATUS_CHINESE_CAPS_ICON:
        return 4;
      case IDC_STATUS_ENGLISH_CAPS_ICON:
        return 5;
      default:
        return 6;
    }
  }();
  SendDlgItemMessage(control, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
  if (preview_icons_[index])
    ::DestroyIcon(preview_icons_[index]);
  preview_icons_[index] = icon;
}

void StatusIconSettingsDialog::RefreshPreviews() {
  SetPreviewIcon(IDC_STATUS_CHINESE_ICON,
                 ResolveIcon(draft_.chinese, false, false));
  SetPreviewIcon(IDC_STATUS_ENGLISH_ICON,
                 ResolveIcon(draft_.english, true, false));
  SetPreviewIcon(IDC_STATUS_AUTO_CHINESE_ICON,
                 ResolveIcon(draft_.chinese, false, true));
  SetPreviewIcon(IDC_STATUS_AUTO_ENGLISH_ICON,
                 ResolveIcon(draft_.english, true, true));
  SetPreviewIcon(IDC_STATUS_CHINESE_CAPS_ICON,
                 ResolveIcon(draft_.chinese_caps, false, false));
  SetPreviewIcon(IDC_STATUS_ENGLISH_CAPS_ICON,
                 ResolveIcon(draft_.english_caps, true, false));

  HICON taskbar = nullptr;
  switch (preview_mode_) {
    case PreviewMode::Chinese:
      taskbar = ResolveIcon(draft_.chinese, false, false);
      break;
    case PreviewMode::English:
      taskbar = ResolveIcon(draft_.english, true, false);
      break;
    case PreviewMode::ChineseCaps:
      taskbar = draft_.caps_mode == weasel::StatusIconCapsMode::Custom
                    ? LoadIconFile(draft_.chinese_caps)
                    : nullptr;
      if (!taskbar)
        taskbar = ResolveIcon(draft_.chinese, false, true);
      break;
    case PreviewMode::EnglishCaps:
      taskbar = draft_.caps_mode == weasel::StatusIconCapsMode::Custom
                    ? LoadIconFile(draft_.english_caps)
                    : nullptr;
      if (!taskbar)
        taskbar = ResolveIcon(draft_.english, true, true);
      break;
  }
  SetPreviewIcon(IDC_STATUS_TASKBAR_ICON, taskbar);
  ::InvalidateRect(GetDlgItem(IDC_STATUS_TASKBAR_CARD), nullptr, FALSE);
}

void StatusIconSettingsDialog::RefreshApplyState() {
  const bool pending = draft_ != initial_;
  CWindow(GetDlgItem(IDC_STATUS_APPLY)).EnableWindow(pending);
  ::SetDlgItemTextW(
      m_hWnd, IDC_STATUS_MESSAGE,
      (draft_ != initial_ ? LocalText(L"有设置等待应用", L"有設定等待套用",
                                      L"Settings are ready to apply")
                          : L"")
          .c_str());
  settings_navigation::SetStatus(
      m_hWnd, pending ? LocalText(L"有更改待应用", L"有變更待套用",
                                  L"Changes ready to apply")
                      : LocalText(L"所有设置已应用", L"所有設定已套用",
                                  L"All settings applied"));
}

bool StatusIconSettingsDialog::ChooseIconFile(std::wstring* path) {
  wchar_t file[MAX_PATH] = {};
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = m_hWnd;
  dialog.lpstrFilter =
      IsChinese() ? L"PNG / ICO 图标 (*.png;*.ico)\0*.png;*.ico\0所有文件 "
                    L"(*.*)\0*.*\0"
                  : L"PNG / ICO icons (*.png;*.ico)\0*.png;*.ico\0All files "
                    L"(*.*)\0*.*\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!::GetOpenFileNameW(&dialog))
    return false;
  if (!IsSupportedIcon(file)) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"请选择可以正常读取的 PNG 或 ICO 图标。",
                  L"請選擇可以正常讀取的 PNG 或 ICO 圖示。",
                  L"Choose a readable PNG or ICO icon.")
            .c_str(),
        LocalText(L"无法使用图标", L"無法使用圖示", L"Invalid icon").c_str(),
        MB_OK | MB_ICONERROR);
    return false;
  }
  HICON validation = LoadIconFile(file);
  if (!validation) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"请选择可以正常读取的 PNG 或 ICO 图标。",
                  L"請選擇可以正常讀取的 PNG 或 ICO 圖示。",
                  L"Choose a readable PNG or ICO icon.")
            .c_str(),
        LocalText(L"无法使用图标", L"無法使用圖示", L"Invalid icon").c_str(),
        MB_OK | MB_ICONERROR);
    return false;
  }
  ::DestroyIcon(validation);
  *path = file;
  return true;
}

LRESULT StatusIconSettingsDialog::OnChooseIcon(WORD, WORD id, HWND, BOOL&) {
  std::wstring* target = nullptr;
  switch (id) {
    case IDC_STATUS_CHINESE_CHANGE:
      target = &draft_.chinese;
      break;
    case IDC_STATUS_ENGLISH_CHANGE:
      target = &draft_.english;
      break;
    case IDC_STATUS_CHINESE_CAPS_CHANGE:
      target = &draft_.chinese_caps;
      break;
    case IDC_STATUS_ENGLISH_CAPS_CHANGE:
      target = &draft_.english_caps;
      break;
  }
  if (target && ChooseIconFile(target)) {
    RefreshPreviews();
    RefreshApplyState();
  }
  return 0;
}

LRESULT StatusIconSettingsDialog::OnCapsModeChanged(WORD, WORD, HWND, BOOL&) {
  RefreshMode();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnBadgeChanged(WORD, WORD, HWND, BOOL&) {
  draft_.caps_badge = IsDlgButtonChecked(IDC_STATUS_BADGE_DOT) == BST_CHECKED
                          ? weasel::StatusIconCapsBadge::Dot
                          : weasel::StatusIconCapsBadge::LetterA;
  RefreshPreviews();
  RefreshApplyState();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnPreviewModeChanged(WORD,
                                                       WORD id,
                                                       HWND,
                                                       BOOL&) {
  preview_mode_ = static_cast<PreviewMode>(id - IDC_STATUS_PREVIEW_CHINESE);
  RefreshPreviews();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnRestore(WORD, WORD, HWND, BOOL&) {
  draft_ = {};
  CheckRadioButton(IDC_STATUS_CAPS_AUTOMATIC, IDC_STATUS_CAPS_CUSTOM,
                   IDC_STATUS_CAPS_AUTOMATIC);
  CheckRadioButton(IDC_STATUS_BADGE_LETTER, IDC_STATUS_BADGE_DOT,
                   IDC_STATUS_BADGE_LETTER);
  RefreshMode();
  return 0;
}

std::wstring StatusIconSettingsDialog::ImportIcon(const std::wstring& source,
                                                  const wchar_t* name,
                                                  std::wstring* error) const {
  if (source.empty())
    return {};
  const std::filesystem::path source_path(source);
  const std::filesystem::path managed =
      WeaselUserDataPath() / L"icons" / L"status";
  std::error_code code;
  if (source_path.parent_path() == managed)
    return source;
  std::filesystem::create_directories(managed, code);
  if (code) {
    *error = LocalText(L"无法创建状态图标文件夹。", L"無法建立狀態圖示資料夾。",
                       L"Could not create the status icon folder.");
    return {};
  }
  const std::filesystem::path destination =
      managed / (std::wstring(name) + source_path.extension().wstring());
  std::filesystem::copy_file(source_path, destination,
                             std::filesystem::copy_options::overwrite_existing,
                             code);
  if (code) {
    *error = LocalText(L"无法复制所选图标。", L"無法複製所選圖示。",
                       L"Could not copy the selected icon.");
    return {};
  }
  return destination.wstring();
}

bool StatusIconSettingsDialog::Persist(std::wstring* error) {
  weasel::StatusIconSettings saved = draft_;
  const struct {
    const std::wstring* source;
    std::wstring* destination;
    const wchar_t* name;
  } imports[] = {
      {&draft_.chinese, &saved.chinese, L"chinese"},
      {&draft_.english, &saved.english, L"english"},
      {&draft_.chinese_caps, &saved.chinese_caps, L"chinese-caps"},
      {&draft_.english_caps, &saved.english_caps, L"english-caps"},
  };
  for (const auto& import : imports) {
    *import.destination = ImportIcon(*import.source, import.name, error);
    if (!import.source->empty() && import.destination->empty())
      return false;
  }
  if (saved.Save() != ERROR_SUCCESS) {
    *error = LocalText(L"无法保存状态图标设置。", L"無法儲存狀態圖示設定。",
                       L"Could not save the status icon settings.");
    return false;
  }
  draft_ = saved;
  initial_ = saved;
  weasel::NotifyUserSettingsChanged();
  return true;
}

LRESULT StatusIconSettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  std::wstring error;
  if (!Persist(&error)) {
    ::MessageBoxW(m_hWnd, error.c_str(),
                  LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
                  MB_OK | MB_ICONERROR);
    return 0;
  }
  RefreshApplyState();
  return 0;
}

bool StatusIconSettingsDialog::ConfirmDiscard() {
  if (draft_ == initial_)
    return true;
  const int result = ::MessageBoxW(
      m_hWnd,
      LocalText(L"状态图标设置尚未应用。是否放弃这些更改？",
                L"狀態圖示設定尚未套用。是否放棄這些變更？",
                L"Status icon changes have not been applied. Discard them?")
          .c_str(),
      LocalText(L"未应用的设置", L"未套用的設定", L"Unapplied settings")
          .c_str(),
      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
  return result == IDYES;
}

LRESULT StatusIconSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (ConfirmDiscard() && !settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT StatusIconSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (ConfirmDiscard() && !settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT StatusIconSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::StatusIcons)
    return 0;
  if (!ConfirmDiscard())
    return 0;
  if (settings_navigation::RequestNavigate(m_hWnd, id))
    return 0;
  EndDialog(id);
  return 0;
}
