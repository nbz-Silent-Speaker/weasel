#include "stdafx.h"
#include "SwitcherSettingsDialog.h"

#include "Configurator.h"
#include "WanxiangUpdateManager.h"
#include "WeaselDeployer.h"

#include <algorithm>
#include <array>
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

struct UpdateSelection {
  bool model = false;
};

class PackageUpdateDialog : public CDialogImpl<PackageUpdateDialog> {
 public:
  enum { IDD = IDD_PACKAGE_UPDATES };

  explicit PackageUpdateDialog(const WanxiangUpdateManager::Result& result)
      : result_(result) {}

  BEGIN_MSG_MAP(PackageUpdateDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
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

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    HWND scheme = GetDlgItem(IDC_UPDATE_SCHEME);
    HWND model = GetDlgItem(IDC_UPDATE_MODEL);
    ::ShowWindow(GetDlgItem(IDC_UPDATE_SCHEME_GROUP),
                 result_.scheme_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(GetDlgItem(IDC_UPDATE_MODEL_GROUP),
                 result_.model_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(scheme, result_.scheme_update_available ? SW_SHOW : SW_HIDE);
    ::ShowWindow(model, result_.model_update_available ? SW_SHOW : SW_HIDE);
    if (!result_.scheme_update_available && result_.model_update_available) {
      MoveControl(IDC_UPDATE_MODEL_GROUP, 14, 28, 372, 36);
      MoveControl(IDC_UPDATE_MODEL, 24, 40, 350, 16);
      MoveControl(IDOK, 218, 72, 92, 18);
      MoveControl(IDCANCEL, 316, 72, 72, 18);
      ::ShowWindow(GetDlgItem(IDC_UPDATE_SUMMARY), SW_HIDE);
      ResizeClient(400, 102);
    } else if (result_.scheme_update_available &&
               !result_.model_update_available) {
      MoveControl(IDC_UPDATE_SUMMARY, 18, 70, 364, 16);
      MoveControl(IDCANCEL, 316, 92, 72, 18);
      ::ShowWindow(GetDlgItem(IDOK), SW_HIDE);
      ::SetWindowTextW(GetDlgItem(IDCANCEL),
                       Localize(L"关闭", L"關閉", L"Close").c_str());
      ResizeClient(400, 122);
    }
    if (result_.scheme_update_available) {
      std::wstring label = Localize(
          L"万象拼音 Lite（需由包含新版方案的安装包更新）",
          L"萬象拼音 Lite（需由包含新版方案的安裝程式更新）",
          L"Wanxiang Lite (update with a Weasel installer that includes it)");
      ::SetWindowTextW(scheme, label.c_str());
      ::EnableWindow(scheme, FALSE);
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
    ::SetDlgItemTextW(
        m_hWnd, IDC_UPDATE_SUMMARY,
        result_.scheme_update_available
            ? Localize(L"方案文件会保留当前安全版本；语法模型可在此直接更新。",
                       L"方案檔案會保留目前安全版本；語法模型可在此直接更新。",
                       L"Schema files stay on the current safe version. The "
                       L"grammar model can be updated here.")
                  .c_str()
            : L"");
    UpdateButton();
    CenterWindow(GetParent());
    return TRUE;
  }

  LRESULT OnSelectionChanged(WORD, WORD, HWND, BOOL&) {
    UpdateButton();
    return 0;
  }

  void UpdateButton() {
    const bool selected =
        ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_MODEL, BM_GETCHECK, 0, 0) ==
        BST_CHECKED;
    ::EnableWindow(GetDlgItem(IDOK), selected);
    ::SetWindowTextW(
        GetDlgItem(IDOK),
        Localize(selected ? L"更新所选（1）" : L"更新所选",
                 selected ? L"更新所選（1）" : L"更新所選",
                 selected ? L"Update selected (1)" : L"Update selected")
            .c_str());
  }

  LRESULT OnOK(WORD, WORD, HWND, BOOL&) {
    selection_.model = ::SendDlgItemMessageW(m_hWnd, IDC_UPDATE_MODEL,
                                             BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (selection_.model)
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
  if (!apply_operation_)
    ::EnableWindow(GetDlgItem(IDOK), HasPendingChanges());
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
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_NAME, schema.name.c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_SCHEMA_DETAIL_VERSION,
                    wanxiang ? WanxiangUpdateManager::kInstalledVersion : L"");
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
  const std::wstring version =
      wanxiang ? WanxiangUpdateManager::kInstalledVersion : L"";
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
  ApplyRoundedRegion(input_mode_, 5);
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
  for (const int id :
       {IDOK, IDC_CHECK_SCHEME_UPDATES, IDC_MODEL_DOWNLOAD, IDC_MODEL_SECONDARY,
        IDC_INPUT_MODE, IDC_SCHEMA_UPDATE_SETTINGS}) {
    ApplyRoundedRegion(GetDlgItem(id), 5);
  }
  ApplyRoundedRegion(GetDlgItem(IDC_SCHEMA_DESCRIPTION), 6);
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

void SwitcherSettingsDialog::UpdateModelUi() {
  const auto progress = model_manager_.GetProgress();
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
  ::ShowWindow(GetDlgItem(IDC_MODEL_SOURCE), SW_HIDE);
  const bool active = state == State::Downloading ||
                      state == State::WaitingRetry || state == State::Paused ||
                      state == State::Transferred || state == State::Error;
  model_status_active_ = (active && state != State::Downloading) ||
                         state == State::RestartRequired ||
                         model_operation_failed_ ||
                         pending_model_action_ != PendingModelAction::None ||
                         (state == State::Modified && !model_update_available_);
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS), active ? SW_SHOW : SW_HIDE);
  ::ShowWindow(GetDlgItem(IDC_MODEL_PROGRESS_TEXT), active ? SW_SHOW : SW_HIDE);
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
  set_text(IDC_MODEL_DOWNLOAD, primary);
  set_text(IDC_MODEL_SECONDARY, secondary);
  ::ShowWindow(GetDlgItem(IDC_MODEL_DOWNLOAD_STATUS),
               download_status.empty() ? SW_HIDE : SW_SHOW);
  ::EnableWindow(GetDlgItem(IDC_MODEL_DOWNLOAD), enabled);
  const auto move_control = [&](HWND control, const CRect& target) {
    CRect previous;
    ::GetWindowRect(control, &previous);
    ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&previous),
                      2);
    if (previous.left == target.left && previous.top == target.top &&
        previous.right == target.right && previous.bottom == target.bottom) {
      return;
    }
    ::SetWindowPos(control, nullptr, target.left, target.top, target.Width(),
                   target.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
    ::InvalidateRect(m_hWnd, &previous, TRUE);
  };
  const CRect& primary_rect =
      active ? model_active_primary_rect_ : model_primary_rect_;
  move_control(GetDlgItem(IDC_MODEL_DOWNLOAD), primary_rect);
  ::ShowWindow(GetDlgItem(IDC_MODEL_DOWNLOAD),
               primary.empty() ? SW_HIDE : SW_SHOW);
  const bool use_primary_slot = primary.empty() && (state == State::Installed ||
                                                    state == State::Modified);
  const CRect& secondary_rect = active ? model_active_secondary_rect_
                                : use_primary_slot ? model_primary_rect_
                                                   : model_secondary_rect_;
  move_control(GetDlgItem(IDC_MODEL_SECONDARY), secondary_rect);
  ::ShowWindow(GetDlgItem(IDC_MODEL_SECONDARY),
               secondary.empty() ? SW_HIDE : SW_SHOW);
  ApplyRoundedRegion(GetDlgItem(IDC_MODEL_DOWNLOAD), 5);
  ApplyRoundedRegion(GetDlgItem(IDC_MODEL_SECONDARY), 5);
  ::InvalidateRect(GetDlgItem(IDC_MODEL_DOWNLOAD), nullptr, TRUE);
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
  RECT height = {0, 0, 0, 10};
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
    const COLORREF background =
        ::GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    const COLORREF foreground =
        ::GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    HBRUSH brush = ::CreateSolidBrush(background);
    ::FillRect(draw->hDC, &draw->rcItem, brush);
    ::DeleteObject(brush);

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
    RECT padding = {0, 0, 5, 0};
    ::MapDialogRect(m_hWnd, &padding);
    text_rectangle.left += padding.right;
    text_rectangle.right -= padding.right;
    ::SetBkMode(draw->hDC, TRANSPARENT);
    ::SetTextColor(draw->hDC, foreground);
    HFONT font = reinterpret_cast<HFONT>(
        ::SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? ::SelectObject(draw->hDC, font) : nullptr;
    ::DrawTextW(
        draw->hDC, label.c_str(), -1, &text_rectangle,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old_font)
      ::SelectObject(draw->hDC, old_font);
    if ((draw->itemState & ODS_FOCUS) != 0 && !edit_portion)
      ::DrawFocusRect(draw->hDC, &draw->rcItem);
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
    ::FillRect(paint, &paint_area, ::GetSysColorBrush(COLOR_WINDOW));
    ::SetBkMode(paint, TRANSPARENT);
    ::SetTextColor(paint, ::GetSysColor(COLOR_GRAYTEXT));
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
    ::FillRect(paint, &area, ::GetSysColorBrush(COLOR_WINDOW));
    ::SetBkMode(paint, TRANSPARENT);
    ::SetTextColor(paint, ::GetSysColor(COLOR_WINDOWTEXT));
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
  const COLORREF window_color = ::GetSysColor(COLOR_WINDOW);
  const COLORREF border_color =
      blend(window_color, ::GetSysColor(COLOR_3DSHADOW), panel ? 36 : 48);
  RECT rectangle = draw->rcItem;
  ::FillRect(draw->hDC, &rectangle,
             ::GetSysColorBrush(panel ? COLOR_3DFACE : COLOR_WINDOW));
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
  HBRUSH brush =
      ::CreateSolidBrush(::GetSysColor(panel ? COLOR_WINDOW : COLOR_BTNFACE));
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
                                  ? ::GetSysColor(COLOR_WINDOWTEXT)
                                  : ::GetSysColor(COLOR_GRAYTEXT));
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
                   ::GetSysColor(COLOR_WINDOWTEXT));
    ::SetBkMode(reinterpret_cast<HDC>(device_context), TRANSPARENT);
    return reinterpret_cast<LRESULT>(description_brush_.m_hBrush);
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
                     ::GetSysColor(COLOR_GRAYTEXT));
    }
    ::SetBkMode(reinterpret_cast<HDC>(device_context), TRANSPARENT);
    return reinterpret_cast<LRESULT>(
        ::GetSysColorBrush(card ? COLOR_WINDOW : COLOR_3DFACE));
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
  LOGFONTW base_font = {};
  ::GetObjectW(GetFont(), sizeof(base_font), &base_font);
  LOGFONTW title = base_font;
  title.lfHeight = title.lfHeight * 4 / 3;
  title.lfWeight = FW_SEMIBOLD;
  if (title_font_.CreateFontIndirect(&title))
    CWindow(GetDlgItem(IDC_SWITCHER_TITLE)).SetFont(title_font_);
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
  schema_list_.SetBkColor(::GetSysColor(COLOR_WINDOW));
  schema_list_.SetTextBkColor(::GetSysColor(COLOR_WINDOW));

  CString schema_name;
  schema_name.LoadStringW(IDS_STR_SCHEMA_NAME);
  schema_list_.AddColumn(schema_name, 0);
  CRect rect;
  schema_list_.GetClientRect(&rect);
  schema_list_.SetColumnWidth(0, rect.Width() - 24);

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
      blend(::GetSysColor(COLOR_WINDOW), ::GetSysColor(COLOR_3DFACE), 35));
  model_progress_.Attach(GetDlgItem(IDC_MODEL_PROGRESS));
  model_progress_.SetRange32(0, 1000);
  input_mode_.Attach(GetDlgItem(IDC_INPUT_MODE));
  update_frequency_.Attach(GetDlgItem(IDC_SCHEMA_UPDATE_SETTINGS));
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
  RECT active_secondary = {392, 206, 448, 224};
  RECT active_primary = {456, 206, 512, 224};
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

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (apply_operation_) {
    close_after_apply_ = true;
    ShowWindow(SW_HIDE);
    return 0;
  }
  if (!ConfirmDiscardChanges())
    return 0;
  DiscardPendingModel();
  KillTimer(kModelTimer);
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (apply_operation_) {
    close_after_apply_ = true;
    ShowWindow(SW_HIDE);
    return 0;
  }
  if (!ConfirmDiscardChanges())
    return 0;
  DiscardPendingModel();
  KillTimer(kModelTimer);
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnTimer(UINT, WPARAM timer, LPARAM, BOOL&) {
  if (timer != kModelTimer)
    return 0;
  FinishApply();
  if (apply_operation_)
    return 0;
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
             schemas_[selected_schema_].id == "wanxiang_lite") {
    UpdateModelUi();
  }
  return 0;
}

