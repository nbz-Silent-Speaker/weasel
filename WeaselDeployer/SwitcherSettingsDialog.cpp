#include "stdafx.h"
#include "SwitcherSettingsDialog.h"

#include "Configurator.h"
#include "WanxiangUpdateManager.h"
#include "WanxiangSchemeManager.h"
#include "WeaselDeployer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <thread>

#include <rime_levers_api.h>
#include <WeaselUtility.h>

namespace {
constexpr UINT_PTR kModelTimer = 1;
constexpr int kUpdateSmart =
    static_cast<int>(WanxiangUpdateManager::Frequency::Smart);
constexpr int kUpdateDaily =
    static_cast<int>(WanxiangUpdateManager::Frequency::Daily);
constexpr int kUpdateWeekly =
    static_cast<int>(WanxiangUpdateManager::Frequency::Weekly);
constexpr int kUpdateMonthly =
    static_cast<int>(WanxiangUpdateManager::Frequency::Monthly);
constexpr int kUpdateDisabled =
    static_cast<int>(WanxiangUpdateManager::Frequency::Disabled);

std::wstring CollectDeploymentErrors(
    const std::filesystem::file_time_type& started) {
  std::set<std::wstring> unique;
  std::wstring details;
  std::error_code error;
  const auto log_directory = WeaselLogPath();
  if (!std::filesystem::is_directory(log_directory, error))
    return {};
  for (const auto& entry :
       std::filesystem::directory_iterator(log_directory, error)) {
    if (error || !entry.is_regular_file())
      continue;
    const auto name = entry.path().filename().wstring();
    if (name.find(L".log.WARNING.") == std::wstring::npos &&
        name.find(L".log.ERROR.") == std::wstring::npos)
      continue;
    if (entry.last_write_time(error) + std::chrono::seconds(2) < started) {
      error.clear();
      continue;
    }
    std::ifstream input(entry.path(), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty() || line.front() != 'E')
        continue;
      const auto marker = line.find("] ");
      const std::wstring message =
          u8tow(marker == std::string::npos ? line : line.substr(marker + 2));
      if (!unique.insert(message).second)
        continue;
      if (!details.empty())
        details += L"\n";
      details += L"• " + message;
      if (unique.size() >= 8)
        return details;
    }
  }
  return details;
}

struct InputMode {
  const wchar_t* value;
  const wchar_t* simplified;
  const wchar_t* traditional;
  const wchar_t* english;
};

constexpr InputMode kInputModes[] = {
    {L"全拼", L"全拼", L"全拼", L"Full Pinyin"},
    {L"小鹤双拼", L"小鹤双拼", L"小鶴雙拼", L"Flypy"},
    {L"自然码", L"自然码", L"自然碼", L"Ziranma"},
    {L"微软双拼", L"微软双拼", L"微軟雙拼", L"Microsoft Shuangpin"},
    {L"搜狗双拼", L"搜狗双拼", L"搜狗雙拼", L"Sogou Shuangpin"},
    {L"智能ABC", L"智能 ABC", L"智能 ABC", L"Intelligent ABC"},
    {L"紫光双拼", L"紫光双拼", L"紫光雙拼", L"Ziguang Shuangpin"},
    {L"拼音加加", L"拼音加加", L"拼音加加", L"Pinyin Jiajia"},
    {L"国标双拼", L"国标双拼", L"國標雙拼", L"GB Shuangpin"},
    {L"乱序17", L"乱序 17", L"亂序 17", L"Luanxu 17"},
    {L"蓝天双拼", L"蓝天双拼", L"藍天雙拼", L"Lantian Shuangpin"},
    {L"自然龙", L"自然龙", L"自然龍", L"Ziranlong"},
    {L"汉心龙", L"汉心龙", L"漢心龍", L"Hanxinlong"},
    {L"首道双拼", L"首道双拼", L"首道雙拼", L"Shoudao Shuangpin"},
    {L"大牛双拼", L"大牛双拼", L"大牛雙拼", L"Daniu Shuangpin"},
};

struct ModeFile {
  const wchar_t* name;
  const char* expression;
};

constexpr ModeFile kModeFiles[] = {
    {L"wanxiang_lite.custom.yaml",
     "((?:^|\\n)[ \\t]*-[ \\t]*wanxiang_algebra:/lite/)[^\\s#]+"},
    {L"wanxiang_mixedcode.custom.yaml",
     "((?:^|\\n)[ \\t]*__patch:[ \\t]*wanxiang_algebra:/mixed/)"
     "[^\\s#]+"},
    {L"wanxiang_reverse.custom.yaml",
     "((?:^|\\n)[ \\t]*__include:[ \\t]*wanxiang_algebra:/reverse/)"
     "[^\\s#]+"},
    {L"wanxiang_english.custom.yaml",
     "((?:^|\\n)[ \\t]*__patch:[ \\t]*wanxiang_algebra:/english/)"
     "[^\\s#]+"},
};

bool ReadFile(const std::filesystem::path& path, std::string* text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text->assign(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
  return !input.bad();
}

bool WriteFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), text.size());
  output.flush();
  return output.good();
}

std::wstring EscapeLinkText(const std::wstring& text) {
  std::wstring escaped;
  for (const auto character : text) {
    if (character == L'&')
      escaped += L"&amp;";
    else if (character == L'<')
      escaped += L"&lt;";
    else if (character == L'>')
      escaped += L"&gt;";
    else
      escaped += character;
  }
  return escaped;
}

std::wstring Localize(const wchar_t* simplified,
                      const wchar_t* traditional,
                      const wchar_t* english) {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE)
    return english;
  const WORD sublanguage = SUBLANGID(language);
  return sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
                 sublanguage == SUBLANG_CHINESE_SINGAPORE
             ? simplified
             : traditional;
}

void LayoutInputPage(HWND dialog) {
  using settings_navigation::MoveControl;
  MoveControl(dialog, IDC_SCHEMA_LAST_CHECK, 280, 262, 176, 10);
  MoveControl(dialog, IDC_CHECK_SCHEME_UPDATES, 462, 258, 64,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_SCHEMA_LIST_LABEL, settings_navigation::kPageInsetDlu,
              10, 170, 10);
  MoveControl(dialog, IDC_SCHEMA_DETAIL_LABEL, 196, 10, 330, 10);
  MoveControl(dialog, IDC_SCHEMA_LIST_PANEL, settings_navigation::kPageInsetDlu,
              24, 170, settings_navigation::kPageCardsBottomDlu - 24);
  MoveControl(dialog, IDC_SCHEMA_DETAIL_GROUP, 196, 24, 330,
              settings_navigation::kPageCardsBottomDlu - 24);
  MoveControl(dialog, IDC_SCHEMA_LIST, 16, 26, 166, 200);
  MoveControl(dialog, IDC_SCHEMA_DETAIL_NAME, 210, 36, 142, 12);
  MoveControl(dialog, IDC_SCHEMA_DETAIL_VERSION, 354, 36, 58, 12);
  MoveControl(dialog, IDC_INPUT_MODE, 432, 32,
              settings_navigation::kComboWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_SCHEMA_DESCRIPTION, 210, 52, 302, 66);
  MoveControl(dialog, IDC_MODEL_GROUP, 210, 126, 302, 1);
  MoveControl(dialog, IDC_MODEL_NAME, 210, 137, 150, 12);
  MoveControl(dialog, IDC_MODEL_SECONDARY, 348, 134,
              settings_navigation::kSecondaryButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_MODEL_DOWNLOAD, 432, 134,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_MODEL_DESCRIPTION, 210, 157, 302, 10);
  MoveControl(dialog, IDC_MODEL_SOURCE, 355, 157, 157, 10);
  MoveControl(dialog, IDC_MODEL_NOTE, 210, 170, 302, 10);
  MoveControl(dialog, IDC_MODEL_DOWNLOAD_STATUS, 210, 185, 302, 10);
  MoveControl(dialog, IDC_MODEL_PROGRESS, 210, 202, 124, 6);
  MoveControl(dialog, IDC_MODEL_PROGRESS_TEXT, 336, 199, 28, 12);
  MoveControl(dialog, IDC_SCHEMA_UPDATE_SETTINGS, 432, 230,
              settings_navigation::kComboWidthDlu,
              settings_navigation::kButtonHeightDlu);
}

struct UpdateSelection {
  bool scheme = false;
  bool model = false;
};

class PackageUpdateDialog : public CDialogImpl<PackageUpdateDialog> {
 public:
  enum { IDD = IDD_PACKAGE_UPDATES };

  explicit PackageUpdateDialog(const WanxiangUpdateManager::Result& result)
      : result_(result) {}

  BEGIN_MSG_MAP(PackageUpdateDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnThemeColor)
  MESSAGE_HANDLER(WM_CTLCOLORDLG, OnThemeColor)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnThemeColor)
  MESSAGE_HANDLER(WM_CTLCOLORBTN, OnThemeColor)
  COMMAND_HANDLER(IDC_UPDATE_SCHEME, BN_CLICKED, OnSelectionChanged)
  COMMAND_HANDLER(IDC_UPDATE_MODEL, BN_CLICKED, OnSelectionChanged)
  COMMAND_ID_HANDLER(IDOK, OnOK)
  COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  END_MSG_MAP()

  const UpdateSelection& selection() const { return selection_; }

 private:
  void MoveControl(int id, int left, int top, int width, int height) {
    RECT rectangle = {left, top, left + width, top + height};
    ::MapDialogRect(m_hWnd, &rectangle);
    ::SetWindowPos(GetDlgItem(id), nullptr, rectangle.left, rectangle.top,
                   rectangle.right - rectangle.left,
                   rectangle.bottom - rectangle.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void ResizeClient(int width, int height) {
    RECT window = {};
    RECT client = {};
    RECT desired = {0, 0, width, height};
    ::GetWindowRect(m_hWnd, &window);
    ::GetClientRect(m_hWnd, &client);
    ::MapDialogRect(m_hWnd, &desired);
    ::SetWindowPos(
        m_hWnd, nullptr, 0, 0,
        desired.right + (window.right - window.left) - client.right,
        desired.bottom + (window.bottom - window.top) - client.bottom,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  LRESULT OnThemeColor(UINT message, WPARAM w, LPARAM, BOOL&) {
    if (message == WM_ERASEBKGND) {
      RECT bounds{};
      ::GetClientRect(m_hWnd, &bounds);
      ::FillRect(reinterpret_cast<HDC>(w), &bounds,
                 settings_theme::GetBrush(COLOR_BTNFACE));
      return 1;
    }
    HDC dc = reinterpret_cast<HDC>(w);
    ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
    ::SetBkColor(dc, settings_theme::GetColor(COLOR_BTNFACE));
    ::SetBkMode(dc, TRANSPARENT);
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_BTNFACE));
  }

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    HWND scheme = GetDlgItem(IDC_UPDATE_SCHEME);
    HWND model = GetDlgItem(IDC_UPDATE_MODEL);
    ::ShowWindow(GetDlgItem(IDC_UPDATE_SCHEME_GROUP),
                 result_.scheme_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(scheme, result_.scheme_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_UPDATE_MODEL_GROUP),
                 result_.model_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(model, result_.model_update_available ? SW_SHOW : SW_HIDE);
    if (result_.scheme_update_available) {
      wchar_t label[192] = {};
      swprintf_s(label,
                 Localize(L"万象拼音 Lite · %s → %s · %s",
                          L"萬象拼音 Lite · %s → %s · %s",
                          L"Wanxiang Lite · %s → %s · %s")
                     .c_str(),
                 result_.installed_scheme_version.c_str(),
                 result_.latest_tag.c_str(),
                 result_.latest_scheme_from_cnb
                     ? Localize(L"CNB 国内源", L"CNB 國內來源", L"CNB").c_str()
                     : L"GitHub");
      ::SetWindowTextW(scheme, label);
      ::SendMessageW(scheme, BM_SETCHECK, BST_CHECKED, 0);
    }
    if (!result_.scheme_update_available && result_.model_update_available) {
      MoveControl(IDC_UPDATE_MODEL_GROUP, 14, 28, 372, 36);
      MoveControl(IDC_UPDATE_MODEL, 24, 40, 350, 16);
      MoveControl(IDOK, 218, 72, 92, 18);
      MoveControl(IDCANCEL, 316, 72, 72, 18);
      ::ShowWindow(GetDlgItem(IDC_UPDATE_SUMMARY), SW_HIDE);
      ResizeClient(400, 102);
    } else if (result_.scheme_update_available &&
               !result_.model_update_available) {
      MoveControl(IDC_UPDATE_SUMMARY, 18, 70, 364, 20);
      MoveControl(IDOK, 218, 96, 92, 18);
      MoveControl(IDCANCEL, 316, 96, 72, 18);
      ResizeClient(400, 126);
    }
    if (result_.model_update_available) {
      wchar_t size[128] = {};
      swprintf_s(size,
                 Localize(L"万象简体 LTS 语法模型 · %.1f MB · CNB",
                          L"萬象簡體 LTS 語法模型 · %.1f MB · CNB",
                          L"Wanxiang Simplified LTS grammar model · %.1f MB · "
                          L"CNB")
                     .c_str(),
                 result_.latest_model_size / 1000000.0);
      ::SetWindowTextW(model, size);
      ::SendMessageW(model, BM_SETCHECK, BST_CHECKED, 0);
    }
    if (result_.scheme_update_available) {
      ::SetDlgItemTextW(m_hWnd, IDC_UPDATE_SUMMARY,
                        Localize(L"输入方案更新会保留用户自定义文件，安装前自动"
                                 L"备份，完成后自动重新部署。",
                                 L"輸入方案更新會保留使用者自訂檔案，安裝前自動"
                                 L"備份，完成後自動重新部署。",
                                 L"Schema updates preserve user files, create "
                                 L"a backup, and redeploy automatically.")
                            .c_str());
    } else {
      ::SetDlgItemTextW(m_hWnd, IDC_UPDATE_SUMMARY, L"");
    }
    for (WORD id : {IDC_UPDATE_SCHEME, IDC_UPDATE_MODEL})
      settings_navigation::StyleCheckbox(
          m_hWnd, id, settings_navigation::ToggleState::Background::ButtonFace);
    for (WORD id : {IDC_UPDATE_SCHEME_GROUP, IDC_UPDATE_MODEL_GROUP})
      settings_navigation::StyleGroupBox(m_hWnd, id);
    for (WORD id : {IDOK, IDCANCEL})
      settings_navigation::StyleActionButton(
          m_hWnd, id, settings_navigation::ToggleState::Background::ButtonFace);
    settings_theme::Update(m_hWnd);
    UpdateButton();
    CenterWindow(GetParent());
    return TRUE;
  }

  LRESULT OnSelectionChanged(WORD, WORD, HWND, BOOL&) {
    UpdateButton();
    return 0;
  }

  void UpdateButton() {
    const bool scheme_selected =
        result_.scheme_update_available &&
        ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_SCHEME, BM_GETCHECK, 0, 0) ==
            BST_CHECKED;
    const bool model_selected =
        result_.model_update_available &&
        ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_MODEL, BM_GETCHECK, 0, 0) ==
            BST_CHECKED;
    const int count =
        static_cast<int>(scheme_selected) + static_cast<int>(model_selected);
    ::EnableWindow(GetDlgItem(IDOK), count != 0);
    ::SetWindowTextW(
        GetDlgItem(IDOK),
        (count ? Localize(L"更新所选（", L"更新所選（", L"Update selected (") +
                     std::to_wstring(count) + Localize(L"）", L"）", L")")
               : Localize(L"更新所选", L"更新所選", L"Update selected"))
            .c_str());
  }

  LRESULT OnOK(WORD, WORD, HWND, BOOL&) {
    selection_.scheme = result_.scheme_update_available &&
                        ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_SCHEME,
                                              BM_GETCHECK, 0, 0) == BST_CHECKED;
    selection_.model = ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_MODEL,
                                             BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (selection_.scheme || selection_.model)
      EndDialog(IDOK);
    return 0;
  }

  LRESULT OnCancel(WORD, WORD, HWND, BOOL&) {
    EndDialog(IDCANCEL);
    return 0;
  }

  WanxiangUpdateManager::Result result_;
  UpdateSelection selection_;
};
}  // namespace

SwitcherSettingsDialog::SwitcherSettingsDialog(RimeSwitcherSettings* settings)
    : settings_(settings), loaded_(false), modified_(false) {
  api_ = (RimeLeversApi*)rime_get_api()->find_module("levers")->get_api();
}

SwitcherSettingsDialog::~SwitcherSettingsDialog() {}

std::wstring SwitcherSettingsDialog::LocalText(const wchar_t* simplified,
                                               const wchar_t* traditional,
                                               const wchar_t* english) const {
  return Localize(simplified, traditional, english);
}

bool SwitcherSettingsDialog::LoadInputMode(std::wstring* mode) const {
  const auto user = WeaselUserDataPath() / L"wanxiang_lite.custom.yaml";
  const auto bundled =
      WeaselSharedDataPath() / L"custom" / L"wanxiang_lite.custom.yaml";
  std::string text;
  if (!ReadFile(user, &text) && !ReadFile(bundled, &text)) {
    *mode = L"全拼";
    return true;
  }
  std::smatch match;
  if (!std::regex_search(
          text, match,
          std::regex("(?:^|\\n)[ \\t]*-[ \\t]*wanxiang_algebra:/lite/"
                     "([^\\s#]+)")))
    return false;
  *mode = u8tow(match[1].str());
  return true;
}

bool SwitcherSettingsDialog::SaveInputMode(const std::wstring& mode,
                                           std::wstring* error) const {
  struct PreparedFile {
    std::filesystem::path destination;
    std::filesystem::path temporary;
    std::filesystem::path backup;
    bool had_destination = false;
    bool committed = false;
  };
  std::vector<PreparedFile> prepared;
  const auto discard_prepared = [&]() {
    for (const auto& item : prepared) {
      std::error_code ignored;
      std::filesystem::remove(item.temporary, ignored);
    }
  };
  const auto user = WeaselUserDataPath();
  const auto bundled = WeaselSharedDataPath() / L"custom";
  const auto suffix =
      L".weasel-mode-" + std::to_wstring(::GetCurrentProcessId());
  const auto replacement = wtou8(mode);
  std::error_code file_error;
  std::filesystem::create_directories(user, file_error);
  if (file_error) {
    *error = LocalText(L"无法访问用户文件夹。", L"無法存取使用者資料夾。",
                       L"The user folder is unavailable.");
    return false;
  }

  for (const auto& file : kModeFiles) {
    PreparedFile item;
    item.destination = user / file.name;
    item.temporary = item.destination.wstring() + suffix + L".tmp";
    item.backup = item.destination.wstring() + suffix + L".bak";
    item.had_destination =
        std::filesystem::exists(item.destination, file_error);
    if (file_error) {
      discard_prepared();
      *error = LocalText(L"无法检查现有万象配置。", L"無法檢查現有萬象設定。",
                         L"Cannot inspect the existing Wanxiang settings.");
      return false;
    }
    const auto source =
        item.had_destination ? item.destination : bundled / file.name;
    std::string text;
    if (!ReadFile(source, &text)) {
      discard_prepared();
      *error = LocalText(L"安装包缺少万象拼音方式模板，请重新安装后再试。",
                         L"安裝包缺少萬象拼音方式範本，請重新安裝後再試。",
                         L"The Wanxiang input-mode templates are missing. "
                         L"Reinstall Weasel and try again.");
      return false;
    }
    const std::regex expression(file.expression);
    if (!std::regex_search(text, expression)) {
      discard_prepared();
      *error = LocalText(L"现有万象配置无法识别，未修改用户文件。",
                         L"現有萬象設定無法識別，未修改使用者檔案。",
                         L"The existing Wanxiang settings are not recognized; "
                         L"no user file was changed.");
      return false;
    }
    text = std::regex_replace(text, expression, "$1" + replacement,
                              std::regex_constants::format_first_only);
    if (!WriteFile(item.temporary, text)) {
      std::error_code ignored;
      std::filesystem::remove(item.temporary, ignored);
      discard_prepared();
      *error = LocalText(L"无法在用户文件夹中准备新配置。",
                         L"無法在使用者資料夾中準備新設定。",
                         L"Cannot prepare settings in the user folder.");
      return false;
    }
    prepared.push_back(std::move(item));
  }

  for (auto& item : prepared) {
    if (item.had_destination) {
      std::filesystem::rename(item.destination, item.backup, file_error);
      if (file_error)
        break;
    }
    std::filesystem::rename(item.temporary, item.destination, file_error);
    if (file_error) {
      if (item.had_destination) {
        std::error_code ignored;
        std::filesystem::rename(item.backup, item.destination, ignored);
      }
      break;
    }
    item.committed = true;
  }
  if (file_error) {
    for (auto iterator = prepared.rbegin(); iterator != prepared.rend();
         ++iterator) {
      std::error_code ignored;
      if (iterator->committed) {
        std::filesystem::remove(iterator->destination, ignored);
        if (iterator->had_destination)
          std::filesystem::rename(iterator->backup, iterator->destination,
                                  ignored);
      }
      std::filesystem::remove(iterator->temporary, ignored);
    }
    *error = LocalText(L"保存拼音方式失败，原配置已经恢复。",
                       L"儲存拼音方式失敗，原設定已經恢復。",
                       L"Saving the input mode failed; original settings were "
                       L"restored.");
    return false;
  }
  for (const auto& item : prepared) {
    std::error_code ignored;
    std::filesystem::remove(item.backup, ignored);
  }
  return true;
}

int SwitcherSettingsDialog::LoadUpdateFrequency(
    const std::string& schema_id) const {
  return static_cast<int>(WanxiangUpdateManager::LoadFrequency(schema_id));
}

bool SwitcherSettingsDialog::SaveUpdateFrequency(const std::string& schema_id,
                                                 int frequency) const {
  return WanxiangUpdateManager::SaveFrequency(
      schema_id, static_cast<WanxiangUpdateManager::Frequency>(frequency));
}

std::wstring SwitcherSettingsDialog::UpdateFrequencyText(int frequency) const {
  switch (frequency) {
    case kUpdateSmart:
      return LocalText(L"智能更新", L"智慧更新", L"Smart updates");
    case kUpdateDaily:
      return LocalText(L"每天更新", L"每天更新", L"Daily updates");
    case kUpdateMonthly:
      return LocalText(L"每月更新", L"每月更新", L"Monthly updates");
    case kUpdateDisabled:
      return LocalText(L"关闭更新", L"關閉更新", L"Updates off");
    default:
      return LocalText(L"每周更新", L"每週更新", L"Weekly updates");
  }
}

bool SwitcherSettingsDialog::ConfirmDiscardChanges() {
  if (!HasPendingChanges())
    return true;
  const std::wstring message =
      LocalText(L"存在尚未应用的设置或语法模型更改。关闭并恢复到应用前状态吗？",
                L"存在尚未套用的設定或語法模型變更。關閉並恢復到套用前狀態嗎？",
                L"Some settings or grammar model changes have not been "
                L"applied. Close and "
                L"restore the previous state?");
  return ::MessageBoxW(
             m_hWnd, message.c_str(),
             LocalText(L"放弃更改", L"放棄變更", L"Discard changes").c_str(),
             MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

bool SwitcherSettingsDialog::HasSchemaSelectionChanges() const {
  return std::any_of(schemas_.begin(), schemas_.end(), [](const auto& schema) {
    return schema.enabled != schema.initial_enabled;
  });
}

bool SwitcherSettingsDialog::HasPendingChanges() const {
  return HasSchemaSelectionChanges() || input_mode_modified_ ||
         update_frequency_modified_ ||
         pending_model_action_ != PendingModelAction::None;
}

void SwitcherSettingsDialog::UpdateApplyButton() {
  if (apply_operation_)
    return;
  const bool pending = HasPendingChanges();
  settings_navigation::SetUnappliedChanges(
      m_hWnd, settings_navigation::Page::Input, pending);
  ::EnableWindow(GetDlgItem(IDOK),
                 settings_navigation::HasAnyUnappliedChanges());
}

void SwitcherSettingsDialog::DiscardPendingModel() {
  if (pending_model_action_ == PendingModelAction::None)
    return;
  if (pending_model_action_ == PendingModelAction::Install) {
    std::wstring error;
    if (!model_manager_.Rollback(&error)) {
      LOG(ERROR) << "Unable to roll back an unapplied Wanxiang model: "
                 << wtou8(error);
    }
  }
  pending_model_action_ = PendingModelAction::None;
  model_operation_failed_ = false;
}

bool SwitcherSettingsDialog::RestorePersistedSettings() {
  bool restored = true;
  std::wstring error;
  if (input_mode_modified_ && !SaveInputMode(initial_input_mode_, &error)) {
    LOG(ERROR) << "Unable to restore the previous Wanxiang input mode: "
               << wtou8(error);
    restored = false;
  }
  if (update_frequency_modified_ &&
      !SaveUpdateFrequency("wanxiang_lite", initial_update_frequency_)) {
    LOG(ERROR) << "Unable to restore the previous Wanxiang update frequency.";
    restored = false;
  }
  const bool schema_selection_modified = HasSchemaSelectionChanges();
  if (settings_ && schema_selection_modified) {
    std::vector<const char*> original_selection;
    for (const auto& schema : schemas_) {
      if (schema.initial_enabled)
        original_selection.push_back(schema.id.c_str());
    }
    api_->select_schemas(settings_, original_selection.data(),
                         static_cast<int>(original_selection.size()));
    if (!api_->save_settings(
            reinterpret_cast<RimeCustomSettings*>(settings_))) {
      LOG(ERROR) << "Unable to restore the previous schema selection.";
      restored = false;
    }
  }
  return restored;
}

void SwitcherSettingsDialog::CommitAppliedBaseline() {
  for (auto& schema : schemas_)
    schema.initial_enabled = schema.enabled;
  initial_input_mode_ = selected_input_mode_;
  initial_update_frequency_ = selected_update_frequency_;
}

void SwitcherSettingsDialog::Populate() {
  if (!settings_)
    return;
  loaded_ = false;
  schemas_.clear();

  RimeSchemaList available = {0};
  api_->get_available_schema_list(settings_, &available);
  RimeSchemaList selected = {0};
  api_->get_selected_schema_list(settings_, &selected);

  std::set<std::string> selected_ids;
  for (size_t i = 0; i < selected.size; ++i) {
    if (selected.list[i].schema_id)
      selected_ids.emplace(selected.list[i].schema_id);
  }

  std::set<RimeSchemaInfo*> recruited;
  const auto append_schema = [&](RimeSchemaListItem& item) {
    auto* info = reinterpret_cast<RimeSchemaInfo*>(item.reserved);
    if (!info || recruited.find(info) != recruited.end())
      return;
    recruited.insert(info);
    SchemaEntry entry;
    entry.info = info;
    entry.id = item.schema_id ? item.schema_id : "";
    entry.name = u8tow(item.name ? item.name : entry.id.c_str());
    entry.enabled = selected_ids.find(entry.id) != selected_ids.end();
    entry.initial_enabled = entry.enabled;
    schemas_.push_back(std::move(entry));
  };

  for (size_t i = 0; i < selected.size; ++i) {
    if (!selected.list[i].schema_id)
      continue;
    for (size_t j = 0; j < available.size; ++j) {
      if (available.list[j].schema_id &&
          !strcmp(available.list[j].schema_id, selected.list[i].schema_id)) {
        append_schema(available.list[j]);
        break;
      }
    }
  }
  for (size_t i = 0; i < available.size; ++i)
    append_schema(available.list[i]);

  RebuildList();
  loaded_ = true;
  modified_ = false;
}

void SwitcherSettingsDialog::RebuildList() {
  loaded_ = false;
  schema_list_.DeleteAllItems();
  int row = 0;
  for (size_t i = 0; i < schemas_.size(); ++i) {
    const auto& schema = schemas_[i];
    schema_list_.AddItem(row, 0, schema.name.c_str());
    schema_list_.SetItemData(row, static_cast<DWORD_PTR>(i));
    schema_list_.SetCheckState(row, schema.enabled ? TRUE : FALSE);
    ++row;
  }
  loaded_ = true;

  if (schema_list_.GetItemCount() > 0) {
    schema_list_.SelectItem(0);
    ShowDetails(static_cast<size_t>(schema_list_.GetItemData(0)));
  } else {
    selected_schema_ = static_cast<size_t>(-1);
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_VERSION, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DESCRIPTION, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_PROJECT_LINKS, L"");
    ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_AUTHOR, L"");
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_DETAIL_VERSION), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_PROJECT_LINKS), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_AUTHOR), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_SCHEMA_RESTORE_PACKAGE), SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_INPUT_MODE), SW_HIDE);
    ShowModelControls(false);
  }
}