LRESULT SwitcherSettingsDialog::OnCheckUpdates(WORD, WORD, HWND, BOOL&) {
  if (scheme_update_available_ || model_update_available_) {
    ShowUpdateList();
    return 0;
  }
  if (update_check_)
    return 0;
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
    latest_model_sha256_ = result.latest_model_sha256;
    latest_model_size_ = result.latest_model_size;
    if (!latest_model_sha256_.empty() && latest_model_size_)
      model_manager_.ConfigureTarget(latest_model_sha256_, latest_model_size_);
    if (selected_schema_ < schemas_.size() &&
        schemas_[selected_schema_].id == "wanxiang_lite") {
      UpdateModelUi();
    }
  }
  UpdateCheckButton();
  UpdateLastCheckText();
  return;
}

void SwitcherSettingsDialog::UpdateCheckButton() {
  HWND button = GetDlgItem(IDC_CHECK_SCHEME_UPDATES);
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
  result.latest_model_sha256 = latest_model_sha256_;
  result.latest_model_size = latest_model_size_;
  PackageUpdateDialog dialog(result);
  if (dialog.DoModal(m_hWnd) != IDOK || !dialog.selection().model)
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
    EndDialog(IDCANCEL);
  }
}

LRESULT SwitcherSettingsDialog::OnOK(WORD, WORD, HWND, BOOL&) {
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