void SwitcherSettingsDialog::ShowDetails(size_t index) {
  if (index >= schemas_.size())
    return;
  selected_schema_ = index;
  const auto& schema = schemas_[index];
  const bool wanxiang = schema.id == "wanxiang_lite";
  const std::wstring version =
      wanxiang ? WanxiangUpdateManager::LoadInstalledSchemeVersion() : L"";
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, schema.name.c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_VERSION, version.c_str());
  HWND name_control = GetDlgItem(IDC_SCHEMA_DETAIL_NAME);
  HWND version_control = GetDlgItem(IDC_SCHEMA_DETAIL_VERSION);
  CRect name_rect;
  ::GetWindowRect(name_control, &name_rect);
  ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&name_rect),
                    2);
  HDC header_dc = ::GetDC(name_control);
  HFONT header_font =
      reinterpret_cast<HFONT>(::SendMessageW(name_control, WM_GETFONT, 0, 0));
  const HGDIOBJ old_header_font =
      header_font ? ::SelectObject(header_dc, header_font) : nullptr;
  SIZE name_size = {};
  ::GetTextExtentPoint32W(header_dc, schema.name.c_str(),
                          static_cast<int>(schema.name.size()), &name_size);
  SIZE version_size = {};
  ::GetTextExtentPoint32W(header_dc, version.c_str(),
                          static_cast<int>(version.size()), &version_size);
  if (old_header_font)
    ::SelectObject(header_dc, old_header_font);
  ::ReleaseDC(name_control, header_dc);
  RECT spacing = {0, 0, 4, 0};
  ::MapDialogRect(m_hWnd, &spacing);
  const int gap = spacing.right;
  const int version_width = static_cast<int>(version_size.cx);
  const int maximum_name_width = static_cast<int>(
      input_mode_base_rect_.left - name_rect.left - version_width - gap * 2);
  const int name_width = (std::max)(
      1, (std::min)(static_cast<int>(name_size.cx), maximum_name_width));
  ::SetWindowPos(name_control, nullptr, name_rect.left, name_rect.top,
                 name_width, name_rect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
  ::SetWindowPos(version_control, nullptr, name_rect.left + name_width + gap,
                 name_rect.top, version_width, name_rect.Height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  ::ShowWindow(version_control, version.empty() ? SW_HIDE : SW_SHOW);

  std::string details;
  if (const char* description = api_->get_schema_description(schema.info))
    details += description;
  description_.SetWindowTextW(u8tow(details).c_str());
  UpdateDescriptionLayout();

  std::wstring links;
  if (wanxiang) {
    links =
        L"<a href=\"https://github.com/amzxyz/rime-wanxiang\">GitHub</a>"
        L"  ·  <a href=\"https://cnb.cool/amzxyz/rime-wanxiang\">" +
        LocalText(L"CNB 国内源", L"CNB 國內來源", L"CNB mirror") + L"</a>";
  }
  std::wstring author;
  if (const char* value = api_->get_schema_author(schema.info)) {
    author = u8tow(value);
    for (auto& character : author) {
      if (character == L'\r' || character == L'\n' || character == L'\t')
        character = L' ';
    }
    if (!author.empty())
      author = LocalText(L"作者：", L"作者：", L"By: ") + author;
  }
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_PROJECT_LINKS, links.c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_AUTHOR, author.c_str());
  RECT author_rect =
      links.empty() ? RECT{210, 233, 428, 245} : RECT{302, 233, 428, 245};
  ::MapDialogRect(m_hWnd, &author_rect);
  if (::GetDlgItem(m_hWnd, settings_navigation::kInput)) {
    const int navigation_offset = static_cast<int>(
        settings_navigation::MapDialogUnits(
            m_hWnd, 0, 0, settings_navigation::kSidebarWidthDlu, 0)
            .right);
    ::OffsetRect(&author_rect, navigation_offset, 0);
  }
  ::SetWindowPos(GetDlgItem(IDC_SCHEMA_AUTHOR), nullptr, author_rect.left,
                 author_rect.top, author_rect.right - author_rect.left,
                 author_rect.bottom - author_rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_PROJECT_LINKS),
               links.empty() ? SW_HIDE : SW_SHOW);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_AUTHOR),
               author.empty() ? SW_HIDE : SW_SHOW);
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS),
               wanxiang ? SW_SHOW : SW_HIDE);
  if (wanxiang) {
    if (!update_frequency_modified_)
      selected_update_frequency_ = LoadUpdateFrequency(schema.id);
    update_frequency_.SetCurSel(selected_update_frequency_);
  }
  ::ShowWindow(GetDlgItem(IDC_SCHEMA_RESTORE_PACKAGE), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_INPUT_MODE), wanxiang ? SW_SHOW : SW_HIDE);
  if (wanxiang)
    AdjustInputModeWidth();
  ShowModelControls(wanxiang);
  if (wanxiang)
    UpdateModelUi();
  CRect details_rect;
  ::GetWindowRect(GetDlgItem(IDC_SCHEMA_DETAIL_GROUP), &details_rect);
  ::MapWindowPoints(HWND_DESKTOP, m_hWnd,
                    reinterpret_cast<POINT*>(&details_rect), 2);
  RedrawWindow(&details_rect, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void SwitcherSettingsDialog::AdjustInputModeWidth() {
  if (!input_mode_.IsWindow())
    return;
  const int selected = input_mode_.GetCurSel();
  if (selected == CB_ERR)
    return;
  CRect available;
  ::GetWindowRect(GetDlgItem(IDC_SCHEMA_DESCRIPTION), &available);
  ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&available),
                    2);
  HDC dc = ::GetDC(input_mode_);
  HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(input_mode_, WM_GETFONT, 0, 0));
  HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
  const int length = input_mode_.GetLBTextLen(selected);
  std::wstring value(length + 1, L'\0');
  input_mode_.GetLBText(selected, value.data());
  SIZE extent = {};
  ::GetTextExtentPoint32W(dc, value.c_str(), length, &extent);
  if (previous)
    ::SelectObject(dc, previous);
  ::ReleaseDC(input_mode_, dc);
  const int desired =
      static_cast<int>(extent.cx) + ::GetSystemMetrics(SM_CXVSCROLL) + 24;
  const int base_width = static_cast<int>(input_mode_base_rect_.Width());
  CRect version_rect;
  ::GetWindowRect(GetDlgItem(IDC_SCHEMA_DETAIL_VERSION), &version_rect);
  ::MapWindowPoints(HWND_DESKTOP, m_hWnd,
                    reinterpret_cast<POINT*>(&version_rect), 2);
  RECT spacing = {0, 0, 4, 0};
  ::MapDialogRect(m_hWnd, &spacing);
  const int left_limit =
      ::IsWindowVisible(GetDlgItem(IDC_SCHEMA_DETAIL_VERSION))
          ? version_rect.right + spacing.right
          : available.left;
  const int available_width =
      static_cast<int>(input_mode_base_rect_.right - left_limit);
  const int width =
      (std::min)((std::max)(base_width, desired), available_width);
  ::SetWindowPos(input_mode_, nullptr, input_mode_base_rect_.right - width,
                 input_mode_base_rect_.top, width,
                 input_mode_base_rect_.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
  input_mode_.SetDroppedWidth((std::max)(width, desired));
}

bool SwitcherSettingsDialog::HasUnappliedChanges() const {
  return HasPendingChanges();
}

bool SwitcherSettingsDialog::ConfirmClose() {
  return ConfirmDiscardChanges();
}

void SwitcherSettingsDialog::PrepareClose() {
  DiscardPendingModel();
  KillTimer(kModelTimer);
}

void SwitcherSettingsDialog::UpdateDescriptionLayout() {
  if (!description_.IsWindow())
    return;
  ::ShowScrollBar(description_, SB_VERT, FALSE);
  CRect client;
  description_.GetClientRect(&client);
  RECT inset = {4, 4, 4, 4};
  ::MapDialogRect(m_hWnd, &inset);
  RECT format = {client.left + inset.left, client.top + inset.top,
                 client.right - inset.right, client.bottom - inset.bottom};
  ::SendMessageW(description_, EM_SETRECTNP, 0,
                 reinterpret_cast<LPARAM>(&format));

  HDC dc = ::GetDC(description_);
  HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(description_, WM_GETFONT, 0, 0));
  const HGDIOBJ previous = font ? ::SelectObject(dc, font) : nullptr;
  TEXTMETRICW metrics = {};
  ::GetTextMetricsW(dc, &metrics);
  if (previous)
    ::SelectObject(dc, previous);
  ::ReleaseDC(description_, dc);
  const int content_height = description_.GetLineCount() * metrics.tmHeight;
  const bool overflow = content_height > format.bottom - format.top;
  ::ShowScrollBar(description_, SB_VERT, overflow);
  description_.Invalidate();
}

void SwitcherSettingsDialog::ApplyRoundedRegion(HWND control, int radius_dlu) {
  if (!::IsWindow(control))
    return;
  RECT client = {};
  ::GetClientRect(control, &client);
  RECT radius = {0, 0, radius_dlu, radius_dlu};
  ::MapDialogRect(m_hWnd, &radius);
  const int diameter = (std::max)(4, static_cast<int>(radius.right));
  HRGN region = ::CreateRoundRectRgn(client.left, client.top, client.right + 1,
                                     client.bottom + 1, diameter, diameter);
  if (region && !::SetWindowRgn(control, region, TRUE))
    ::DeleteObject(region);
}

void SwitcherSettingsDialog::ApplyControlRounding() {
  for (const int id : {IDOK, IDC_CHECK_SCHEME_UPDATES, IDC_MODEL_DOWNLOAD,
                       IDC_MODEL_SECONDARY}) {
    settings_navigation::StyleActionButton(m_hWnd, static_cast<WORD>(id));
  }
  settings_navigation::StyleInput(m_hWnd, IDC_SCHEMA_DESCRIPTION);
  ApplyRoundedRegion(GetDlgItem(IDC_MODEL_PROGRESS), 3);
}

void SwitcherSettingsDialog::ShowModelControls(bool show) {
  constexpr int controls[] = {
      IDC_MODEL_GROUP,    IDC_MODEL_NAME,          IDC_MODEL_DESCRIPTION,
      IDC_MODEL_NOTE,     IDC_MODEL_DOWNLOAD,      IDC_MODEL_SECONDARY,
      IDC_MODEL_PROGRESS, IDC_MODEL_PROGRESS_TEXT, IDC_MODEL_DOWNLOAD_STATUS,
  };
  for (const int control : controls)
    ::ShowWindow(GetDlgItem(control), show ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_SOURCE), SW_HIDE);
}

std::wstring SwitcherSettingsDialog::ModelErrorText(HRESULT error_code) const {
  std::wstring message;
  if (error_code == HRESULT_FROM_WIN32(ERROR_DISK_FULL))
    return LocalText(L"下载磁盘空间不足，请释放空间后重试。",
                     L"下載磁碟空間不足，請釋放空間後重試。",
                     L"Download disk is full. Free space and retry.");
  if (error_code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
    return LocalText(L"下载缓存没有写入权限，请检查文件夹权限。",
                     L"下載快取沒有寫入權限，請檢查資料夾權限。",
                     L"Cannot write to the cache. Check folder permissions.");
  if (error_code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
      error_code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
    return LocalText(L"下载缓存路径不存在，需要修复或重新下载。",
                     L"下載快取路徑不存在，需要修復或重新下載。",
                     L"Download cache path is missing. Repair or restart.");
  constexpr unsigned int kHttpFacility = 25;
  if (HRESULT_FACILITY(error_code) == kHttpFacility) {
    switch (HRESULT_CODE(error_code)) {
      case 403:
        message = LocalText(L"下载源拒绝了访问。", L"下載來源拒絕了存取。",
                            L"The download source denied access.");
        break;
      case 404:
        message = LocalText(L"下载文件不存在。", L"下載檔案不存在。",
                            L"The download file was not found.");
        break;
      case 408:
        message = LocalText(L"连接下载源超时。", L"連線下載來源逾時。",
                            L"The download source timed out.");
        break;
      case 429:
        message = LocalText(L"下载源请求过多，请稍后继续。",
                            L"下載來源要求過多，請稍後繼續。",
                            L"The download source is busy. Try again later.");
        break;
      default:
        if (HRESULT_CODE(error_code) >= 500)
          message =
              LocalText(L"下载源暂时不可用。", L"下載來源暫時無法使用。",
                        L"The download source is temporarily unavailable.");
        break;
    }
  }
  if (message.empty()) {
    message = LocalText(
        L"下载暂时无法继续，请检查网络后重试。",
        L"下載暫時無法繼續，請檢查網路後重試。",
        L"The download cannot continue. Check the network and retry.");
  }
  return message;
}

bool SwitcherSettingsDialog::ModelUiNeedsRefresh(
    const WanxiangModelManager::Progress& progress) const {
  using State = WanxiangModelManager::State;
  if (!model_ui_snapshot_valid_ || progress.state != model_ui_state_ ||
      progress.total != model_ui_total_ ||
      progress.error_code != model_ui_error_code_ ||
      progress.error_context != model_ui_error_context_) {
    return true;
  }
  // Transfer counters are the only values that should repaint on the timer.
  // Stable installed/not-installed states must remain completely idle.
  return progress.state == State::Downloading &&
         progress.transferred != model_ui_transferred_;
}

void SwitcherSettingsDialog::UpdateModelUi(
    const WanxiangModelManager::Progress* snapshot) {
  const auto progress = snapshot ? *snapshot : model_manager_.GetProgress();
  using State = WanxiangModelManager::State;
  const auto state = progress.state;
  // Only update changed text; avoid flashing default and actual state messages.
  const auto set_text = [&](int id, const std::wstring& text) {
    HWND control = GetDlgItem(id);
    const int length = ::GetWindowTextLengthW(control);
    std::wstring previous(length + 1, L'\0');
    ::GetWindowTextW(control, previous.data(), length + 1);
    previous.resize(length);
    if (previous != text)
      ::SetWindowTextW(control, text.c_str());
  };
  const auto target_size = latest_model_size_
                               ? latest_model_size_
                               : WanxiangModelManager::kExpectedSize;
  wchar_t model_summary[192] = {};
  swprintf_s(model_summary,
             LocalText(L"提升长句和上下文预测 · %.1f MB · CNB 国内下载源",
                       L"提升長句和上下文預測 · %.1f MB · CNB 國內下載來源",
                       L"Improves long phrases and context prediction · "
                       L"%.1f MB · CNB mirror")
                 .c_str(),
             target_size / 1000000.0);
  set_text(IDC_MODEL_DESCRIPTION, model_summary);
  set_text(IDC_MODEL_SOURCE, L"");
  const auto control_is_visible = [](HWND control) {
    return (::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
  };
  const auto show_if_changed = [](HWND control, bool visible) {
    if (((::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) !=
        visible) {
      ::ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
  };
  show_if_changed(GetDlgItem(IDC_MODEL_SOURCE), false);
  const bool active = state == State::Downloading ||
                      state == State::WaitingRetry || state == State::Paused ||
                      state == State::Transferred || state == State::Error;
  model_status_active_ = (active && state != State::Downloading) ||
                         state == State::RestartRequired ||
                         model_operation_failed_ ||
                         pending_model_action_ != PendingModelAction::None ||
                         (state == State::Modified && !model_update_available_);
  show_if_changed(GetDlgItem(IDC_MODEL_PROGRESS), active);
  show_if_changed(GetDlgItem(IDC_MODEL_PROGRESS_TEXT), active);
  if (active) {
    const auto total = progress.total ? progress.total : target_size;
    const int value = static_cast<int>(std::min<unsigned long long>(
        1000, progress.transferred * 1000 / total));
    if (model_progress_.GetPos() != value)
      model_progress_.SetPos(value);
    wchar_t label[16] = {};
    swprintf_s(label, L"%d%%", value / 10);
    if (model_progress_percent_ != label) {
      model_progress_percent_ = label;
      HWND percent = GetDlgItem(IDC_MODEL_PROGRESS_TEXT);
      ::SendMessageW(percent, WM_SETREDRAW, FALSE, 0);
      ::SetWindowTextW(percent, label);
      ::SendMessageW(percent, WM_SETREDRAW, TRUE, 0);
      ::InvalidateRect(percent, nullptr, FALSE);
    }
  } else {
    model_progress_percent_.clear();
  }
  std::wstring note, download_phase, download_speed, download_amount, primary,
      secondary;
  const auto format_download_amount = [&]() {
    const auto total = progress.total ? progress.total : target_size;
    wchar_t amount[64] = {};
    swprintf_s(amount, L"%.1f / %.1f MB", progress.transferred / 1000000.0,
               total / 1000000.0);
    return std::wstring(amount);
  };
  bool enabled = true;
  model_button_accent_ = false;
  if (state != State::Downloading) {
    last_progress_tick_ = 0;
    last_progress_bytes_ = progress.transferred;
  }
  switch (state) {
    case State::Downloading:
      note = LocalText(
          L"下载完成后自动校验并安装；点击“应用”完成重新部署。",
          L"下載完成後自動校驗並安裝；點擊「套用」完成重新部署。",
          L"The download is verified and installed automatically; select "
          L"Apply to redeploy.");
      download_phase = LocalText(L"正在下载", L"正在下載", L"Downloading");
      if (last_progress_tick_ && progress.transferred >= last_progress_bytes_) {
        const ULONGLONG now = ::GetTickCount64();
        const ULONGLONG elapsed = now - last_progress_tick_;
        if (elapsed) {
          wchar_t speed[48] = {};
          swprintf_s(speed, L"%.1f MB/s",
                     (progress.transferred - last_progress_bytes_) * 1000.0 /
                         elapsed / 1000000.0);
          download_speed = speed;
        }
      }
      last_progress_tick_ = ::GetTickCount64();
      last_progress_bytes_ = progress.transferred;
      download_amount = format_download_amount();
      primary = LocalText(L"暂停下载", L"暫停下載", L"Pause");
      break;
    case State::WaitingRetry:
      note = ModelErrorText(progress.error_code) +
             LocalText(L" 将自动重试。", L" 將自動重試。",
                       L" Retrying automatically.");
      download_phase = LocalText(L"等待重试", L"等待重試", L"Waiting to retry");
      download_amount = format_download_amount();
      primary = LocalText(L"立即重试", L"立即重試", L"Retry now");
      break;
    case State::Paused:
      note = LocalText(L"已暂停，可继续下载。", L"已暫停，可繼續下載。",
                       L"Paused. Resume when ready.");
      download_phase = LocalText(L"已暂停", L"已暫停", L"Paused");
      download_amount = format_download_amount();
      primary = LocalText(L"继续下载", L"繼續下載", L"Resume");
      break;
    case State::RestartRequired:
      note = LocalText(L"原下载缓存已丢失，需要重新下载。",
                       L"原下載快取已遺失，需要重新下載。",
                       L"The previous download cache is missing. Start again.");
      primary = LocalText(L"重新下载", L"重新下載", L"Restart");
      break;
    case State::Error:
      note = ModelErrorText(progress.error_code);
      if (progress.error_context == BG_ERROR_CONTEXT_LOCAL_FILE &&
          HRESULT_CODE(progress.error_code) != ERROR_DISK_FULL &&
          HRESULT_CODE(progress.error_code) != ERROR_ACCESS_DENIED) {
        note = LocalText(
            L"无法访问下载缓存，请检查目录和磁盘后重试。",
            L"無法存取下載快取，請檢查目錄和磁碟後重試。",
            L"Cannot access the download cache. Check the folder and disk.");
      }
      download_phase =
          LocalText(L"下载中断", L"下載中斷", L"Download interrupted");
      download_amount = format_download_amount();
      primary = LocalText(L"重试", L"重試", L"Retry");
      break;
    case State::Transferred:
      note = LocalText(L"下载完成，正在校验。", L"下載完成，正在校驗。",
                       L"Verifying the downloaded grammar model.");
      primary = LocalText(L"正在校验", L"正在校驗", L"Verifying");
      enabled = false;
      break;
    case State::Installed:
      secondary =
          LocalText(L"移除语法模型", L"移除語法模型", L"Remove grammar model");
      break;
    case State::Modified:
      note =
          model_update_available_
              ? L""
              : LocalText(L"现有语法模型不是此处管理的版本。",
                          L"現有語法模型不是此處管理的版本。",
                          L"The existing grammar model is not managed here.");
      secondary =
          LocalText(L"移除语法模型", L"移除語法模型", L"Remove grammar model");
      break;
    default:
      note = LocalText(
          L"下载完成后自动校验并安装；点击“应用”完成重新部署。",
          L"下載完成後自動校驗並安裝；點擊「套用」完成重新部署。",
          L"The download is verified and installed automatically; select "
          L"Apply to redeploy.");
      primary = LocalText(L"下载语法模型", L"下載語法模型",
                          L"Download grammar model");
      break;
  }
  if (model_update_available_ &&
      pending_model_action_ == PendingModelAction::None &&
      (state == State::Installed || state == State::Modified)) {
    primary =
        LocalText(L"更新语法模型", L"更新語法模型", L"Update grammar model");
    enabled = true;
    model_button_accent_ = true;
  }
  if (pending_model_action_ == PendingModelAction::Install) {
    note = LocalText(L"已安装 · 待应用", L"已安裝 · 待套用",
                     L"Installed · Apply pending");
    primary.clear();
    secondary.clear();
  } else if (pending_model_action_ == PendingModelAction::Remove) {
    note = LocalText(L"已移除 · 待应用", L"已移除 · 待套用",
                     L"Removed · Apply pending");
    primary.clear();
    secondary = LocalText(L"取消移除", L"取消移除", L"Undo removal");
  }
  if ((active && state != State::Transferred) ||
      state == State::RestartRequired)
    secondary = LocalText(L"取消下载", L"取消下載", L"Cancel download");
  if (model_operation_failed_) {
    if (pending_model_action_ == PendingModelAction::Install) {
      note = LocalText(L"安装未完成，请点击“应用”重试。",
                       L"安裝未完成，請點擊「套用」重試。",
                       L"Installation did not finish. Select Apply to retry.");
      primary.clear();
    } else if (pending_model_action_ == PendingModelAction::Remove) {
      note = LocalText(L"移除未完成，请点击“应用”重试。",
                       L"移除未完成，請點擊「套用」重試。",
                       L"Removal did not finish. Select Apply to retry.");
    } else {
      note = LocalText(
          L"安装未完成。检查用户文件夹后重试，已校验的下载会复用。",
          L"安裝未完成。檢查使用者資料夾後重試，已校驗的下載會重用。",
          L"Installation failed. Check the user folder and retry; verified "
          L"data is reused.");
      primary = LocalText(L"重试安装", L"重試安裝", L"Retry install");
      enabled = true;
    }
  }
  if ((state == State::Installed || state == State::Modified) && note.empty()) {
    SYSTEMTIME installed = {};
    if (WanxiangModelManager::LoadLastInstalledTime(&installed)) {
      wchar_t date[64] = {};
      swprintf_s(
          date,
          LocalText(L"上次更新：%04u-%02u-%02u", L"上次更新：%04u-%02u-%02u",
                    L"Last updated: %04u-%02u-%02u")
              .c_str(),
          installed.wYear, installed.wMonth, installed.wDay);
      note = date;
    }
  }
  set_text(IDC_MODEL_NOTE, note);
  std::wstring download_status = download_phase;
  if (!download_speed.empty())
    download_status += L" · " + download_speed;
  if (!download_amount.empty())
    download_status += L" · " + download_amount;
  model_download_phase_ = std::move(download_phase);
  model_download_speed_ = std::move(download_speed);
  model_download_amount_ = std::move(download_amount);
  set_text(IDC_MODEL_DOWNLOAD_STATUS, download_status);
  show_if_changed(GetDlgItem(IDC_MODEL_DOWNLOAD_STATUS),
                  !download_status.empty());
  const CRect& primary_rect =
      active ? model_active_primary_rect_ : model_primary_rect_;
  const bool primary_visible = !primary.empty();
  const bool use_primary_slot = primary.empty() && (state == State::Installed ||
                                                    state == State::Modified);
  const CRect& secondary_rect = active ? model_active_secondary_rect_
                                : use_primary_slot ? model_primary_rect_
                                                   : model_secondary_rect_;
  const bool secondary_visible = !secondary.empty();
  const auto placement_changed = [&](HWND control, const CRect& target,
                                     bool visible) {
    CRect current;
    ::GetWindowRect(control, &current);
    ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&current),
                      2);
    return current != target || control_is_visible(control) != visible;
  };
  HWND primary_button = GetDlgItem(IDC_MODEL_DOWNLOAD);
  HWND secondary_button = GetDlgItem(IDC_MODEL_SECONDARY);
  const bool relayout =
      placement_changed(primary_button, primary_rect, primary_visible) ||
      placement_changed(secondary_button, secondary_rect, secondary_visible);
  if (relayout)
    ::SendMessageW(m_hWnd, WM_SETREDRAW, FALSE, 0);
  const auto place_control = [&](HWND control, WORD id, const CRect& target,
                                 const std::wstring& label, bool visible,
                                 bool control_enabled) {
    CRect current;
    ::GetWindowRect(control, &current);
    ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&current),
                      2);
    const bool moved = current != target;
    const bool visibility_changed = control_is_visible(control) != visible;
    if (moved || visibility_changed)
      ::ShowWindow(control, SW_HIDE);
    if (moved) {
      // Rounded child windows can copy their old edge pixels when moved.  Drop
      // those pixels and rebuild the region at the destination instead.
      ::SetWindowRgn(control, nullptr, FALSE);
      ::SetWindowPos(
          control, nullptr, target.left, target.top, target.Width(),
          target.Height(),
          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
      settings_navigation::StyleActionButton(m_hWnd, id);
    }
    set_text(id, label);
    if ((::IsWindowEnabled(control) != FALSE) != control_enabled)
      ::EnableWindow(control, control_enabled);
    if (visible)
      ::ShowWindow(control, SW_SHOWNA);
    if (visible && (moved || visibility_changed)) {
      ::RedrawWindow(control, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    }
  };
  place_control(primary_button, IDC_MODEL_DOWNLOAD, primary_rect, primary,
                primary_visible, enabled);
  place_control(secondary_button, IDC_MODEL_SECONDARY, secondary_rect,
                secondary, secondary_visible, true);
  if (relayout) {
    ::SendMessageW(m_hWnd, WM_SETREDRAW, TRUE, 0);
    ::RedrawWindow(m_hWnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
                       RDW_UPDATENOW);
  }
  model_ui_snapshot_valid_ = true;
  model_ui_state_ = state;
  model_ui_transferred_ = progress.transferred;
  model_ui_total_ = progress.total;
  model_ui_error_code_ = progress.error_code;
  model_ui_error_context_ = progress.error_context;
}

LRESULT SwitcherSettingsDialog::OnMeasureItem(UINT,
                                              WPARAM,
                                              LPARAM parameter,
                                              BOOL& handled) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  if (!measure || (measure->CtlID != IDC_INPUT_MODE &&
                   measure->CtlID != IDC_SCHEMA_UPDATE_SETTINGS)) {
    handled = FALSE;
    return 0;
  }
  RECT height = {0, 0, 0, settings_navigation::kComboItemHeightDlu};
  ::MapDialogRect(m_hWnd, &height);
  measure->itemHeight = (std::max)(16u, static_cast<UINT>(height.bottom));
  handled = TRUE;
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnDrawItem(UINT,
                                           WPARAM,
                                           LPARAM parameter,
                                           BOOL& handled) {
  const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(parameter);
  if (!draw) {
    handled = FALSE;
    return 0;
  }
  const int id = static_cast<int>(draw->CtlID);
  const bool combo = id == IDC_INPUT_MODE || id == IDC_SCHEMA_UPDATE_SETTINGS;
  if (combo) {
    const bool edit_portion = (draw->itemState & ODS_COMBOBOXEDIT) != 0;
    const bool selected =
        (draw->itemState & ODS_SELECTED) != 0 && !edit_portion;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
    const COLORREF selected_fill = settings_navigation::Mix(
        settings_theme::GetColor(COLOR_HIGHLIGHT), surface, 25);
    ::FillRect(draw->hDC, &draw->rcItem,
               settings_theme::GetBrush(COLOR_WINDOW));

    RECT selection = draw->rcItem;
    const int scale = ::GetDeviceCaps(draw->hDC, LOGPIXELSX);
    if (selected) {
      const int horizontal_inset = (std::max)(3, ::MulDiv(3, scale, 96));
      const int vertical_inset = (std::max)(1, ::MulDiv(1, scale, 96));
      selection.left += horizontal_inset;
      selection.top += vertical_inset;
      selection.right -= horizontal_inset;
      selection.bottom -= vertical_inset;
      HBRUSH selection_brush = ::CreateSolidBrush(selected_fill);
      HPEN selection_pen = ::CreatePen(PS_NULL, 0, selected_fill);
      const HGDIOBJ old_brush = ::SelectObject(draw->hDC, selection_brush);
      const HGDIOBJ old_pen = ::SelectObject(draw->hDC, selection_pen);
      const int radius = (std::max)(6, ::MulDiv(8, scale, 96));
      ::RoundRect(draw->hDC, selection.left, selection.top, selection.right,
                  selection.bottom, radius, radius);
      ::SelectObject(draw->hDC, old_pen);
      ::SelectObject(draw->hDC, old_brush);
      ::DeleteObject(selection_pen);
      ::DeleteObject(selection_brush);

      const int marker_width = (std::max)(2, ::MulDiv(3, scale, 96));
      RECT marker{selection.left + horizontal_inset,
                  selection.top + (selection.bottom - selection.top) / 4,
                  selection.left + horizontal_inset + marker_width,
                  selection.bottom - (selection.bottom - selection.top) / 4};
      HBRUSH marker_brush =
          ::CreateSolidBrush(settings_theme::GetColor(COLOR_HIGHLIGHT));
      HRGN marker_region =
          ::CreateRoundRectRgn(marker.left, marker.top, marker.right,
                               marker.bottom, marker_width, marker_width);
      if (marker_region) {
        ::FillRgn(draw->hDC, marker_region, marker_brush);
        ::DeleteObject(marker_region);
      }
      ::DeleteObject(marker_brush);
    }

    std::wstring label;
    int item = static_cast<int>(draw->itemID);
    if (item == -1)
      item =
          static_cast<int>(::SendMessageW(draw->hwndItem, CB_GETCURSEL, 0, 0));
    if (item != CB_ERR) {
      const int length = static_cast<int>(
          ::SendMessageW(draw->hwndItem, CB_GETLBTEXTLEN, item, 0));
      if (length >= 0) {
        label.resize(static_cast<size_t>(length) + 1);
        ::SendMessageW(draw->hwndItem, CB_GETLBTEXT, item,
                       reinterpret_cast<LPARAM>(label.data()));
        label.resize(static_cast<size_t>(length));
      }
    }
    RECT text_rectangle = draw->rcItem;
    text_rectangle.left += selected ? (std::max)(13, ::MulDiv(13, scale, 96))
                                    : (std::max)(7, ::MulDiv(7, scale, 96));
    text_rectangle.right -= (std::max)(7, ::MulDiv(7, scale, 96));
    ::SetBkMode(draw->hDC, TRANSPARENT);
    ::SetTextColor(
        draw->hDC,
        settings_theme::GetColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    HFONT font = reinterpret_cast<HFONT>(
        ::SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(draw->hDC, font) : nullptr;
    ::DrawTextW(
        draw->hDC, label.c_str(), -1, &text_rectangle,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old_font)
      ::SelectObject(draw->hDC, old_font);
    handled = TRUE;
    return TRUE;
  }
  if (id == IDC_MODEL_DOWNLOAD_STATUS) {
    const int width = draw->rcItem.right - draw->rcItem.left;
    const int height = draw->rcItem.bottom - draw->rcItem.top;
    RECT canvas = {0, 0, width, height};
    HDC buffer = ::CreateCompatibleDC(draw->hDC);
    HBITMAP bitmap =
        buffer ? ::CreateCompatibleBitmap(draw->hDC, width, height) : nullptr;
    HGDIOBJ old_bitmap =
        bitmap ? ::SelectObject(buffer, bitmap) : static_cast<HGDIOBJ>(nullptr);
    HDC paint = old_bitmap ? buffer : draw->hDC;
    RECT paint_area = old_bitmap ? canvas : draw->rcItem;
    ::FillRect(paint, &paint_area, settings_theme::GetBrush(COLOR_WINDOW));
    ::SetBkMode(paint, TRANSPARENT);
    ::SetTextColor(paint, settings_theme::GetColor(COLOR_GRAYTEXT));
    HFONT font = reinterpret_cast<HFONT>(
        ::SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(paint, font) : nullptr;
    const int origin = old_bitmap ? 0 : draw->rcItem.left;
    const int top = old_bitmap ? 0 : draw->rcItem.top;
    const int bottom = old_bitmap ? height : draw->rcItem.bottom;
    SIZE phase_extent = {};
    ::GetTextExtentPoint32W(paint, model_download_phase_.c_str(),
                            static_cast<int>(model_download_phase_.size()),
                            &phase_extent);
    const std::wstring speed_reserve = L"· 9999.9 MB/s";
    SIZE speed_extent = {};
    ::GetTextExtentPoint32W(paint, speed_reserve.c_str(),
                            static_cast<int>(speed_reserve.size()),
                            &speed_extent);
    const int gap = (std::max)(4, height / 2);
    const int speed_left = origin + phase_extent.cx + gap;
    const int amount_left = speed_left + speed_extent.cx + gap;
    RECT phase = {origin, top, speed_left, bottom};
    RECT speed = {speed_left, top, amount_left, bottom};
    RECT amount = {amount_left, top, origin + width, bottom};
    const UINT format =
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    ::DrawTextW(paint, model_download_phase_.c_str(), -1, &phase, format);
    const std::wstring speed_text =
        model_download_speed_.empty() ? L"" : L"· " + model_download_speed_;
    const std::wstring amount_text =
        model_download_amount_.empty() ? L"" : L"· " + model_download_amount_;
    ::DrawTextW(paint, speed_text.c_str(), -1, &speed, format);
    ::DrawTextW(paint, amount_text.c_str(), -1, &amount, format);
    if (old_font)
      ::SelectObject(paint, old_font);
    if (old_bitmap) {
      ::BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top, width, height,
               buffer, 0, 0, SRCCOPY);
      ::SelectObject(buffer, old_bitmap);
    }
    if (bitmap)
      ::DeleteObject(bitmap);
    if (buffer)
      ::DeleteDC(buffer);
    handled = TRUE;
    return TRUE;
  }
  if (id == IDC_MODEL_PROGRESS_TEXT) {
    const int width = draw->rcItem.right - draw->rcItem.left;
    const int height = draw->rcItem.bottom - draw->rcItem.top;
    HDC buffer = ::CreateCompatibleDC(draw->hDC);
    HBITMAP bitmap =
        buffer ? ::CreateCompatibleBitmap(draw->hDC, width, height) : nullptr;
    HGDIOBJ old_bitmap =
        bitmap ? ::SelectObject(buffer, bitmap) : static_cast<HGDIOBJ>(nullptr);
    HDC paint = old_bitmap ? buffer : draw->hDC;
    RECT area = old_bitmap ? RECT{0, 0, width, height} : draw->rcItem;
    ::FillRect(paint, &area, settings_theme::GetBrush(COLOR_WINDOW));
    ::SetBkMode(paint, TRANSPARENT);
    ::SetTextColor(paint, settings_theme::GetColor(COLOR_WINDOWTEXT));
    HFONT font = reinterpret_cast<HFONT>(
        ::SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(paint, font) : nullptr;
    ::DrawTextW(paint, model_progress_percent_.c_str(), -1, &area,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old_font)
      ::SelectObject(paint, old_font);
    if (old_bitmap) {
      ::BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top, width, height,
               buffer, 0, 0, SRCCOPY);
      ::SelectObject(buffer, old_bitmap);
    }
    if (bitmap)
      ::DeleteObject(bitmap);
    if (buffer)
      ::DeleteDC(buffer);
    handled = TRUE;
    return TRUE;
  }
  const bool panel =
      id == IDC_SCHEMA_LIST_PANEL || id == IDC_SCHEMA_DETAIL_GROUP;
  const bool divider = id == IDC_MODEL_GROUP ||
                       id == IDC_SCHEMA_SHORTCUT_DIVIDER ||
                       id == IDC_SCHEMA_FOOTER_DIVIDER;
  const bool key =
      id == IDC_HOTKEYS || id == IDC_HOTKEY_GRAVE || id == IDC_HOTKEY_F4;
  if (!panel && !divider && !key) {
    handled = FALSE;
    return 0;
  }

  const auto blend = [](COLORREF base, COLORREF accent, int accent_percent) {
    const int base_percent = 100 - accent_percent;
    return RGB(
        (GetRValue(base) * base_percent + GetRValue(accent) * accent_percent) /
            100,
        (GetGValue(base) * base_percent + GetGValue(accent) * accent_percent) /
            100,
        (GetBValue(base) * base_percent + GetBValue(accent) * accent_percent) /
            100);
  };
  const COLORREF window_color = settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF border_color = blend(
      window_color, settings_theme::GetColor(COLOR_3DSHADOW), panel ? 36 : 48);
  RECT rectangle = draw->rcItem;
  ::FillRect(draw->hDC, &rectangle,
             settings_theme::GetBrush(panel ? COLOR_3DFACE : COLOR_WINDOW));
  if (divider) {
    const int y = (rectangle.top + rectangle.bottom) / 2;
    HPEN pen = ::CreatePen(PS_SOLID, 1, border_color);
    const HGDIOBJ old_pen = ::SelectObject(draw->hDC, pen);
    ::MoveToEx(draw->hDC, rectangle.left, y, nullptr);
    ::LineTo(draw->hDC, rectangle.right, y);
    ::SelectObject(draw->hDC, old_pen);
    ::DeleteObject(pen);
    handled = TRUE;
    return TRUE;
  }

  rectangle.right -= 1;
  rectangle.bottom -= 1;
  HPEN pen = ::CreatePen(PS_SOLID, 1, border_color);
  HBRUSH brush = ::CreateSolidBrush(
      settings_theme::GetColor(panel ? COLOR_WINDOW : COLOR_BTNFACE));
  const HGDIOBJ old_pen = ::SelectObject(draw->hDC, pen);
  const HGDIOBJ old_brush = ::SelectObject(draw->hDC, brush);
  RECT rounding = {0, 0, panel ? 8 : 5, 0};
  ::MapDialogRect(m_hWnd, &rounding);
  const int radius = rounding.right > 4 ? static_cast<int>(rounding.right) : 4;
  ::RoundRect(draw->hDC, rectangle.left, rectangle.top, rectangle.right,
              rectangle.bottom, radius, radius);
  ::SelectObject(draw->hDC, old_brush);
  ::SelectObject(draw->hDC, old_pen);
  ::DeleteObject(brush);
  ::DeleteObject(pen);

  if (key) {
    wchar_t label[32] = {};
    ::GetWindowTextW(draw->hwndItem, label, static_cast<int>(_countof(label)));
    ::SetBkMode(draw->hDC, TRANSPARENT);
    ::SetTextColor(draw->hDC, ::IsWindowEnabled(draw->hwndItem)
                                  ? settings_theme::GetColor(COLOR_WINDOWTEXT)
                                  : settings_theme::GetColor(COLOR_GRAYTEXT));
    HFONT font = reinterpret_cast<HFONT>(
        ::SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(draw->hDC, font) : nullptr;
    ::DrawTextW(draw->hDC, label, -1, &rectangle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_font)
      ::SelectObject(draw->hDC, old_font);
  }
  handled = TRUE;
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnCtlColorStatic(UINT,
                                                 WPARAM device_context,
                                                 LPARAM control,
                                                 BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(control));
  if (id == IDC_SCHEMA_DESCRIPTION && description_brush_.m_hBrush) {
    ::SetTextColor(reinterpret_cast<HDC>(device_context),
                   settings_theme::GetColor(COLOR_WINDOWTEXT));
    ::SetBkMode(reinterpret_cast<HDC>(device_context), TRANSPARENT);
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
  }
  const bool muted =
      id == IDC_SCHEMA_LAST_CHECK || id == IDC_SCHEMA_DETAIL_VERSION ||
      id == IDC_SCHEMA_AUTHOR || id == IDC_MODEL_DESCRIPTION ||
      id == IDC_MODEL_SOURCE || id == IDC_MODEL_DOWNLOAD_STATUS ||
      (id == IDC_MODEL_NOTE && !model_status_active_);
  const bool card = id == IDC_SCHEMA_DETAIL_NAME ||
                    id == IDC_SCHEMA_DETAIL_VERSION || id == IDC_MODEL_NAME ||
                    id == IDC_MODEL_DESCRIPTION || id == IDC_MODEL_SOURCE ||
                    id == IDC_MODEL_NOTE || id == IDC_MODEL_PROGRESS_TEXT ||
                    id == IDC_MODEL_DOWNLOAD_STATUS ||
                    id == IDC_SCHEMA_PROJECT_LINKS || id == IDC_SCHEMA_AUTHOR ||
                    id == IDC_SCHEMA_SHORTCUT_LABEL || id == IDC_HOTKEY_PLUS ||
                    id == IDC_HOTKEY_OR;
  if (muted || card) {
    if (muted) {
      ::SetTextColor(reinterpret_cast<HDC>(device_context),
                     settings_theme::GetColor(COLOR_GRAYTEXT));
    }
    ::SetBkMode(reinterpret_cast<HDC>(device_context), TRANSPARENT);
    return reinterpret_cast<LRESULT>(
        settings_theme::GetBrush(card ? COLOR_WINDOW : COLOR_3DFACE));
  }
  handled = FALSE;
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCtlColorEdit(UINT message,
                                               WPARAM device_context,
                                               LPARAM control,
                                               BOOL& handled) {
  return OnCtlColorStatic(message, device_context, control, handled);
}

LRESULT SwitcherSettingsDialog::OnSchemaCustomDraw(int,
                                                   LPNMHDR notification,
                                                   BOOL&) {
  auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(notification);
  if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    return CDRF_NOTIFYITEMDRAW;
  if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
    return CDRF_DODEFAULT;
  const HWND list = draw->nmcd.hdr.hwndFrom;
  const int item = static_cast<int>(draw->nmcd.dwItemSpec);
  const bool selected =
      (ListView_GetItemState(list, item, LVIS_SELECTED) & LVIS_SELECTED) != 0;
  const bool enabled = ::IsWindowEnabled(list) != FALSE;
  const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
  const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
  const COLORREF fill =
      selected ? settings_navigation::Mix(accent, surface, 38) : surface;
  HBRUSH background = ::CreateSolidBrush(fill);
  RECT row{};
  ListView_GetItemRect(list, item, &row, LVIR_BOUNDS);
  ::FillRect(draw->nmcd.hdc, &row, background);
  ::DeleteObject(background);
  RECT label{};
  ListView_GetItemRect(list, item, &label, LVIR_LABEL);
  const int size = settings_navigation::ScaledLogicalPixels(list, 14);
  RECT box{row.left + 2, row.top + (row.bottom - row.top - size) / 2,
           row.left + 2 + size,
           row.top + (row.bottom - row.top - size) / 2 + size};
  const bool checked = ListView_GetCheckState(list, item) != FALSE;
  const COLORREF border =
      checked ? accent : settings_theme::GetColor(COLOR_GRAYTEXT);
  HBRUSH brush = ::CreateSolidBrush(checked ? accent : surface);
  HPEN pen = ::CreatePen(PS_SOLID, 1, border);
  auto old_brush = ::SelectObject(draw->nmcd.hdc, brush);
  auto old_pen = ::SelectObject(draw->nmcd.hdc, pen);
  ::RoundRect(draw->nmcd.hdc, box.left, box.top, box.right, box.bottom, 5, 5);
  ::SelectObject(draw->nmcd.hdc, old_pen);
  ::SelectObject(draw->nmcd.hdc, old_brush);
  ::DeleteObject(brush);
  ::DeleteObject(pen);
  if (checked) {
    pen = ::CreatePen(PS_SOLID, (std::max)(1, size / 10),
                      settings_theme::GetColor(COLOR_HIGHLIGHTTEXT));
    old_pen = ::SelectObject(draw->nmcd.hdc, pen);
    ::MoveToEx(draw->nmcd.hdc, box.left + size / 4, box.top + size / 2,
               nullptr);
    ::LineTo(draw->nmcd.hdc, box.left + size * 2 / 5, box.top + size * 3 / 4);
    ::LineTo(draw->nmcd.hdc, box.left + size * 4 / 5, box.top + size / 4);
    ::SelectObject(draw->nmcd.hdc, old_pen);
    ::DeleteObject(pen);
  }
  wchar_t text[256]{};
  ListView_GetItemText(list, item, 0, text, _countof(text));
  label.left = (std::max)(label.left, box.right + 4);
  ::SetBkMode(draw->nmcd.hdc, TRANSPARENT);
  ::SetTextColor(
      draw->nmcd.hdc,
      settings_theme::GetColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
  ::DrawTextW(
      draw->nmcd.hdc, text, -1, &label,
      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  return CDRF_SKIPDEFAULT;
}

LRESULT SwitcherSettingsDialog::OnButtonCustomDraw(int control_id,
                                                   LPNMHDR notification,
                                                   BOOL& handled) {
  auto* draw = reinterpret_cast<LPNMCUSTOMDRAW>(notification);
  if (!draw || draw->dwDrawStage != CDDS_PREPAINT) {
    handled = FALSE;
    return CDRF_DODEFAULT;
  }
  const bool accent =
      (control_id == IDC_CHECK_SCHEME_UPDATES && check_button_accent_) ||
      (control_id == IDC_MODEL_DOWNLOAD && model_button_accent_);
  if (!accent) {
    handled = FALSE;
    return CDRF_DODEFAULT;
  }
  ::SetTextColor(draw->hdc, RGB(16, 124, 65));
  return CDRF_NEWFONT;
}
void SwitcherSettingsDialog::FinishModelDownload() {
  model_operation_failed_ = true;
  std::wstring error;
  if (!model_manager_.CompleteAndInstall(&error)) {
    ::MessageBoxW(m_hWnd, error.c_str(),
                  LocalText(L"语法模型校验失败", L"語法模型校驗失敗",
                            L"Grammar model verification failed")
                      .c_str(),
                  MB_OK | MB_ICONERROR);
    UpdateModelUi();
    return;
  }
  model_operation_failed_ = false;
  pending_model_action_ = PendingModelAction::Install;
  UpdateApplyButton();
  UpdateModelUi();
}

LRESULT SwitcherSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  LayoutInputPage(m_hWnd);
  HWND last_check = GetDlgItem(IDC_SCHEMA_LAST_CHECK);
  LONG_PTR last_check_style = ::GetWindowLongPtrW(last_check, GWL_STYLE);
  last_check_style &= ~static_cast<LONG_PTR>(SS_TYPEMASK);
  last_check_style |= SS_RIGHT;
  ::SetWindowLongPtrW(last_check, GWL_STYLE, last_check_style);
  LOGFONTW base_font = {};
  ::GetObjectW(GetFont(), sizeof(base_font), &base_font);
  LOGFONTW heading = base_font;
  heading.lfWeight = FW_SEMIBOLD;
  if (heading_font_.CreateFontIndirect(&heading)) {
    for (int id : {IDC_SCHEMA_LIST_LABEL, IDC_SCHEMA_DETAIL_LABEL,
                   IDC_SCHEMA_DETAIL_NAME, IDC_MODEL_NAME}) {
      CWindow(GetDlgItem(id)).SetFont(heading_font_);
    }
  }

  schema_list_.SubclassWindow(GetDlgItem(IDC_SCHEMA_LIST));
  schema_list_.ModifyStyle(0, LVS_SHOWSELALWAYS);
  schema_list_.SetExtendedListViewStyle(
      LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER,
      LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
  schema_list_.SetBkColor(settings_theme::GetColor(COLOR_WINDOW));
  schema_list_.SetTextBkColor(settings_theme::GetColor(COLOR_WINDOW));

  CString schema_name;
  schema_name.LoadStringW(IDS_STR_SCHEMA_NAME);
  schema_list_.AddColumn(schema_name, 0);
  CRect rect;
  schema_list_.GetClientRect(&rect);
  // Put the native report-column boundary under the list's right frame.  The
  // boundary otherwise continues through the empty area below the final row.
  schema_list_.SetColumnWidth(0, rect.Width() + ::GetSystemMetrics(SM_CXEDGE));
  ::ShowScrollBar(schema_list_, SB_HORZ, FALSE);

  description_.Attach(GetDlgItem(IDC_SCHEMA_DESCRIPTION));
  const auto blend = [](COLORREF base, COLORREF accent, int accent_percent) {
    const int base_percent = 100 - accent_percent;
    return RGB(
        (GetRValue(base) * base_percent + GetRValue(accent) * accent_percent) /
            100,
        (GetGValue(base) * base_percent + GetGValue(accent) * accent_percent) /
            100,
        (GetBValue(base) * base_percent + GetBValue(accent) * accent_percent) /
            100);
  };
  description_brush_.CreateSolidBrush(
      blend(settings_theme::GetColor(COLOR_WINDOW),
            settings_theme::GetColor(COLOR_3DFACE), 35));
  model_progress_.Attach(GetDlgItem(IDC_MODEL_PROGRESS));
  model_progress_.SetRange32(0, 1000);
  input_mode_.Attach(GetDlgItem(IDC_INPUT_MODE));
  update_frequency_.Attach(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS));
  settings_navigation::StyleCombo(m_hWnd, IDC_INPUT_MODE);
  settings_navigation::StyleCombo(m_hWnd, IDC_SCHEMA_UPDATE_SETTINGS);
  loading_input_mode_ = true;
  if (!LoadInputMode(&selected_input_mode_))
    selected_input_mode_ = L"全拼";
  int selected_mode = 0;
  for (int index = 0; index < static_cast<int>(_countof(kInputModes));
       ++index) {
    const auto& mode = kInputModes[index];
    const int row = input_mode_.AddString(
        LocalText(mode.simplified, mode.traditional, mode.english).c_str());
    input_mode_.SetItemData(row, static_cast<DWORD_PTR>(index));
    if (selected_input_mode_ == mode.value)
      selected_mode = row;
  }
  input_mode_.SetCurSel(selected_mode);
  selected_input_mode_ =
      kInputModes[static_cast<size_t>(input_mode_.GetItemData(selected_mode))]
          .value;
  initial_input_mode_ = selected_input_mode_;
  loading_input_mode_ = false;
  for (int frequency = kUpdateSmart; frequency <= kUpdateDisabled;
       ++frequency) {
    update_frequency_.AddString(UpdateFrequencyText(frequency).c_str());
  }
  selected_update_frequency_ = LoadUpdateFrequency("wanxiang_lite");
  initial_update_frequency_ = selected_update_frequency_;

  auto capture_control_rect = [&](int id, CRect* target) {
    ::GetWindowRect(GetDlgItem(id), target);
    ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(target),
                      2);
  };
  capture_control_rect(IDC_MODEL_SECONDARY, &model_secondary_rect_);
  capture_control_rect(IDC_MODEL_DOWNLOAD, &model_primary_rect_);
  capture_control_rect(IDC_INPUT_MODE, &input_mode_base_rect_);
  RECT active_secondary = {384, 196, 444, 214};
  RECT active_primary = {452, 196, 512, 214};
  ::MapDialogRect(m_hWnd, &active_secondary);
  ::MapDialogRect(m_hWnd, &active_primary);
  model_active_secondary_rect_ = active_secondary;
  model_active_primary_rect_ = active_primary;

  ::ShowWindow(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS), SW_HIDE);
  // Package importing remains hidden until conflict-safe transactional install
  // is implemented. Do not fall back to the legacy command window.
  ::ShowWindow(GetDlgItem(IDC_ADD_SCHEMA_GROUP), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_GET_SCHEMATA), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_ADD_SCHEME_URL), SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_IMPORT_SCHEME), SW_HIDE);
  WanxiangUpdateManager::Result cached;
  if (WanxiangUpdateManager::LoadCachedResult(&cached)) {
    scheme_update_available_ = cached.scheme_update_available;
    model_update_available_ = cached.model_update_available;
    latest_release_tag_ = cached.latest_tag;
    latest_model_sha256_ = cached.latest_model_sha256;
    latest_model_size_ = cached.latest_model_size;
    if (!latest_model_sha256_.empty() && latest_model_size_)
      model_manager_.ConfigureTarget(latest_model_sha256_, latest_model_size_);
    WanxiangUpdateManager::StoreAvailableCount(static_cast<unsigned int>(
        scheme_update_available_ + model_update_available_));
  }

  Populate();
  ApplyControlRounding();
  ::EnableWindow(GetDlgItem(IDOK), FALSE);
  const auto user_folder = WeaselUserDataPath().wstring();
  ::SetDlgItemTextW(
      m_hWnd, IDC_USER_DATA_FOLDER,
      (L"<a id=\"open\">" + EscapeLinkText(user_folder) + L"</a>").c_str());
  if (const char* hotkeys = api_->get_hotkeys(settings_)) {
    const auto configured = u8tow(hotkeys);
    const bool grave = configured.find(L"Control+grave") != std::wstring::npos;
    const bool f4 = configured.find(L"F4") != std::wstring::npos;
    ::ShowWindow(GetDlgItem(IDC_HOTKEYS), grave ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_HOTKEY_PLUS), grave ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_HOTKEY_GRAVE), grave ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_HOTKEY_OR), grave && f4 ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_HOTKEY_F4), f4 ? SW_SHOW : SW_HIDE);
  }
  tooltip_.Create(m_hWnd);
  TOOLINFOW tool = {sizeof(tool)};
  tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tool.hwnd = m_hWnd;
  HWND grave_key = GetDlgItem(IDC_HOTKEY_GRAVE);
  tool.uId = reinterpret_cast<UINT_PTR>(grave_key);
  tooltip_text_ = LocalText(L"反引号键位于 Esc 下方、数字 1 左侧。",
                            L"反引號鍵位於 Esc 下方、數字 1 左側。",
                            L"The grave key is below Esc and left of 1.");
  tool.lpszText = const_cast<wchar_t*>(tooltip_text_.c_str());
  tooltip_.AddTool(&tool);
  UpdateLastCheckText();
  UpdateCheckButton();
  SetTimer(kModelTimer, 500);

  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Input,
      {IDOK,
       IDCANCEL,
       WeaselDisplayUserDataPath().wstring(),
       {IDC_SWITCHER_TITLE, IDC_USER_DATA_LABEL, IDC_USER_DATA_FOLDER}});
  const int navigation_offset = static_cast<int>(
      settings_navigation::MapDialogUnits(
          m_hWnd, 0, 0, settings_navigation::kSidebarWidthDlu, 0)
          .right);
  model_secondary_rect_.OffsetRect(navigation_offset, 0);
  model_primary_rect_.OffsetRect(navigation_offset, 0);
  model_active_secondary_rect_.OffsetRect(navigation_offset, 0);
  model_active_primary_rect_.OffsetRect(navigation_offset, 0);
  input_mode_base_rect_.OffsetRect(navigation_offset, 0);
  UpdateModelUi();
  UpdateApplyButton();
  CenterWindow();
  return settings_navigation::HostedPageInitResult();
}

LRESULT SwitcherSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (apply_operation_) {
    close_after_apply_ = true;
    ShowWindow(SW_HIDE);
    return 0;
  }
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (!ConfirmClose())
    return 0;
  PrepareClose();
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (apply_operation_) {
    close_after_apply_ = true;
    ShowWindow(SW_HIDE);
    return 0;
  }
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (!ConfirmClose())
    return 0;
  PrepareClose();
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::Input || apply_operation_)
    return 0;
  if (settings_navigation::RequestNavigate(m_hWnd, id))
    return 0;
  if (!ConfirmDiscardChanges())
    return 0;
  PrepareClose();
  EndDialog(id);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnTimer(UINT, WPARAM timer, LPARAM, BOOL&) {
  if (timer != kModelTimer)
    return 0;
  FinishApply();
  if (apply_operation_)
    return 0;
  if (scheme_update_operation_ && !scheme_update_operation_->done.load())
    UpdateCheckButton();
  FinishSchemeUpdate();
  FinishUpdateCheck();
  const auto progress = model_manager_.GetProgress();
  if (!model_operation_failed_ &&
      pending_model_action_ == PendingModelAction::None &&
      progress.state == WanxiangModelManager::State::Transferred) {
    UpdateModelUi();
    KillTimer(kModelTimer);
    FinishModelDownload();
    SetTimer(kModelTimer, 500);
  } else if (selected_schema_ < schemas_.size() &&
             schemas_[selected_schema_].id == "wanxiang_lite" &&
             ModelUiNeedsRefresh(progress)) {
    UpdateModelUi(&progress);
  }
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCheckUpdates(WORD, WORD, HWND, BOOL&) {
  if (update_check_ || scheme_update_operation_)
    return 0;
  open_update_list_after_check_ = true;
  HWND button = GetDlgItem(IDC_CHECK_SCHEME_UPDATES);
  ::EnableWindow(button, FALSE);
  ::SetWindowTextW(button,
                   LocalText(L"正在检查…", L"正在檢查…", L"Checking…").c_str());
  check_button_accent_ = false;
  RedrawWindow(nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

  update_check_ = std::make_shared<UpdateCheck>();
  std::thread([check = update_check_]() {
    try {
      check->result = WanxiangUpdateManager::CheckNow();
    } catch (...) {
      check->result.success = false;
    }
    check->done.store(true);
  }).detach();
  return 0;
}

void SwitcherSettingsDialog::FinishUpdateCheck() {
  if (!update_check_ || !update_check_->done.load())
    return;
  const auto result = update_check_->result;
  update_check_.reset();
  if (!result.success) {
    open_update_list_after_check_ = false;
    LOG(ERROR) << "Unable to check Wanxiang releases: " << wtou8(result.error);
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"暂时无法检查更新，请稍后重试。",
                  L"暫時無法檢查更新，請稍後重試。",
                  L"Updates could not be checked. Try again later.")
            .c_str(),
        LocalText(L"检查更新", L"檢查更新", L"Check for updates").c_str(),
        MB_OK | MB_ICONINFORMATION);
  } else {
    scheme_update_available_ = result.scheme_update_available;
    model_update_available_ = result.model_update_available;
    latest_release_tag_ = result.latest_tag;
    latest_scheme_release_.tag = result.latest_tag;
    latest_scheme_release_.url = result.latest_scheme_url;
    latest_scheme_release_.sha256 = result.latest_scheme_sha256;
    latest_scheme_release_.size = result.latest_scheme_size;
    latest_scheme_release_.from_cnb = result.latest_scheme_from_cnb;
    latest_model_sha256_ = result.latest_model_sha256;
    latest_model_size_ = result.latest_model_size;
    if (!latest_model_sha256_.empty() && latest_model_size_)
      model_manager_.ConfigureTarget(latest_model_sha256_, latest_model_size_);
    if (selected_schema_ < schemas_.size() &&
        schemas_[selected_schema_].id == "wanxiang_lite") {
      UpdateModelUi();
    }
    if (!result.error.empty()) {
      LOG(WARNING) << "Wanxiang scheme version was checked, but model "
                      "metadata was unavailable: "
                   << wtou8(result.error);
      if (!result.update_available) {
        ::MessageBoxW(
            m_hWnd,
            LocalText(L"输入方案版本已检查；语法模型更新信息暂时无法获取。",
                      L"輸入方案版本已檢查；語法模型更新資訊暫時無法取得。",
                      L"The schema version was checked, but grammar-model "
                      L"update information is temporarily unavailable.")
                .c_str(),
            LocalText(L"检查更新", L"檢查更新", L"Check for updates").c_str(),
            MB_OK | MB_ICONINFORMATION);
      }
    }
  }
  UpdateCheckButton();
  UpdateLastCheckText();
  if (open_update_list_after_check_ && result.success &&
      result.update_available) {
    open_update_list_after_check_ = false;
    ShowUpdateList();
  } else {
    open_update_list_after_check_ = false;
  }
  return;
}

void SwitcherSettingsDialog::UpdateCheckButton() {
  HWND button = GetDlgItem(IDC_CHECK_SCHEME_UPDATES);
  if (scheme_update_operation_ && !scheme_update_operation_->done.load()) {
    const auto phase = scheme_update_operation_->phase.load();
    const auto completed = scheme_update_operation_->completed.load();
    const auto total = scheme_update_operation_->total.load();
    std::wstring button_text;
    std::wstring status_text;
    switch (phase) {
      case SchemeUpdateOperation::Phase::Downloading: {
        const unsigned long long percent = total ? completed * 100 / total : 0;
        button_text = LocalText(L"下载 · ", L"下載 · ", L"Download · ") +
                      std::to_wstring(percent) + L"%";
        wchar_t progress[128] = {};
        swprintf_s(progress,
                   LocalText(L"正在下载方案：%.1f / %.1f MB",
                             L"正在下載方案：%.1f / %.1f MB",
                             L"Downloading schema: %.1f / %.1f MB")
                       .c_str(),
                   completed / 1048576.0, total / 1048576.0);
        status_text = progress;
        break;
      }
      case SchemeUpdateOperation::Phase::Verifying:
        button_text = LocalText(L"正在校验…", L"正在校驗…", L"Verifying…");
        status_text = LocalText(L"正在校验下载文件…", L"正在校驗下載檔案…",
                                L"Verifying downloaded files…");
        break;
      case SchemeUpdateOperation::Phase::Extracting:
        button_text = LocalText(L"正在解压…", L"正在解壓…", L"Extracting…");
        status_text = LocalText(L"正在解压输入方案…", L"正在解壓輸入方案…",
                                L"Extracting schema…");
        break;
      case SchemeUpdateOperation::Phase::Installing:
        button_text = LocalText(L"正在安装…", L"正在安裝…", L"Installing…");
        status_text =
            LocalText(L"正在安装 Lite 方案文件…", L"正在安裝 Lite 方案檔案…",
                      L"Installing Lite schema files…");
        break;
      case SchemeUpdateOperation::Phase::Deploying:
        button_text = LocalText(L"正在部署…", L"正在部署…", L"Deploying…");
        status_text =
            LocalText(L"正在重新部署输入方案…", L"正在重新部署輸入方案…",
                      L"Redeploying schema…");
        break;
      case SchemeUpdateOperation::Phase::RollingBack:
        button_text = LocalText(L"正在恢复…", L"正在還原…", L"Restoring…");
        status_text = LocalText(L"正在恢复更新前的 Lite 文件…",
                                L"正在還原更新前的 Lite 檔案…",
                                L"Restoring previous Lite files…");
        break;
      case SchemeUpdateOperation::Phase::Finalizing:
        button_text = LocalText(L"正在完成…", L"正在完成…", L"Finishing…");
        status_text =
            LocalText(L"正在完成输入方案更新…", L"正在完成輸入方案更新…",
                      L"Finishing schema update…");
        break;
      case SchemeUpdateOperation::Phase::Preparing:
      default:
        button_text = LocalText(L"正在准备…", L"正在準備…", L"Preparing…");
        status_text =
            LocalText(L"正在准备输入方案更新…", L"正在準備輸入方案更新…",
                      L"Preparing schema update…");
        break;
    }
    check_button_accent_ = false;
    ::SetWindowTextW(button, button_text.c_str());
    ::EnableWindow(button, FALSE);
    HWND status = GetDlgItem(IDC_SCHEMA_LAST_CHECK);
    ::SetWindowTextW(status, status_text.c_str());
    ::ShowWindow(status, SW_SHOW);
    return;
  }
  if (update_check_ && !update_check_->done.load()) {
    check_button_accent_ = false;
    ::SetWindowTextW(
        button, LocalText(L"正在检查…", L"正在檢查…", L"Checking…").c_str());
    ::EnableWindow(button, FALSE);
    return;
  }
  const int count = static_cast<int>(scheme_update_available_) +
                    static_cast<int>(model_update_available_);
  std::wstring label;
  if (count) {
    label = LocalText(L"更新 · ", L"更新 · ", L"Updates · ") +
            std::to_wstring(count) + LocalText(L" 项", L" 項", L"");
  } else {
    label = LocalText(L"检查更新", L"檢查更新", L"Check updates");
  }
  check_button_accent_ = count != 0;
  ::SetWindowTextW(button, label.c_str());
  ::EnableWindow(button, TRUE);
  ::InvalidateRect(button, nullptr, TRUE);
}

void SwitcherSettingsDialog::UpdateLastCheckText() {
  std::wstring tag;
  SYSTEMTIME checked = {};
  SYSTEMTIME installed = {};
  HWND label = GetDlgItem(IDC_SCHEMA_LAST_CHECK);
  const bool has_check = WanxiangUpdateManager::LoadLastCheck(&tag, &checked);
  const bool has_install =
      WanxiangModelManager::LoadLastInstalledTime(&installed);
  if (!has_check && !has_install) {
    ::ShowWindow(label, SW_HIDE);
    return;
  }

  const auto value_of = [](const SYSTEMTIME& time) {
    FILETIME file_time = {};
    ULARGE_INTEGER value = {};
    if (::SystemTimeToFileTime(&time, &file_time)) {
      value.LowPart = file_time.dwLowDateTime;
      value.HighPart = file_time.dwHighDateTime;
    }
    return value.QuadPart;
  };
  const bool show_install =
      has_install && (!has_check || value_of(installed) > value_of(checked));
  const SYSTEMTIME& displayed = show_install ? installed : checked;
  wchar_t text[96] = {};
  swprintf_s(text,
             LocalText(show_install ? L"上次更新：%04u-%02u-%02u %02u:%02u"
                                    : L"上次检查：%04u-%02u-%02u %02u:%02u",
                       show_install ? L"上次更新：%04u-%02u-%02u %02u:%02u"
                                    : L"上次檢查：%04u-%02u-%02u %02u:%02u",
                       show_install ? L"Last updated: %04u-%02u-%02u %02u:%02u"
                                    : L"Last checked: %04u-%02u-%02u %02u:%02u")
                 .c_str(),
             displayed.wYear, displayed.wMonth, displayed.wDay, displayed.wHour,
             displayed.wMinute);
  ::SetWindowTextW(label, text);
  ::ShowWindow(label, SW_SHOW);
}

void SwitcherSettingsDialog::ShowUpdateList() {
  WanxiangUpdateManager::Result result;
  result.success = true;
  result.update_available = scheme_update_available_ || model_update_available_;
  result.scheme_update_available = scheme_update_available_;
  result.model_update_available = model_update_available_;
  result.latest_tag = latest_release_tag_;
  result.installed_scheme_version =
      WanxiangUpdateManager::LoadInstalledSchemeVersion();
  result.latest_scheme_url = latest_scheme_release_.url;
  result.latest_scheme_sha256 = latest_scheme_release_.sha256;
  result.latest_scheme_size = latest_scheme_release_.size;
  result.latest_scheme_from_cnb = latest_scheme_release_.from_cnb;
  result.latest_model_sha256 = latest_model_sha256_;
  result.latest_model_size = latest_model_size_;
  PackageUpdateDialog dialog(result);
  if (dialog.DoModal(m_hWnd) != IDOK)
    return;
  if (dialog.selection().scheme)
    StartSchemeUpdate(latest_scheme_release_);
  if (!dialog.selection().model)
    return;
  std::wstring error;
  model_operation_failed_ = false;
  if (!latest_model_sha256_.empty() && latest_model_size_)
    model_manager_.ConfigureTarget(latest_model_sha256_, latest_model_size_);
  const bool started = model_manager_.Start(&error);
  if (!started) {
    ::MessageBoxW(
        m_hWnd, error.c_str(),
        LocalText(L"无法开始更新", L"無法開始更新", L"Unable to update")
            .c_str(),
        MB_OK | MB_ICONERROR);
  }
  UpdateModelUi();
  if (started && schema_list_.IsWindow())
    schema_list_.SetFocus();
}

void SwitcherSettingsDialog::StartSchemeUpdate(
    const WanxiangUpdateManager::SchemeRelease& release) {
  if (scheme_update_operation_)
    return;
  scheme_update_operation_ = std::make_shared<SchemeUpdateOperation>();
  ::EnableWindow(GetDlgItem(IDOK), FALSE);
  UpdateCheckButton();
  auto operation = scheme_update_operation_;
  std::thread([operation, release]() {
    WanxiangSchemeManager manager;
    manager.SetProgressCallback([operation](WanxiangSchemeManager::Phase phase,
                                            unsigned long long completed,
                                            unsigned long long total) {
      switch (phase) {
        case WanxiangSchemeManager::Phase::Downloading:
          operation->phase.store(SchemeUpdateOperation::Phase::Downloading);
          break;
        case WanxiangSchemeManager::Phase::Verifying:
          operation->phase.store(SchemeUpdateOperation::Phase::Verifying);
          break;
        case WanxiangSchemeManager::Phase::Extracting:
          operation->phase.store(SchemeUpdateOperation::Phase::Extracting);
          break;
        case WanxiangSchemeManager::Phase::Installing:
          operation->phase.store(SchemeUpdateOperation::Phase::Installing);
          break;
        case WanxiangSchemeManager::Phase::Preparing:
        default:
          operation->phase.store(SchemeUpdateOperation::Phase::Preparing);
          break;
      }
      operation->completed.store(completed);
      operation->total.store(total);
    });
    std::wstring error;
    if (!manager.PrepareAndInstall(release, &error)) {
      const std::wstring primary_error = error;
      WanxiangUpdateManager::SchemeRelease fallback;
      std::wstring fallback_error;
      if (!release.from_cnb ||
          !WanxiangUpdateManager::QueryGithubSchemeRelease(
              release.tag, &fallback, &fallback_error) ||
          !manager.PrepareAndInstall(fallback, &fallback_error)) {
        operation->error = L"CNB 更新失败：" + primary_error;
        if (!fallback_error.empty())
          operation->error += L"\nGitHub 备用源失败：" + fallback_error;
        operation->done.store(true);
        return;
      }
    }
    operation->phase.store(SchemeUpdateOperation::Phase::Deploying);
    const auto deployment_started =
        std::filesystem::file_time_type::clock::now();
    Configurator configurator;
    const bool deployed = configurator.UpdateWorkspace(false) == 0;
    const std::wstring deployment_error =
        deployed ? std::wstring() : CollectDeploymentErrors(deployment_started);
    operation->phase.store(SchemeUpdateOperation::Phase::Finalizing);
    if (!manager.Commit(&error)) {
      std::wstring rollback_error;
      operation->phase.store(SchemeUpdateOperation::Phase::RollingBack);
      operation->restored = manager.Rollback(&rollback_error) &&
                            configurator.UpdateWorkspace(false) == 0;
      operation->error =
          operation->restored
              ? L"无法完成输入方案安装记录，已恢复更新前的文件。"
              : L"输入方案安装记录和自动恢复均失败，请查看部署日志。";
      if (!error.empty())
        operation->error += L"\n" + error;
      if (!rollback_error.empty())
        operation->error += L"\n" + rollback_error;
      operation->done.store(true);
      return;
    }
    operation->success = true;
    operation->installed_tag = release.tag;
    if (!deployed) {
      operation->error =
          L"Lite 方案文件已更新，但重新部署未完成。用户的 my_dicts、根目录 "
          L"*.custom.yaml 和非 Lite 方案文件均未被修改。";
      if (!deployment_error.empty())
        operation->error += L"\n\n部署失败原因：\n" + deployment_error;
      else
        operation->error +=
            L"\n\n部署日志未返回具体错误，请检查用户文件夹中的配置引用。";
    }
    operation->done.store(true);
  }).detach();
}

void SwitcherSettingsDialog::FinishSchemeUpdate() {
  if (!scheme_update_operation_ || !scheme_update_operation_->done.load())
    return;
  const auto operation = scheme_update_operation_;
  scheme_update_operation_.reset();
  if (operation->success) {
    scheme_update_available_ = false;
    WanxiangUpdateManager::StoreAvailableCount(model_update_available_ ? 1u
                                                                       : 0u);
    if (selected_schema_ < schemas_.size() &&
        schemas_[selected_schema_].id == "wanxiang_lite") {
      ShowDetails(selected_schema_);
    }
    if (operation->error.empty()) {
      ::MessageBoxW(
          m_hWnd,
          (LocalText(L"万象拼音 Lite 已更新到 ", L"萬象拼音 Lite 已更新到 ",
                     L"Wanxiang Lite was updated to ") +
           operation->installed_tag +
           LocalText(L"，并已完成重新部署。", L"，並已完成重新部署。",
                     L" and redeployed."))
              .c_str(),
          LocalText(L"输入方案更新完成", L"輸入方案更新完成",
                    L"Schema update complete")
              .c_str(),
          MB_OK | MB_ICONINFORMATION);
    } else {
      ::MessageBoxW(m_hWnd, operation->error.c_str(),
                    LocalText(L"方案文件已更新，部署未完成",
                              L"方案檔案已更新，部署未完成",
                              L"Schema files updated; deployment incomplete")
                        .c_str(),
                    MB_OK | MB_ICONWARNING);
    }
  } else {
    ::MessageBoxW(m_hWnd, operation->error.c_str(),
                  LocalText(L"输入方案更新失败", L"輸入方案更新失敗",
                            L"Schema update failed")
                      .c_str(),
                  MB_OK | MB_ICONERROR);
  }
  UpdateCheckButton();
  UpdateLastCheckText();
  UpdateApplyButton();
}

LRESULT SwitcherSettingsDialog::OnUpdateSettingsChanged(WORD,
                                                        WORD,
                                                        HWND,
                                                        BOOL&) {
  if (selected_schema_ >= schemas_.size())
    return 0;
  const auto& schema = schemas_[selected_schema_];
  if (schema.id != "wanxiang_lite")
    return 0;
  const int frequency = update_frequency_.GetCurSel();
  if (frequency != CB_ERR && frequency != selected_update_frequency_) {
    selected_update_frequency_ = frequency;
    update_frequency_modified_ =
        selected_update_frequency_ != initial_update_frequency_;
    modified_ = HasSchemaSelectionChanges() || input_mode_modified_ ||
                update_frequency_modified_;
    UpdateApplyButton();
  }
  ::RedrawWindow(update_frequency_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnProjectLink(int,
                                              LPNMHDR notification,
                                              BOOL&) {
  const auto* link = reinterpret_cast<PNMLINK>(notification);
  if (!link)
    return 0;
  const std::wstring url(link->item.szUrl);
  if (url.rfind(L"https://github.com/amzxyz/rime-wanxiang", 0) != 0 &&
      url.rfind(L"https://cnb.cool/amzxyz/rime-wanxiang", 0) != 0) {
    return 0;
  }
  ShellExecuteW(m_hWnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnUserFolderLink(int, LPNMHDR, BOOL&) {
  const auto folder = WeaselUserDataPath();
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error || reinterpret_cast<INT_PTR>(
                   ::ShellExecuteW(m_hWnd, L"open", folder.c_str(), nullptr,
                                   nullptr, SW_SHOWNORMAL)) <= 32) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法打开用户文件夹，请检查路径是否可用。",
                  L"無法開啟使用者資料夾，請檢查路徑是否可用。",
                  L"Cannot open the user folder. Check that the path is "
                  L"available.")
            .c_str(),
        LocalText(L"用户文件夹", L"使用者資料夾", L"User folder").c_str(),
        MB_OK | MB_ICONERROR);
  }
  return 0;
}

LRESULT SwitcherSettingsDialog::OnInputModeChanged(WORD, WORD, HWND, BOOL&) {
  if (loading_input_mode_)
    return 0;
  const int selected = input_mode_.GetCurSel();
  if (selected == CB_ERR)
    return 0;
  const auto index = static_cast<size_t>(input_mode_.GetItemData(selected));
  if (index >= _countof(kInputModes))
    return 0;
  if (selected_input_mode_ != kInputModes[index].value) {
    selected_input_mode_ = kInputModes[index].value;
    input_mode_modified_ = selected_input_mode_ != initial_input_mode_;
    modified_ = HasSchemaSelectionChanges() || input_mode_modified_ ||
                update_frequency_modified_;
    UpdateApplyButton();
  }
  AdjustInputModeWidth();
  ::RedrawWindow(input_mode_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnModelPrimary(WORD, WORD, HWND, BOOL&) {
  std::wstring error;
  model_operation_failed_ = false;
  const auto state = model_manager_.GetProgress().state;
  if (state == WanxiangModelManager::State::Transferred) {
    FinishModelDownload();
    return 0;
  }
  const bool success = state == WanxiangModelManager::State::Downloading
                           ? model_manager_.Pause(&error)
                           : model_manager_.Start(&error);
  if (!success) {
    ::MessageBoxW(
        m_hWnd, error.c_str(),
        LocalText(L"无法开始下载", L"無法開始下載", L"Unable to start download")
            .c_str(),
        MB_OK | MB_ICONERROR);
  }
  UpdateModelUi();
  if (success && schema_list_.IsWindow())
    schema_list_.SetFocus();
  return 0;
}

LRESULT SwitcherSettingsDialog::OnModelSecondary(WORD, WORD, HWND, BOOL&) {
  if (pending_model_action_ == PendingModelAction::Remove) {
    pending_model_action_ = PendingModelAction::None;
    model_operation_failed_ = false;
    UpdateApplyButton();
    UpdateModelUi();
    return 0;
  }

  const auto state = model_manager_.GetProgress().state;
  if (state == WanxiangModelManager::State::Downloading ||
      state == WanxiangModelManager::State::WaitingRetry ||
      state == WanxiangModelManager::State::Paused ||
      state == WanxiangModelManager::State::RestartRequired ||
      state == WanxiangModelManager::State::Error) {
    model_manager_.Cancel();
    model_operation_failed_ = false;
    UpdateModelUi();
    return 0;
  }

  const int answer = ::MessageBoxW(
      m_hWnd,
      LocalText(L"确定移除已安装的语法模型吗？",
                L"確定移除已安裝的語法模型嗎？",
                L"Remove the installed grammar model?")
          .c_str(),
      LocalText(L"移除语法模型", L"移除語法模型", L"Remove grammar model")
          .c_str(),
      MB_YESNO | MB_ICONQUESTION);
  if (answer != IDYES)
    return 0;

  pending_model_action_ = PendingModelAction::Remove;
  model_operation_failed_ = false;
  UpdateApplyButton();
  UpdateModelUi();
  return 0;
}

bool SwitcherSettingsDialog::ApplyChanges() {
  if (scheme_update_operation_ && !scheme_update_operation_->done.load())
    return false;
  const bool schema_selection_modified = HasSchemaSelectionChanges();
  if (!HasPendingChanges()) {
    UpdateApplyButton();
    return true;
  }
  std::vector<const char*> selection;
  if (schema_selection_modified && settings_ && !schemas_.empty()) {
    for (const auto& schema : schemas_) {
      if (schema.enabled)
        selection.push_back(schema.id.c_str());
    }
    if (selection.empty()) {
      MSG_BY_IDS(IDS_STR_ERR_AT_LEAST_ONE_SEL, IDS_STR_NOT_REGULAR,
                 MB_OK | MB_ICONEXCLAMATION);
      return false;
    }
  }
  if (input_mode_modified_) {
    std::wstring error;
    if (!SaveInputMode(selected_input_mode_, &error)) {
      RestorePersistedSettings();
      ::MessageBoxW(m_hWnd, error.c_str(),
                    LocalText(L"无法保存拼音方式", L"無法儲存拼音方式",
                              L"Cannot save input mode")
                        .c_str(),
                    MB_OK | MB_ICONERROR);
      return false;
    }
  }
  if (schema_selection_modified && settings_ && !schemas_.empty()) {
    api_->select_schemas(settings_, selection.data(),
                         static_cast<int>(selection.size()));
  }
  if (update_frequency_modified_ &&
      !SaveUpdateFrequency("wanxiang_lite", selected_update_frequency_)) {
    RestorePersistedSettings();
    ::MessageBoxW(m_hWnd,
                  LocalText(L"无法保存更新频率。", L"無法儲存更新頻率。",
                            L"The update frequency could not be saved.")
                      .c_str(),
                  LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
                  MB_OK | MB_ICONERROR);
    return false;
  }
  if (schema_selection_modified && settings_ &&
      !api_->save_settings(reinterpret_cast<RimeCustomSettings*>(settings_))) {
    RestorePersistedSettings();
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法保存输入方案设置。", L"無法儲存輸入方案設定。",
                  L"Schema settings could not be saved.")
            .c_str(),
        LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
        MB_OK | MB_ICONERROR);
    return false;
  }
  const PendingModelAction model_action = pending_model_action_;
  SetApplyingUi(true);
  apply_operation_ = std::make_shared<ApplyOperation>();
  apply_operation_->model_action = model_action;
  try {
    std::thread([this, operation = apply_operation_, model_action]() {
      if (model_action == PendingModelAction::Remove &&
          !model_manager_.RemoveInstalled(&operation->model_operation_error)) {
        operation->model_operation_failed = true;
        operation->done.store(true);
        return;
      }
      Configurator configurator;
      operation->result = configurator.UpdateWorkspace(false);
      operation->done.store(true);
    }).detach();
  } catch (...) {
    apply_operation_.reset();
    RestorePersistedSettings();
    SetApplyingUi(false);
    UpdateApplyButton();
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法启动后台应用任务，请稍后重试。",
                  L"無法啟動背景套用工作，請稍後重試。",
                  L"The background apply task could not start. Try again.")
            .c_str(),
        LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
        MB_OK | MB_ICONERROR);
    return false;
  }
  return true;
}

void SwitcherSettingsDialog::SetApplyingUi(bool applying) {
  for (int id :
       {IDC_SCHEMA_LIST, IDC_INPUT_MODE, IDC_SCHEMA_UPDATE_SETTINGS,
        IDC_MODEL_DOWNLOAD, IDC_MODEL_SECONDARY, IDC_CHECK_SCHEME_UPDATES}) {
    ::EnableWindow(GetDlgItem(id), applying ? FALSE : TRUE);
  }
  ::SetWindowTextW(GetDlgItem(IDOK),
                   LocalText(applying ? L"正在应用…" : L"应用",
                             applying ? L"正在套用…" : L"套用",
                             applying ? L"Applying…" : L"Apply")
                       .c_str());
  ::EnableWindow(GetDlgItem(IDOK), FALSE);
  if (!applying) {
    UpdateCheckButton();
    UpdateModelUi();
  }
  settings_navigation::NotifyHostStateChanged(m_hWnd);
}

void SwitcherSettingsDialog::FinishApply() {
  if (!apply_operation_ || !apply_operation_->done.load())
    return;
  const int deployment_result = apply_operation_->result;
  const PendingModelAction model_action = apply_operation_->model_action;
  const bool model_operation_failed = apply_operation_->model_operation_failed;
  const std::wstring model_operation_error =
      apply_operation_->model_operation_error;
  apply_operation_.reset();
  bool success = !model_operation_failed && deployment_result == 0;
  if (model_operation_failed) {
    const bool settings_restored = RestorePersistedSettings();
    model_operation_failed_ = true;
    std::wstring message = model_operation_error;
    if (!settings_restored) {
      message += LocalText(
          L"\n同时无法恢复先前的设置，请保留当前文件并查看部署日志。",
          L"\n同時無法恢復先前的設定，請保留目前檔案並查看部署記錄。",
          L"\nThe previous settings could not be restored. Keep the current "
          L"files and review the deployment log.");
    }
    if (!close_after_apply_) {
      ::MessageBoxW(m_hWnd, message.c_str(),
                    LocalText(L"无法移除语法模型", L"無法移除語法模型",
                              L"Unable to remove grammar model")
                        .c_str(),
                    MB_OK | MB_ICONERROR);
    } else {
      LOG(ERROR) << "Background model removal failed after the settings "
                    "window closed: "
                 << wtou8(message);
    }
  } else if (!success) {
    const bool had_pending_model = model_action != PendingModelAction::None;
    std::wstring rollback_error;
    const bool file_restored =
        !had_pending_model || model_manager_.Rollback(&rollback_error);
    const bool settings_restored = RestorePersistedSettings();
    Configurator configurator;
    const bool previous_state_deployed =
        configurator.UpdateWorkspace(false) == 0;
    const bool restored =
        file_restored && settings_restored && previous_state_deployed;
    if (restored) {
      pending_model_action_ = PendingModelAction::None;
      model_operation_failed_ = model_action == PendingModelAction::Install;
    } else if (had_pending_model) {
      model_operation_failed_ = true;
    }
    std::wstring message;
    if (!restored) {
      message = LocalText(
          L"重新部署和自动恢复均失败，请保留当前文件并查看部署日志。",
          L"重新部署和自動恢復均失敗，請保留目前檔案並查看部署記錄。",
          L"Deployment and automatic recovery both failed. Keep the current "
          L"files and review the deployment log.");
    } else if (model_action == PendingModelAction::Install) {
      message = LocalText(
          L"重新部署失败，设置和语法模型已经恢复。下载缓存已保留，可重试。",
          L"重新部署失敗，設定和語法模型已經恢復。下載快取已保留，可重試。",
          L"Deployment failed. Settings and the previous grammar model were "
          L"restored, and the download cache was kept.");
    } else if (model_action == PendingModelAction::Remove) {
      message = LocalText(
          L"重新部署失败，设置和语法模型已经恢复，可重试移除。",
          L"重新部署失敗，設定和語法模型已經恢復，可重試移除。",
          L"Deployment failed. Settings and the grammar model were restored; "
          L"the removal can be retried.");
    } else {
      message = LocalText(L"重新部署失败，设置已经恢复。",
                          L"重新部署失敗，設定已經恢復。",
                          L"Deployment failed and settings were restored.");
    }
    if (!rollback_error.empty())
      message += L"\n" + rollback_error;
    if (!close_after_apply_) {
      ::MessageBoxW(
          m_hWnd, message.c_str(),
          LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
          MB_OK | MB_ICONERROR);
    } else {
      LOG(ERROR) << "Background deployment failed after the settings window "
                    "closed: "
                 << wtou8(message);
    }
  }
  if (success && model_action != PendingModelAction::None) {
    std::wstring commit_error;
    if (!model_manager_.Commit(&commit_error)) {
      std::wstring rollback_error;
      const bool file_restored = model_manager_.Rollback(&rollback_error);
      const bool settings_restored = RestorePersistedSettings();
      Configurator configurator;
      const bool previous_state_deployed =
          configurator.UpdateWorkspace(false) == 0;
      const bool restored =
          file_restored && settings_restored && previous_state_deployed;
      if (restored) {
        pending_model_action_ = PendingModelAction::None;
        model_operation_failed_ = model_action == PendingModelAction::Install;
      } else {
        model_operation_failed_ = true;
      }
      std::wstring message =
          restored
              ? LocalText(
                    L"语法模型已经部署，但无法完成安装记录，原文件已经恢复。",
                    L"語法模型已經部署，但無法完成安裝記錄，原檔案已經恢復。",
                    L"The grammar model was deployed, but installation "
                    L"could not be finalized. The previous grammar model "
                    L"was restored.")
              : LocalText(
                    L"语法模型已经部署，但安装记录和自动恢复均失败，请查看部署"
                    L"日志"
                    L"。",
                    L"語法模型已經部署，但安裝記錄和自動恢復均失敗，請查看部署"
                    L"記錄"
                    L"。",
                    L"The grammar model was deployed, but finalization and "
                    L"automatic recovery failed. Review the deployment log.");
      if (!commit_error.empty())
        message += L"\n" + commit_error;
      if (!rollback_error.empty())
        message += L"\n" + rollback_error;
      if (!close_after_apply_) {
        ::MessageBoxW(
            m_hWnd, message.c_str(),
            LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
            MB_OK | MB_ICONERROR);
      } else {
        LOG(ERROR) << "Unable to finalize the background model deployment: "
                   << wtou8(message);
      }
      success = false;
    } else {
      pending_model_action_ = PendingModelAction::None;
      model_operation_failed_ = false;
      model_update_available_ = false;
      WanxiangUpdateManager::StoreAvailableCount(scheme_update_available_ ? 1u
                                                                          : 0u);
      UpdateCheckButton();
      UpdateLastCheckText();
      UpdateModelUi();
    }
  }
  if (success) {
    CommitAppliedBaseline();
    modified_ = false;
    input_mode_modified_ = false;
    update_frequency_modified_ = false;
  }
  SetApplyingUi(false);
  UpdateApplyButton();
  if (close_after_apply_) {
    KillTimer(kModelTimer);
    if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
      return;
    EndDialog(IDCANCEL);
  }
}

LRESULT SwitcherSettingsDialog::OnOK(WORD, WORD, HWND, BOOL&) {
  if (settings_navigation::RequestApply(m_hWnd))
    return 0;
  ApplyChanges();
  return 0;
}

LRESULT SwitcherSettingsDialog::OnSchemaListItemChanged(int,
                                                        LPNMHDR notification,
                                                        BOOL&) {
  auto* item = reinterpret_cast<LPNMLISTVIEW>(notification);
  if (!loaded_ || !item || item->iItem < 0 ||
      item->iItem >= schema_list_.GetItemCount()) {
    return 0;
  }
  const size_t index =
      static_cast<size_t>(schema_list_.GetItemData(item->iItem));
  if (index >= schemas_.size())
    return 0;

  if ((item->uNewState & LVIS_STATEIMAGEMASK) !=
      (item->uOldState & LVIS_STATEIMAGEMASK)) {
    schemas_[index].enabled = schema_list_.GetCheckState(item->iItem) != FALSE;
    modified_ = HasSchemaSelectionChanges() || input_mode_modified_ ||
                update_frequency_modified_;
    UpdateApplyButton();
    if (selected_schema_ == index)
      ShowDetails(index);
  }
  if ((item->uNewState & LVIS_SELECTED) && !(item->uOldState & LVIS_SELECTED)) {
    ShowDetails(index);
  }
  return 0;
}
