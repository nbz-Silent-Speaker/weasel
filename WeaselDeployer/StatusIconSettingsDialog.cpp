#include "stdafx.h"
#include "StatusIconSettingsDialog.h"
#include "InputMethodIcon.h"

#include <WeaselUtility.h>
#include <rime_api.h>
#include <rime_levers_api.h>

#include <algorithm>
#include <bcrypt.h>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <gdiplus.h>
#include <set>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {
constexpr UINT kServerEnglishIcon = 101;
constexpr UINT kServerChineseIcon = 102;
constexpr UINT kServerCapsIcon = 104;

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

std::wstring UiText(const wchar_t* simplified,
                    const wchar_t* traditional,
                    const wchar_t* english) {
  return IsChinese() ? (IsSimplifiedChinese() ? simplified : traditional)
                     : english;
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

HICON LoadBuiltInIcon(bool english, bool caps = false) {
  wchar_t module_path[MAX_PATH] = {};
  if (::GetModuleFileNameW(nullptr, module_path, MAX_PATH)) {
    std::filesystem::path server(module_path);
    server.replace_filename(L"WeaselServer.exe");
    HMODULE module =
        ::LoadLibraryExW(server.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module) {
      const UINT resource =
          caps ? kServerCapsIcon
               : (english ? kServerEnglishIcon : kServerChineseIcon);
      HICON icon = reinterpret_cast<HICON>(::LoadImageW(
          module, MAKEINTRESOURCEW(resource), IMAGE_ICON, 32, 32, 0));
      ::FreeLibrary(module);
      if (icon)
        return icon;
    }
    server.replace_filename(caps ? L"caps.ico"
                                 : (english ? L"en.ico" : L"zh.ico"));
    if (HICON icon = LoadIconFile(server.wstring()))
      return icon;
  }
  return ::CopyIcon(::LoadIconW(nullptr, IDI_APPLICATION));
}

bool IsSupportedIcon(const std::filesystem::path& path) {
  std::wstring extension = path.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t character) {
                   return static_cast<wchar_t>(std::towlower(character));
                 });
  return extension == L".png" || extension == L".ico";
}

std::filesystem::path StatusIconRoot() {
  return WeaselUserDataPath() / L"icons" / L"status";
}

std::filesystem::path StatusIconLibrary() {
  return StatusIconRoot() / L"library";
}

std::filesystem::path BundledStatusIconDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  if (!::GetModuleFileNameW(nullptr, module_path, MAX_PATH))
    return {};
  return std::filesystem::path(module_path).parent_path() / L"icons" /
         L"status";
}

std::wstring NormalizePath(const std::filesystem::path& path) {
  std::error_code error;
  auto normalized = std::filesystem::absolute(path, error).lexically_normal();
  std::wstring value = (error ? path.lexically_normal() : normalized).wstring();
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t character) {
                   return static_cast<wchar_t>(std::towlower(character));
                 });
  return value;
}

bool SamePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
  if (left.empty() || right.empty())
    return left.empty() && right.empty();
  return NormalizePath(left) == NormalizePath(right);
}

bool IsLibraryIcon(const std::filesystem::path& path) {
  return !path.empty() && SamePath(path.parent_path(), StatusIconLibrary());
}

bool IsBundledStatusIcon(const std::filesystem::path& path) {
  const std::filesystem::path bundled = BundledStatusIconDirectory();
  return !path.empty() && !bundled.empty() &&
         SamePath(path.parent_path(), bundled);
}

std::wstring HashFile(const std::filesystem::path& path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  HANDLE file = INVALID_HANDLE_VALUE;
  std::vector<UCHAR> object;
  std::vector<UCHAR> digest;
  std::wstring result;
  do {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0)
      break;
    DWORD object_length = 0;
    DWORD digest_length = 0;
    DWORD received = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &received, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&digest_length),
                          sizeof(digest_length), &received, 0) < 0)
      break;
    object.resize(object_length);
    digest.resize(digest_length);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_length,
                         nullptr, 0, 0) < 0)
      break;
    file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
      break;
    std::array<UCHAR, 64 * 1024> buffer{};
    DWORD read = 0;
    bool failed = false;
    while (::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &read, nullptr) &&
           read != 0) {
      if (BCryptHashData(hash, buffer.data(), read, 0) < 0) {
        failed = true;
        break;
      }
    }
    if (failed || BCryptFinishHash(hash, digest.data(), digest_length, 0) < 0)
      break;
    wchar_t encoded[13]{};
    for (size_t index = 0; index < 6; ++index)
      swprintf_s(encoded + index * 2, 3, L"%02x", digest[index]);
    result = encoded;
  } while (false);
  if (file != INVALID_HANDLE_VALUE)
    ::CloseHandle(file);
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  return result;
}

std::wstring SafeIconStem(std::wstring stem) {
  for (wchar_t& character : stem) {
    if (!std::iswalnum(character) && character != L'-' && character != L'_')
      character = L'_';
  }
  while (!stem.empty() && (stem.front() == L'_' || stem.front() == L' '))
    stem.erase(stem.begin());
  while (!stem.empty() && (stem.back() == L'_' || stem.back() == L' '))
    stem.pop_back();
  if (stem.empty())
    stem = L"icon";
  if (stem.size() > 36)
    stem.resize(36);
  return stem;
}

std::filesystem::path StoreIconInLibrary(const std::filesystem::path& source,
                                         std::wstring* error) {
  if (source.empty())
    return {};
  if (!IsSupportedIcon(source)) {
    *error = UiText(L"请选择 PNG 或 ICO 图标。", L"請選擇 PNG 或 ICO 圖示。",
                    L"Choose a PNG or ICO icon.");
    return {};
  }
  HICON validation = LoadIconFile(source.wstring());
  if (!validation) {
    *error = UiText(L"所选文件不是有效图标。", L"所選檔案不是有效圖示。",
                    L"The selected file is not a valid icon.");
    return {};
  }
  ::DestroyIcon(validation);
  if (IsLibraryIcon(source) || IsBundledStatusIcon(source))
    return source;

  const std::wstring digest = HashFile(source);
  if (digest.empty()) {
    *error = UiText(L"无法读取所选图标。", L"無法讀取所選圖示。",
                    L"Could not read the selected icon.");
    return {};
  }
  std::wstring extension = source.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t character) {
                   return static_cast<wchar_t>(std::towlower(character));
                 });
  const std::filesystem::path library = StatusIconLibrary();
  std::error_code code;
  std::filesystem::create_directories(library, code);
  if (code) {
    *error = UiText(L"无法创建状态图标库。", L"無法建立狀態圖示庫。",
                    L"Could not create the status icon library.");
    return {};
  }
  for (std::filesystem::directory_iterator iterator(library, code), end;
       !code && iterator != end; iterator.increment(code)) {
    if (!iterator->is_regular_file(code) ||
        _wcsicmp(iterator->path().extension().c_str(), extension.c_str()) != 0)
      continue;
    const std::wstring stem = iterator->path().stem().wstring();
    if (stem.size() > digest.size() &&
        stem.compare(stem.size() - digest.size(), digest.size(), digest) == 0 &&
        stem[stem.size() - digest.size() - 1] == L'-')
      return iterator->path();
  }
  code.clear();
  const std::filesystem::path destination =
      library /
      (SafeIconStem(source.stem().wstring()) + L"-" + digest + extension);
  if (SamePath(source, destination) || std::filesystem::exists(destination))
    return destination;

  std::filesystem::path temporary = destination;
  temporary += L".tmp";
  std::filesystem::copy_file(source, temporary,
                             std::filesystem::copy_options::overwrite_existing,
                             code);
  if (!code)
    std::filesystem::rename(temporary, destination, code);
  if (code) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (std::filesystem::exists(destination))
      return destination;
    *error = UiText(L"无法将图标保存到用户文件夹。",
                    L"無法將圖示儲存到使用者資料夾。",
                    L"Could not save the icon in the user folder.");
    return {};
  }
  return destination;
}

bool BrowseIconFile(HWND owner,
                    std::filesystem::path* path,
                    bool ico_only = false) {
  wchar_t file[32768]{};
  const std::wstring initial = WeaselUserDataPath().wstring();
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter =
      ico_only ? L"ICO\0*.ico\0"
               : L"PNG / ICO\0*.png;*.ico\0PNG\0*.png\0ICO\0*.ico\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = static_cast<DWORD>(std::size(file));
  dialog.lpstrInitialDir = initial.c_str();
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!::GetOpenFileNameW(&dialog))
    return false;
  *path = file;
  return true;
}

std::wstring IconDisplayName(const std::filesystem::path& path) {
  std::wstring name = path.stem().wstring();
  if (name.size() > 13 && name[name.size() - 13] == L'-') {
    const std::wstring suffix = name.substr(name.size() - 12);
    if (std::all_of(suffix.begin(), suffix.end(), [](wchar_t character) {
          return std::iswxdigit(character) != 0;
        }))
      name.resize(name.size() - 13);
  }
  return name;
}

#pragma pack(push, 2)
struct PickerDialogTemplate {
  DLGTEMPLATE dialog{};
  WORD menu = 0;
  WORD window_class = 0;
  wchar_t title[1]{};
  WORD point_size = 9;
  wchar_t typeface[9] = L"Segoe UI";
};
#pragma pack(pop)

class StatusIconPicker {
 public:
  StatusIconPicker(HWND owner,
                   std::wstring current,
                   bool english,
                   bool caps,
                   bool inherit_global,
                   std::wstring inherited_default,
                   std::vector<std::wstring> protected_paths)
      : owner_(owner),
        current_(std::move(current)),
        english_(english),
        caps_(caps),
        inherit_global_(inherit_global),
        inherited_default_(std::move(inherited_default)),
        protected_(std::move(protected_paths)) {}

  bool Show(std::wstring* selected) {
    PickerDialogTemplate dialog_template;
    dialog_template.dialog.style =
        DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dialog_template.dialog.dwExtendedStyle = WS_EX_CONTROLPARENT;
    dialog_template.dialog.cdit = 0;
    dialog_template.dialog.x = 0;
    dialog_template.dialog.y = 0;
    dialog_template.dialog.cx = 390;
    dialog_template.dialog.cy = 250;
    const INT_PTR result = ::DialogBoxIndirectParamW(
        ::GetModuleHandleW(nullptr), &dialog_template.dialog, owner_,
        DialogProc, reinterpret_cast<LPARAM>(this));
    if (result != IDOK)
      return false;
    *selected = result_;
    return true;
  }

 private:
  struct Item {
    std::wstring path;
    std::wstring label;
    bool removable = false;
  };

  enum : WORD {
    kList = 41001,
    kImport = 41002,
    kDelete = 41003,
  };

  static INT_PTR CALLBACK DialogProc(HWND dialog,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
    auto* self = reinterpret_cast<StatusIconPicker*>(
        ::GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
      self = reinterpret_cast<StatusIconPicker*>(lparam);
      ::SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
      return self->Initialize(dialog);
    }
    if (!self)
      return FALSE;
    if (message == WM_COMMAND) {
      switch (LOWORD(wparam)) {
        case kImport:
          self->Import();
          return TRUE;
        case kDelete:
          self->Delete();
          return TRUE;
        case IDOK:
          self->Accept();
          return TRUE;
        case IDCANCEL:
          ::EndDialog(dialog, IDCANCEL);
          return TRUE;
      }
    } else if (message == WM_NOTIFY) {
      const auto* notification = reinterpret_cast<NMHDR*>(lparam);
      if (notification->idFrom == kList) {
        if (notification->code == LVN_ITEMCHANGED) {
          self->RefreshButtons();
          return TRUE;
        }
        if (notification->code == NM_DBLCLK) {
          self->Accept();
          return TRUE;
        }
      }
    } else if (message == WM_CLOSE) {
      ::EndDialog(dialog, IDCANCEL);
      return TRUE;
    } else if (message == WM_DESTROY) {
      if (self->images_) {
        ::ImageList_Destroy(self->images_);
        self->images_ = nullptr;
      }
    }
    return FALSE;
  }

  HWND CreateControl(const wchar_t* class_name,
                     const wchar_t* text,
                     DWORD style,
                     DWORD extended_style,
                     WORD id,
                     int x,
                     int y,
                     int width,
                     int height) {
    RECT bounds{x, y, x + width, y + height};
    ::MapDialogRect(dialog_, &bounds);
    HWND control = ::CreateWindowExW(
        extended_style, class_name, text, WS_CHILD | WS_VISIBLE | style,
        bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, dialog_, reinterpret_cast<HMENU>(id),
        ::GetModuleHandleW(nullptr), nullptr);
    if (control)
      ::SendMessageW(control, WM_SETFONT,
                     ::SendMessageW(dialog_, WM_GETFONT, 0, 0), TRUE);
    return control;
  }

  BOOL Initialize(HWND dialog) {
    dialog_ = dialog;
    ::SetWindowTextW(
        dialog_, UiText(L"选择状态图标", L"選擇狀態圖示", L"Choose status icon")
                     .c_str());
    CreateControl(L"STATIC",
                  UiText(L"选择以前导入的图标，或导入一个新图标。",
                         L"選擇以前匯入的圖示，或匯入一個新圖示。",
                         L"Choose an imported icon or import a new one.")
                      .c_str(),
                  SS_LEFT, 0, 0, 14, 12, 362, 12);
    list_ = CreateControl(WC_LISTVIEWW, L"",
                          LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                              LVS_AUTOARRANGE | WS_TABSTOP,
                          WS_EX_CLIENTEDGE, kList, 14, 30, 362, 170);
    CreateControl(
        L"BUTTON",
        UiText(L"导入新图标…", L"匯入新圖示…", L"Import new…").c_str(),
        BS_PUSHBUTTON | WS_TABSTOP, 0, kImport, 14, 218, 82, 18);
    CreateControl(L"BUTTON", UiText(L"删除", L"刪除", L"Delete").c_str(),
                  BS_PUSHBUTTON | WS_TABSTOP, 0, kDelete, 102, 218, 54, 18);
    CreateControl(L"BUTTON",
                  UiText(L"使用此图标", L"使用此圖示", L"Use icon").c_str(),
                  BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDOK, 242, 218, 64, 18);
    CreateControl(L"BUTTON", UiText(L"取消", L"取消", L"Cancel").c_str(),
                  BS_PUSHBUTTON | WS_TABSTOP, 0, IDCANCEL, 312, 218, 64, 18);
    const std::array<WORD, 4> action_ids = {
        kImport, kDelete, static_cast<WORD>(IDOK), static_cast<WORD>(IDCANCEL)};
    for (WORD id : action_ids)
      settings_navigation::StyleActionButton(dialog_, id);
    settings_navigation::StyleInput(dialog_, kList);
    ListView_SetExtendedListViewStyle(
        list_, LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);
    HDC dc = ::GetDC(dialog_);
    const UINT dpi =
        dc ? static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc)
      ::ReleaseDC(dialog_, dc);
    const int image_size = (std::max)(32, ::MulDiv(36, dpi, 96));
    images_ = ::ImageList_Create(image_size, image_size, ILC_COLOR32 | ILC_MASK,
                                 8, 8);
    ListView_SetImageList(list_, images_, LVSIL_NORMAL);
    ListView_SetIconSpacing(list_, ::MulDiv(82, dpi, 96),
                            ::MulDiv(68, dpi, 96));
    Populate(current_);

    RECT owner_bounds{};
    RECT bounds{};
    ::GetWindowRect(owner_, &owner_bounds);
    ::GetWindowRect(dialog_, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    ::SetWindowPos(dialog_, nullptr,
                   owner_bounds.left +
                       (owner_bounds.right - owner_bounds.left - width) / 2,
                   owner_bounds.top +
                       (owner_bounds.bottom - owner_bounds.top - height) / 2,
                   0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetFocus(list_);
    return FALSE;
  }

  void AddCandidate(std::vector<std::filesystem::path>* paths,
                    const std::filesystem::path& candidate) const {
    if (candidate.empty() || !IsSupportedIcon(candidate) ||
        !std::filesystem::is_regular_file(candidate) ||
        IsBundledStatusIcon(candidate))
      return;
    if (std::none_of(paths->begin(), paths->end(), [&](const auto& existing) {
          return SamePath(existing, candidate);
        }))
      paths->push_back(candidate);
  }

  void EnumerateDirectory(std::vector<std::filesystem::path>* paths,
                          const std::filesystem::path& directory) const {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error)) {
      if (iterator->is_regular_file(error))
        AddCandidate(paths, iterator->path());
    }
  }

  void Populate(const std::wstring& preferred) {
    ListView_DeleteAllItems(list_);
    ::ImageList_RemoveAll(images_);
    items_.clear();

    HICON built_in =
        inherit_global_ ? LoadIconFile(inherited_default_) : nullptr;
    if (!built_in)
      built_in = LoadBuiltInIcon(english_, caps_);
    const int built_in_image = ::ImageList_AddIcon(images_, built_in);
    if (built_in)
      ::DestroyIcon(built_in);
    items_.push_back(
        {L"",
         inherit_global_
             ? UiText(L"使用全局", L"使用全域", L"Use global")
             : UiText(L"内置默认", L"內建預設", L"Built-in default"),
         false});
    InsertListItem(0, built_in_image);

    int selected = preferred.empty() ? 0 : -1;
    const std::filesystem::path bundled = BundledStatusIconDirectory();
    const auto add_bundled =
        [&](const wchar_t* filename, const wchar_t* simplified,
            const wchar_t* traditional, const wchar_t* english) {
          const std::filesystem::path path = bundled / filename;
          HICON icon = LoadIconFile(path.wstring());
          if (!icon)
            return;
          const int image = ::ImageList_AddIcon(images_, icon);
          ::DestroyIcon(icon);
          const int index = static_cast<int>(items_.size());
          items_.push_back({path.wstring(),
                            UiText(simplified, traditional, english), false});
          InsertListItem(index, image);
          if (SamePath(path, preferred))
            selected = index;
        };
    if (caps_) {
      add_bundled(L"caps-blue.ico", L"内置 · 蓝色", L"內建 · 藍色",
                  L"Built-in · Blue");
      add_bundled(L"caps-red.ico", L"内置 · 红色", L"內建 · 紅色",
                  L"Built-in · Red");
    } else if (english_) {
      add_bundled(L"ascii-black.ico", L"内置 · 黑色", L"內建 · 黑色",
                  L"Built-in · Black");
      add_bundled(L"ascii-red.ico", L"内置 · 红色", L"內建 · 紅色",
                  L"Built-in · Red");
    } else {
      add_bundled(L"chinese-black.ico", L"内置 · 黑色", L"內建 · 黑色",
                  L"Built-in · Black");
      add_bundled(L"chinese-blue.ico", L"内置 · 蓝色", L"內建 · 藍色",
                  L"Built-in · Blue");
    }

    std::vector<std::filesystem::path> paths;
    AddCandidate(&paths, current_);
    EnumerateDirectory(&paths, StatusIconLibrary());
    // Keep the last files created by older versions visible until they are
    // selected and migrated into the library on Apply.
    EnumerateDirectory(&paths, StatusIconRoot());
    std::sort(
        paths.begin(), paths.end(), [](const auto& left, const auto& right) {
          return _wcsicmp(left.filename().c_str(), right.filename().c_str()) <
                 0;
        });

    for (const auto& path : paths) {
      HICON icon = LoadIconFile(path.wstring());
      if (!icon)
        continue;
      const int image = ::ImageList_AddIcon(images_, icon);
      ::DestroyIcon(icon);
      const int index = static_cast<int>(items_.size());
      items_.push_back(
          {path.wstring(), IconDisplayName(path), IsLibraryIcon(path)});
      InsertListItem(index, image);
      if (SamePath(path, preferred))
        selected = index;
    }
    if (selected < 0)
      selected = 0;
    ListView_SetItemState(list_, selected, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_, selected, FALSE);
    RefreshButtons();
  }

  void InsertListItem(int index, int image) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_IMAGE;
    item.iItem = index;
    item.iImage = image;
    item.pszText = items_[index].label.data();
    ListView_InsertItem(list_, &item);
  }

  int SelectedIndex() const {
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
  }

  bool IsProtected(const std::wstring& path) const {
    return std::any_of(protected_.begin(), protected_.end(),
                       [&](const std::wstring& protected_path) {
                         return SamePath(path, protected_path);
                       });
  }

  void RefreshButtons() {
    const int selected = SelectedIndex();
    const bool valid =
        selected >= 0 && selected < static_cast<int>(items_.size());
    ::EnableWindow(::GetDlgItem(dialog_, IDOK), valid);
    const bool removable = valid && items_[selected].removable &&
                           !IsProtected(items_[selected].path);
    ::EnableWindow(::GetDlgItem(dialog_, kDelete), removable);
  }

  void Import() {
    std::filesystem::path source;
    if (!BrowseIconFile(dialog_, &source))
      return;
    std::wstring error;
    const auto stored = StoreIconInLibrary(source, &error);
    if (stored.empty()) {
      ::MessageBoxW(dialog_, error.c_str(),
                    UiText(L"导入失败", L"匯入失敗", L"Import failed").c_str(),
                    MB_OK | MB_ICONERROR);
      return;
    }
    Populate(stored.wstring());
  }

  void Delete() {
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(items_.size()) ||
        !items_[selected].removable)
      return;
    if (IsProtected(items_[selected].path)) {
      ::MessageBoxW(
          dialog_,
          UiText(L"这个图标正在使用，应用其他图标后才能删除。",
                 L"這個圖示正在使用，套用其他圖示後才能刪除。",
                 L"This icon is in use. Apply another icon before deleting it.")
              .c_str(),
          UiText(L"无法删除", L"無法刪除", L"Cannot delete").c_str(),
          MB_OK | MB_ICONINFORMATION);
      return;
    }
    if (::MessageBoxW(
            dialog_,
            UiText(L"从图标库中删除这个图标？", L"從圖示庫中刪除這個圖示？",
                   L"Delete this icon from the library?")
                .c_str(),
            UiText(L"删除图标", L"刪除圖示", L"Delete icon").c_str(),
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
      return;
    std::error_code error;
    std::filesystem::remove(items_[selected].path, error);
    if (error) {
      ::MessageBoxW(dialog_,
                    UiText(L"无法删除图标文件。", L"無法刪除圖示檔案。",
                           L"Could not delete the icon file.")
                        .c_str(),
                    UiText(L"删除失败", L"刪除失敗", L"Delete failed").c_str(),
                    MB_OK | MB_ICONERROR);
      return;
    }
    Populate(L"");
  }

  void Accept() {
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(items_.size()))
      return;
    result_ = items_[selected].path;
    ::EndDialog(dialog_, IDOK);
  }

  HWND owner_ = nullptr;
  HWND dialog_ = nullptr;
  HWND list_ = nullptr;
  HIMAGELIST images_ = nullptr;
  std::wstring current_;
  std::wstring result_;
  bool english_ = false;
  bool caps_ = false;
  bool inherit_global_ = false;
  std::wstring inherited_default_;
  std::vector<std::wstring> protected_;
  std::vector<Item> items_;
};

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

HWND CreateStatusControl(HWND dialog,
                         const wchar_t* class_name,
                         DWORD style,
                         UINT id) {
  HWND control = ::CreateWindowExW(
      0, class_name, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, dialog,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      ::GetModuleHandleW(nullptr), nullptr);
  if (control)
    ::SendMessageW(control, WM_SETFONT,
                   ::SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
  return control;
}

void EnsureStatusControls(HWND dialog) {
  CreateStatusControl(dialog, L"BUTTON", BS_OWNERDRAW, IDC_INPUT_METHOD_CARD);
  for (UINT id : {IDC_INPUT_METHOD_TITLE, IDC_INPUT_METHOD_HINT})
    CreateStatusControl(dialog, L"STATIC", SS_LEFT, id);
  CreateStatusControl(dialog, L"STATIC", SS_ICON | SS_CENTERIMAGE,
                      IDC_INPUT_METHOD_ICON);
  for (UINT id : {IDC_INPUT_METHOD_CHANGE, IDC_INPUT_METHOD_RESTORE})
    CreateStatusControl(dialog, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, id);
  if (!::GetDlgItem(dialog, IDC_STATUS_SCHEMA_COMBO)) {
    CreateStatusControl(dialog, WC_COMBOBOXW,
                        CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE |
                            CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
                        IDC_STATUS_SCHEMA_COMBO);
  }
  for (UINT id : {IDC_STATUS_CHINESE_INHERIT, IDC_STATUS_ASCII_INHERIT,
                  IDC_STATUS_CAPS_INHERIT}) {
    if (!::GetDlgItem(dialog, id))
      CreateStatusControl(dialog, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, id);
  }
  for (UINT id : {IDC_STATUS_PREVIEW_LIGHT, IDC_STATUS_PREVIEW_DARK}) {
    if (!::GetDlgItem(dialog, id))
      CreateStatusControl(dialog, L"BUTTON", BS_AUTORADIOBUTTON | WS_TABSTOP,
                          id);
  }
}

void LayoutStatusIconPage(HWND dialog) {
  using settings_navigation::MoveControl;
  constexpr int kToggleWidthDlu = 46;
  MoveControl(dialog, IDC_STATUS_RESTORE,
              settings_navigation::kBottomActionLeftDlu,
              settings_navigation::kBottomActionTopDlu,
              settings_navigation::kActionButtonWidthDlu,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_STATUS_BASE_CARD, settings_navigation::kPageInsetDlu,
              settings_navigation::kFirstCardTopDlu,
              settings_navigation::kPageBodyWidthDlu, 110);
  MoveControl(dialog, IDC_STATUS_BASE_TITLE, 26, 22, 210, 12);
  MoveControl(dialog, IDC_STATUS_CAPS_AUTOMATIC, 512 - kToggleWidthDlu * 2, 21,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_CAPS_CUSTOM, 512 - kToggleWidthDlu, 21,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_CAPS_TITLE, 28, 45, 130, 12);
  MoveControl(dialog, IDC_STATUS_SCHEMA_COMBO, 310, 40, 202, 100);
  const std::array<UINT, 3> labels = {IDC_STATUS_CHINESE_LABEL,
                                      IDC_STATUS_ENGLISH_LABEL,
                                      IDC_STATUS_CHINESE_CAPS_LABEL};
  const std::array<UINT, 3> icons = {IDC_STATUS_CHINESE_ICON,
                                     IDC_STATUS_ENGLISH_ICON,
                                     IDC_STATUS_CHINESE_CAPS_ICON};
  const std::array<UINT, 3> changes = {IDC_STATUS_CHINESE_CHANGE,
                                       IDC_STATUS_ENGLISH_CHANGE,
                                       IDC_STATUS_CHINESE_CAPS_CHANGE};
  const std::array<UINT, 3> inherits = {IDC_STATUS_CHINESE_INHERIT,
                                        IDC_STATUS_ASCII_INHERIT,
                                        IDC_STATUS_CAPS_INHERIT};
  for (size_t index = 0; index < labels.size(); ++index) {
    const int top = 59 + static_cast<int>(index) * 20;
    MoveControl(dialog, labels[index], 28, top + 5, 160, 12);
    MoveControl(dialog, inherits[index], 310, top + 1, 76,
                settings_navigation::kButtonHeightDlu);
    MoveControl(dialog, icons[index], 396, top, 20, 20);
    MoveControl(dialog, changes[index], 432, top + 1, 80,
                settings_navigation::kButtonHeightDlu);
  }

  MoveControl(dialog, IDC_INPUT_METHOD_CARD, settings_navigation::kPageInsetDlu,
              130, settings_navigation::kPageBodyWidthDlu, 34);
  MoveControl(dialog, IDC_INPUT_METHOD_TITLE, 26, 134, 220, 12);
  MoveControl(dialog, IDC_INPUT_METHOD_HINT, 28, 149, 275, 10);
  MoveControl(dialog, IDC_INPUT_METHOD_RESTORE, 310, 138, 76,
              settings_navigation::kButtonHeightDlu);
  MoveControl(dialog, IDC_INPUT_METHOD_ICON, 396, 137, 20, 20);
  MoveControl(dialog, IDC_INPUT_METHOD_CHANGE, 432, 138, 80,
              settings_navigation::kButtonHeightDlu);

  MoveControl(dialog, IDC_STATUS_TASKBAR_CARD,
              settings_navigation::kPageInsetDlu, 170,
              settings_navigation::kPageBodyWidthDlu,
              settings_navigation::kPageCardsBottomDlu - 170);
  MoveControl(dialog, IDC_STATUS_TASKBAR_TITLE, 26, 178, 100, 12);
  constexpr int kPreviewGroupGapDlu = 8;
  MoveControl(dialog, IDC_STATUS_PREVIEW_CHINESE,
              512 - kToggleWidthDlu * 5 - kPreviewGroupGapDlu, 177,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_ENGLISH,
              512 - kToggleWidthDlu * 4 - kPreviewGroupGapDlu, 177,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_CHINESE_CAPS,
              512 - kToggleWidthDlu * 3 - kPreviewGroupGapDlu, 177,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_LIGHT, 512 - kToggleWidthDlu * 2, 177,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
  MoveControl(dialog, IDC_STATUS_PREVIEW_DARK, 512 - kToggleWidthDlu, 177,
              kToggleWidthDlu, settings_navigation::kCompactToggleHeightDlu);
}

RECT TaskbarPreviewIconBounds(HWND dialog, HWND card) {
  RECT bounds{};
  if (!card || !::GetClientRect(card, &bounds))
    return bounds;
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  const RECT units = settings_navigation::MapDialogUnits(dialog, 0, 0, 10, 26);
  const int padding = units.right;
  const int scene_top = units.bottom;
  const int scene_bottom = height - padding / 2;
  if (width <= padding * 2 || scene_bottom <= scene_top)
    return {};
  const int scene_right = width - padding;
  const int scene_height = scene_bottom - scene_top;
  const int taskbar_height = (std::max)(18, scene_height * 2 / 5);
  const int center_y = scene_bottom - taskbar_height / 2;
  HDC dpi_dc = ::GetDC(card);
  const int dpi = dpi_dc ? ::GetDeviceCaps(dpi_dc, LOGPIXELSX) : 96;
  if (dpi_dc)
    ::ReleaseDC(card, dpi_dc);
  const int effective_dpi = dpi > 0 ? dpi : 96;
  const int edge_inset = (std::max)(8, ::MulDiv(8, effective_dpi, 96));
  const int preferred_icon_size = ::MulDiv(16, effective_dpi, 96);
  const int icon_size =
      (std::min)((std::max)(16, preferred_icon_size), taskbar_height - 8);
  const int preferred_system_icon_size = ::MulDiv(16, effective_dpi, 96);
  const int system_icon_size = (std::min)(
      (std::max)(16, preferred_system_icon_size), taskbar_height - 8);
  const int system_icon_gap = (std::max)(4, ::MulDiv(4, effective_dpi, 96));
  const int input_group_gap = (std::max)(8, ::MulDiv(8, effective_dpi, 96));
  const int clock_slot = (std::max)(76, ::MulDiv(76, effective_dpi, 96));
  const int icon_right = scene_right - edge_inset - clock_slot -
                         system_icon_size * 2 - system_icon_gap -
                         input_group_gap;
  constexpr int kRepaintMargin = 2;
  return {icon_right - icon_size * 2 - input_group_gap - kRepaintMargin,
          center_y - icon_size / 2 - kRepaintMargin,
          icon_right + kRepaintMargin,
          center_y + (icon_size + 1) / 2 + kRepaintMargin};
}

RECT TaskbarPreviewBarBounds(HWND dialog, HWND card) {
  RECT bounds{};
  if (!card || !::GetClientRect(card, &bounds))
    return bounds;
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  const RECT units = settings_navigation::MapDialogUnits(dialog, 0, 0, 10, 26);
  const int padding = units.right;
  const int scene_top = units.bottom;
  const int scene_bottom = height - padding / 2;
  if (width <= padding * 2 || scene_bottom <= scene_top)
    return {};
  const int taskbar_height = (std::max)(18, (scene_bottom - scene_top) * 2 / 5);
  return {padding, scene_bottom - taskbar_height, width - padding,
          scene_bottom};
}
}  // namespace

std::wstring StatusIconSettingsDialog::LocalText(const wchar_t* simplified,
                                                 const wchar_t* traditional,
                                                 const wchar_t* english) const {
  return UiText(simplified, traditional, english);
}

LRESULT StatusIconSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  EnsureStatusControls(m_hWnd);
  LayoutStatusIconPage(m_hWnd);
  Gdiplus::GdiplusStartupInput startup;
  if (Gdiplus::GdiplusStartup(&graphics_token_, &startup, nullptr) !=
      Gdiplus::Ok) {
    EndDialog(IDCANCEL);
    return settings_navigation::HostedPageInitResult();
  }
  initial_ = weasel::StatusIconSettings::Load();
  draft_ = initial_;
  initial_input_method_ = weasel::InputMethodIconSettings::Load();
  draft_input_method_ = initial_input_method_;
  const auto user_settings = weasel::UserSettings::Load();
  preview_dark_ = weasel::ResolveAppearanceDarkMode(
      user_settings.appearance_theme_mode, IsUserDarkMode() != FALSE);
  schema_combo_.Attach(GetDlgItem(IDC_STATUS_SCHEMA_COMBO));
  PopulateSchemas();

  LOGFONTW title{};
  ::GetObjectW(GetFont(), sizeof(title), &title);
  title.lfWeight = FW_SEMIBOLD;
  if (heading_font_.CreateFontIndirect(&title)) {
    for (UINT id : {IDC_STATUS_BASE_TITLE, IDC_STATUS_TASKBAR_TITLE,
                    IDC_INPUT_METHOD_TITLE}) {
      CWindow(GetDlgItem(id)).SetFont(heading_font_);
    }
  }

  Localize();
  for (UINT id :
       {IDC_STATUS_BASE_CARD, IDC_STATUS_TASKBAR_CARD, IDC_INPUT_METHOD_CARD})
    settings_navigation::PrepareCard(m_hWnd, id);
  for (UINT id : {IDC_STATUS_RESTORE, IDC_STATUS_CHINESE_CHANGE,
                  IDC_STATUS_ENGLISH_CHANGE, IDC_STATUS_CHINESE_CAPS_CHANGE,
                  IDC_STATUS_CHINESE_INHERIT, IDC_STATUS_ASCII_INHERIT,
                  IDC_STATUS_CAPS_INHERIT, IDC_INPUT_METHOD_CHANGE,
                  IDC_INPUT_METHOD_RESTORE})
    settings_navigation::StyleActionButton(m_hWnd, id);
  settings_navigation::StyleCombo(m_hWnd, IDC_STATUS_SCHEMA_COMBO);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_CAPS_AUTOMATIC,
      settings_navigation::ToggleState::Segment::Left);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_CAPS_CUSTOM,
      settings_navigation::ToggleState::Segment::Right);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_PREVIEW_CHINESE,
      settings_navigation::ToggleState::Segment::Left);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_PREVIEW_ENGLISH,
      settings_navigation::ToggleState::Segment::Middle);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_PREVIEW_CHINESE_CAPS,
      settings_navigation::ToggleState::Segment::Right);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_PREVIEW_LIGHT,
      settings_navigation::ToggleState::Segment::Left);
  settings_navigation::StyleSegmentedToggle(
      m_hWnd, IDC_STATUS_PREVIEW_DARK,
      settings_navigation::ToggleState::Segment::Right);
  for (UINT id :
       {IDC_STATUS_CAPS_CARD, IDC_STATUS_BADGE_LETTER, IDC_STATUS_BADGE_DOT,
        IDC_STATUS_AUTO_CHINESE_ICON, IDC_STATUS_AUTO_CHINESE_LABEL,
        IDC_STATUS_AUTO_ENGLISH_ICON, IDC_STATUS_AUTO_ENGLISH_LABEL,
        IDC_STATUS_ENGLISH_CAPS_LABEL, IDC_STATUS_ENGLISH_CAPS_ICON,
        IDC_STATUS_ENGLISH_CAPS_CHANGE, IDC_STATUS_PREVIEW_ENGLISH_CAPS}) {
    ::ShowWindow(GetDlgItem(id), SW_HIDE);
  }
  ::ShowWindow(GetDlgItem(IDC_STATUS_TASKBAR_ICON), SW_HIDE);
  CheckRadioButton(IDC_STATUS_PREVIEW_CHINESE, IDC_STATUS_PREVIEW_CHINESE_CAPS,
                   IDC_STATUS_PREVIEW_CHINESE);
  CheckRadioButton(
      IDC_STATUS_PREVIEW_LIGHT, IDC_STATUS_PREVIEW_DARK,
      preview_dark_ ? IDC_STATUS_PREVIEW_DARK : IDC_STATUS_PREVIEW_LIGHT);
  CheckRadioButton(IDC_STATUS_CAPS_AUTOMATIC, IDC_STATUS_CAPS_CUSTOM,
                   IDC_STATUS_CAPS_AUTOMATIC);
  RefreshScope();
  settings_navigation::Install(m_hWnd, settings_navigation::Page::StatusIcons,
                               {IDC_STATUS_APPLY,
                                IDCANCEL,
                                WeaselDisplayUserDataPath().wstring(),
                                {IDC_STATUS_TITLE, IDC_STATUS_MESSAGE}});
  CenterWindow();
  return settings_navigation::HostedPageInitResult();
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

LRESULT StatusIconSettingsDialog::OnMeasureItem(UINT,
                                                WPARAM,
                                                LPARAM parameter,
                                                BOOL& handled) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  if (!measure || measure->CtlID != IDC_STATUS_SCHEMA_COMBO) {
    handled = FALSE;
    return 0;
  }
  measure->itemHeight = settings_navigation::MeasureComboItemHeight(m_hWnd);
  handled = TRUE;
  return TRUE;
}

LRESULT StatusIconSettingsDialog::OnDrawItem(UINT,
                                             WPARAM,
                                             LPARAM parameter,
                                             BOOL& handled) {
  const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
  if (draw->CtlID == IDC_STATUS_SCHEMA_COMBO) {
    settings_navigation::DrawComboItem(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_STATUS_TASKBAR_CARD) {
    DrawTaskbarPreview(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_STATUS_BASE_CARD ||
      draw->CtlID == IDC_INPUT_METHOD_CARD ||
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
  if ((id < IDC_STATUS_BASE_TITLE || id > IDC_STATUS_TASKBAR_TITLE) &&
      id != IDC_INPUT_METHOD_TITLE && id != IDC_INPUT_METHOD_HINT &&
      id != IDC_INPUT_METHOD_ICON) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkMode(context, TRANSPARENT);
  return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
}

LRESULT StatusIconSettingsDialog::OnButtonColor(UINT,
                                                WPARAM dc,
                                                LPARAM window,
                                                BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id < IDC_STATUS_CHINESE_CHANGE ||
       id > IDC_STATUS_PREVIEW_ENGLISH_CAPS) &&
      id != IDC_STATUS_PREVIEW_LIGHT && id != IDC_STATUS_PREVIEW_DARK &&
      id != IDC_INPUT_METHOD_CHANGE && id != IDC_INPUT_METHOD_RESTORE) {
    handled = FALSE;
    return 0;
  }
  const auto context = reinterpret_cast<HDC>(dc);
  ::SetBkColor(context, settings_theme::GetColor(COLOR_WINDOW));
  return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
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
  SolidBrush taskbar_fill(preview_dark_ ? Color(224, 28, 30, 34)
                                        : Color(232, 243, 243, 245));
  canvas.FillRectangle(&taskbar_fill, taskbar);

  const int preview_dpi = ::GetDeviceCaps(draw.hDC, LOGPIXELSX);
  const int effective_dpi = preview_dpi > 0 ? preview_dpi : 96;
  const int edge_inset = (std::max)(8, ::MulDiv(8, effective_dpi, 96));
  const int preferred_icon_size = ::MulDiv(16, effective_dpi, 96);
  const int icon_size =
      (std::min)((std::max)(16, preferred_icon_size), taskbar_height - 8);
  int right = taskbar.GetRight() - edge_inset;
  FontFamily family(L"Segoe UI");
  const REAL clock_font_size = static_cast<REAL>(
      (std::min)((std::max)(10, ::MulDiv(10, effective_dpi, 96)),
                 (std::max)(10, taskbar.Height / 3)));
  Font time_font(&family, clock_font_size, FontStyleRegular, UnitPixel);
  SolidBrush foreground(preview_dark_ ? Color(255, 245, 245, 245)
                                      : Color(255, 35, 35, 35));
  StringFormat format;
  format.SetAlignment(StringAlignmentFar);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetFormatFlags(StringFormatFlagsNoWrap);
  SYSTEMTIME now{};
  ::GetLocalTime(&now);
  wchar_t time_text[16]{};
  wchar_t date_text[16]{};
  swprintf_s(time_text, L"%02d:%02d", now.wHour, now.wMinute);
  swprintf_s(date_text, L"%04d/%02d/%02d", now.wYear, now.wMonth, now.wDay);
  const int clock_slot = (std::max)(76, ::MulDiv(76, effective_dpi, 96));
  const REAL clock_left = static_cast<REAL>(right - clock_slot + 4);
  const REAL clock_width = static_cast<REAL>(clock_slot - 6);
  const REAL clock_line_height = static_cast<REAL>(taskbar.Height) / 2.0f;
  const RectF time_bounds(clock_left, static_cast<REAL>(taskbar.Y), clock_width,
                          clock_line_height);
  const RectF date_bounds(clock_left,
                          static_cast<REAL>(taskbar.Y) + clock_line_height,
                          clock_width, clock_line_height);
  canvas.DrawString(time_text, -1, &time_font, time_bounds, &format,
                    &foreground);
  canvas.DrawString(date_text, -1, &time_font, date_bounds, &format,
                    &foreground);
  right -= clock_slot;

  const int center_y = taskbar.Y + taskbar.Height / 2;
  const int preferred_system_icon_size = ::MulDiv(16, effective_dpi, 96);
  const int system_icon_size = (std::min)(
      (std::max)(16, preferred_system_icon_size), taskbar_height - 8);
  const int system_icon_gap = (std::max)(4, ::MulDiv(4, effective_dpi, 96));
  const int input_group_gap = (std::max)(8, ::MulDiv(8, effective_dpi, 96));
  FontFamily fluent_icons(L"Segoe Fluent Icons");
  FontFamily legacy_icons(L"Segoe MDL2 Assets");
  FontFamily* icon_family =
      fluent_icons.GetLastStatus() == Ok ? &fluent_icons : &legacy_icons;
  Font system_icon_font(icon_family, static_cast<REAL>(system_icon_size),
                        FontStyleRegular, UnitPixel);
  StringFormat icon_format;
  icon_format.SetAlignment(StringAlignmentCenter);
  icon_format.SetLineAlignment(StringAlignmentCenter);
  icon_format.SetFormatFlags(StringFormatFlagsNoWrap);
  const RectF battery_bounds(static_cast<REAL>(right - system_icon_size),
                             static_cast<REAL>(center_y - system_icon_size / 2),
                             static_cast<REAL>(system_icon_size),
                             static_cast<REAL>(system_icon_size));
  canvas.DrawString(L"\xE83F", 1, &system_icon_font, battery_bounds,
                    &icon_format, &foreground);
  right -= system_icon_size + system_icon_gap;

  const RectF sound_bounds(static_cast<REAL>(right - system_icon_size),
                           static_cast<REAL>(center_y - system_icon_size / 2),
                           static_cast<REAL>(system_icon_size),
                           static_cast<REAL>(system_icon_size));
  canvas.DrawString(L"\xE767", 1, &system_icon_font, sound_bounds, &icon_format,
                    &foreground);
  canvas.Flush(FlushIntentionSync);
  right -= system_icon_size + input_group_gap;
  if (preview_icons_[4]) {
    ::DrawIconEx(draw.hDC, right - icon_size, center_y - icon_size / 2,
                 preview_icons_[4], icon_size, icon_size, 0, nullptr,
                 DI_NORMAL);
  }
  right -= icon_size + input_group_gap;
  if (preview_icons_[3]) {
    ::DrawIconEx(draw.hDC, right - icon_size, center_y - icon_size / 2,
                 preview_icons_[3], icon_size, icon_size, 0, nullptr,
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
      {IDC_INPUT_METHOD_TITLE,
       LocalText(L"输入法标识图标", L"輸入法識別圖示", L"Input method icon")},
      {IDC_INPUT_METHOD_HINT,
       LocalText(L"所有用户共用 · 应用需要管理员权限",
                 L"所有使用者共用 · 套用需要管理員權限",
                 L"Shared by all users · Administrator permission required")},
      {IDC_INPUT_METHOD_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_INPUT_METHOD_RESTORE,
       LocalText(L"恢复默认", L"還原預設", L"Restore default")},
      {IDC_STATUS_TITLE, LocalText(L"状态图标", L"狀態圖示", L"Status icons")},
      {IDC_STATUS_RESTORE,
       LocalText(L"恢复全局默认", L"還原全域預設", L"Restore global defaults")},
      {IDC_STATUS_BASE_TITLE,
       LocalText(L"输入状态图标", L"輸入狀態圖示", L"Input status icons")},
      {IDC_STATUS_CHINESE_LABEL,
       LocalText(L"中文状态", L"中文狀態", L"Chinese state")},
      {IDC_STATUS_ENGLISH_LABEL,
       LocalText(L"西文状态", L"西文狀態", L"Western state")},
      {IDC_STATUS_CHINESE_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_ENGLISH_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_CAPS_TITLE,
       LocalText(L"适用于所有输入方案", L"適用於所有輸入方案",
                 L"Used by all input schemes")},
      {IDC_STATUS_CAPS_AUTOMATIC,
       LocalText(L"全局图标", L"全域圖示", L"Global icons")},
      {IDC_STATUS_CAPS_CUSTOM,
       LocalText(L"方案图标", L"方案圖示", L"Scheme icons")},
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
       LocalText(L"大写状态", L"大寫狀態", L"Caps Lock state")},
      {IDC_STATUS_ENGLISH_CAPS_LABEL,
       LocalText(L"英文 · 大写锁定", L"英文 · 大寫鎖定",
                 L"English · Caps Lock")},
      {IDC_STATUS_CHINESE_CAPS_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_CHINESE_INHERIT,
       LocalText(L"使用全局", L"使用全域", L"Use global")},
      {IDC_STATUS_ASCII_INHERIT,
       LocalText(L"使用全局", L"使用全域", L"Use global")},
      {IDC_STATUS_CAPS_INHERIT,
       LocalText(L"使用全局", L"使用全域", L"Use global")},
      {IDC_STATUS_ENGLISH_CAPS_CHANGE,
       LocalText(L"更换图标…", L"更換圖示…", L"Choose icon…")},
      {IDC_STATUS_TASKBAR_TITLE,
       LocalText(L"任务栏预览", L"工作列預覽", L"Taskbar preview")},
      {IDC_STATUS_PREVIEW_CHINESE, LocalText(L"中文", L"中文", L"Chinese")},
      {IDC_STATUS_PREVIEW_ENGLISH, LocalText(L"西文", L"西文", L"Western")},
      {IDC_STATUS_PREVIEW_CHINESE_CAPS, LocalText(L"大写", L"大寫", L"Caps")},
      {IDC_STATUS_PREVIEW_LIGHT, LocalText(L"浅色", L"淺色", L"Light")},
      {IDC_STATUS_PREVIEW_DARK, LocalText(L"深色", L"深色", L"Dark")},
      {IDC_STATUS_PREVIEW_ENGLISH_CAPS,
       LocalText(L"英文大写", L"英文大寫", L"English Caps")},
      {IDC_STATUS_APPLY, LocalText(L"应用", L"套用", L"Apply")},
      {IDCANCEL, LocalText(L"关闭", L"關閉", L"Close")},
  };
  for (const auto& [id, text] : labels)
    ::SetDlgItemTextW(m_hWnd, id, text.c_str());
}

void StatusIconSettingsDialog::PopulateSchemas() {
  schemas_.clear();
  schema_combo_.ResetContent();
  RimeApi* api = rime_get_api();
  RimeModule* module = api ? api->find_module("levers") : nullptr;
  auto* levers =
      module ? reinterpret_cast<RimeLeversApi*>(module->get_api()) : nullptr;
  RimeSwitcherSettings* settings =
      levers ? levers->switcher_settings_init() : nullptr;
  if (!api || !levers || !settings ||
      !levers->load_settings(reinterpret_cast<RimeCustomSettings*>(settings))) {
    if (settings && levers)
      levers->custom_settings_destroy(
          reinterpret_cast<RimeCustomSettings*>(settings));
    return;
  }

  RimeSchemaList available{};
  RimeSchemaList selected{};
  const bool has_available =
      levers->get_available_schema_list(settings, &available) != FALSE;
  const bool has_selected =
      levers->get_selected_schema_list(settings, &selected) != FALSE;
  std::set<std::string> added;
  const auto append_schema = [&](const RimeSchemaListItem& item) {
    if (!item.schema_id || !*item.schema_id)
      return;
    if (!added.emplace(item.schema_id).second)
      return;
    SchemaEntry entry;
    entry.id = u8tow(item.schema_id);
    entry.name = u8tow(item.name && *item.name ? item.name : item.schema_id);
    entry.initial = weasel::SchemaStatusIconSettings::Load(entry.id);
    entry.draft = entry.initial;
    const int row = schema_combo_.AddString(entry.name.c_str());
    if (row != CB_ERR && row != CB_ERRSPACE) {
      schemas_.push_back(std::move(entry));
      schema_combo_.SetItemData(row, schemas_.size() - 1);
    }
  };

  if (has_selected) {
    for (size_t index = 0; index < selected.size; ++index) {
      const auto& selected_item = selected.list[index];
      bool matched = false;
      if (selected_item.schema_id && has_available) {
        for (size_t available_index = 0; available_index < available.size;
             ++available_index) {
          const auto& available_item = available.list[available_index];
          if (available_item.schema_id &&
              !strcmp(available_item.schema_id, selected_item.schema_id)) {
            append_schema(available_item);
            matched = true;
            break;
          }
        }
      }
      if (!matched)
        append_schema(selected_item);
    }
  }
  if (has_available) {
    for (size_t index = 0; index < available.size; ++index)
      append_schema(available.list[index]);
  }
  if (has_selected)
    levers->schema_list_destroy(&selected);
  if (has_available)
    levers->schema_list_destroy(&available);
  levers->custom_settings_destroy(
      reinterpret_cast<RimeCustomSettings*>(settings));
  if (schema_combo_.GetCount() > 0)
    schema_combo_.SetCurSel(0);
  CWindow(GetDlgItem(IDC_STATUS_CAPS_CUSTOM)).EnableWindow(!schemas_.empty());
}

StatusIconSettingsDialog::SchemaEntry*
StatusIconSettingsDialog::SelectedSchema() {
  const int row = schema_combo_.GetCurSel();
  if (row == CB_ERR)
    return nullptr;
  const DWORD_PTR index = schema_combo_.GetItemData(row);
  return index < schemas_.size() ? &schemas_[index] : nullptr;
}

const StatusIconSettingsDialog::SchemaEntry*
StatusIconSettingsDialog::SelectedSchema() const {
  const int row = schema_combo_.GetCurSel();
  if (row == CB_ERR)
    return nullptr;
  const DWORD_PTR index = schema_combo_.GetItemData(row);
  return index < schemas_.size() ? &schemas_[index] : nullptr;
}

bool StatusIconSettingsDialog::HasSchemaChanges() const {
  return std::any_of(schemas_.begin(), schemas_.end(), [](const auto& schema) {
    return schema.draft != schema.initial;
  });
}

void StatusIconSettingsDialog::RefreshScope() {
  const bool schema_scope = edit_scope_ == EditScope::Schema;
  const auto set_visible_without_redraw = [](HWND control, bool visible) {
    if (!control)
      return;
    const bool currently_visible =
        (::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
    if (currently_visible == visible)
      return;
    ::SetWindowPos(control, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_NOREDRAW |
                       (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
  };
  ::SetDlgItemTextW(
      m_hWnd, IDC_STATUS_CAPS_TITLE,
      (schema_scope ? LocalText(L"输入方案", L"輸入方案", L"Input scheme")
                    : LocalText(L"适用于所有输入方案", L"適用於所有輸入方案",
                                L"Used by all input schemes"))
          .c_str());
  set_visible_without_redraw(GetDlgItem(IDC_STATUS_SCHEMA_COMBO), schema_scope);
  const std::array<UINT, 3> inherits = {IDC_STATUS_CHINESE_INHERIT,
                                        IDC_STATUS_ASCII_INHERIT,
                                        IDC_STATUS_CAPS_INHERIT};
  for (UINT inherit : inherits)
    set_visible_without_redraw(GetDlgItem(inherit), schema_scope);
  ::SetDlgItemTextW(
      m_hWnd, IDC_STATUS_RESTORE,
      (schema_scope ? LocalText(L"恢复本方案默认", L"還原本方案預設",
                                L"Restore scheme defaults")
                    : LocalText(L"恢复全局默认", L"還原全域預設",
                                L"Restore global defaults"))
          .c_str());
  CheckRadioButton(
      IDC_STATUS_CAPS_AUTOMATIC, IDC_STATUS_CAPS_CUSTOM,
      schema_scope ? IDC_STATUS_CAPS_CUSTOM : IDC_STATUS_CAPS_AUTOMATIC);
  RefreshPreviews();
  RefreshApplyState();

  // The card and the controls placed over it are separate child windows.
  // Redrawing their parent with RDW_ALLCHILDREN presents the dialog background,
  // the card, and the controls in separate frames. Repaint only the card: its
  // WS_CLIPSIBLINGS style preserves visible controls while clearing pixels left
  // by controls that were just hidden.
  if (HWND card = GetDlgItem(IDC_STATUS_BASE_CARD)) {
    ::RedrawWindow(card, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
  }
  const std::array<UINT, 8> scope_controls = {
      IDC_STATUS_CAPS_TITLE,      IDC_STATUS_SCHEMA_COMBO,
      IDC_STATUS_CHINESE_INHERIT, IDC_STATUS_ASCII_INHERIT,
      IDC_STATUS_CAPS_INHERIT,    IDC_STATUS_CAPS_AUTOMATIC,
      IDC_STATUS_CAPS_CUSTOM,     IDC_STATUS_RESTORE};
  for (UINT id : scope_controls) {
    HWND control = GetDlgItem(id);
    if (control && ::IsWindowVisible(control)) {
      ::RedrawWindow(control, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_NOERASE | RDW_FRAME | RDW_UPDATENOW);
    }
  }
}

HICON StatusIconSettingsDialog::ResolveIcon(const std::wstring& custom,
                                            bool english,
                                            bool caps) const {
  if (HICON custom_icon = LoadIconFile(custom))
    return custom_icon;
  return LoadBuiltInIcon(english, caps);
}

HICON StatusIconSettingsDialog::ResolveActiveIcon(size_t state) const {
  const std::wstring* global = nullptr;
  switch (state) {
    case 0:
      global = &draft_.chinese;
      break;
    case 1:
      global = &draft_.english;
      break;
    default:
      global = &draft_.caps;
      break;
  }
  const bool english = state == 1;
  const bool caps = state == 2;
  if (edit_scope_ == EditScope::Schema) {
    if (const SchemaEntry* schema = SelectedSchema()) {
      const std::wstring* override = state == 0   ? &schema->draft.chinese
                                     : state == 1 ? &schema->draft.ascii
                                                  : &schema->draft.caps;
      if (!override->empty() && !weasel::StatusIconUsesGlobal(*override)) {
        if (HICON icon = LoadIconFile(*override))
          return icon;
      }
    }
  }
  return ResolveIcon(*global, english, caps);
}

void StatusIconSettingsDialog::SetPreviewIcon(UINT control, HICON icon) {
  const size_t index = [control]() -> size_t {
    switch (control) {
      case IDC_STATUS_CHINESE_ICON:
        return 0;
      case IDC_STATUS_ENGLISH_ICON:
        return 1;
      case IDC_STATUS_CHINESE_CAPS_ICON:
        return 2;
      case IDC_INPUT_METHOD_ICON:
        return 4;
      default:
        return 3;
    }
  }();
  SendDlgItemMessage(control, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
  if (preview_icons_[index])
    ::DestroyIcon(preview_icons_[index]);
  preview_icons_[index] = icon;
}

void StatusIconSettingsDialog::RefreshPreviews() {
  HICON identity = LoadIconFile(draft_input_method_.source);
  if (!identity)
    identity = reinterpret_cast<HICON>(::LoadImageW(
        ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_INPUT_METHOD),
        IMAGE_ICON, 32, 32, 0));
  SetPreviewIcon(IDC_INPUT_METHOD_ICON, identity);
  SetPreviewIcon(IDC_STATUS_CHINESE_ICON, ResolveActiveIcon(0));
  SetPreviewIcon(IDC_STATUS_ENGLISH_ICON, ResolveActiveIcon(1));
  SetPreviewIcon(IDC_STATUS_CHINESE_CAPS_ICON, ResolveActiveIcon(2));

  HICON taskbar = nullptr;
  switch (preview_mode_) {
    case PreviewMode::Chinese:
      taskbar = ResolveActiveIcon(0);
      break;
    case PreviewMode::Western:
      taskbar = ResolveActiveIcon(1);
      break;
    case PreviewMode::Caps:
      taskbar = ResolveActiveIcon(2);
      break;
  }
  SetPreviewIcon(IDC_STATUS_TASKBAR_ICON, taskbar);
  if (HWND card = GetDlgItem(IDC_STATUS_TASKBAR_CARD)) {
    const RECT icon_bounds = TaskbarPreviewIconBounds(m_hWnd, card);
    ::RedrawWindow(card, &icon_bounds, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
  }
}

void StatusIconSettingsDialog::RefreshApplyState() {
  const bool pending = HasUnappliedChanges();
  settings_navigation::SetUnappliedChanges(
      m_hWnd, settings_navigation::Page::StatusIcons, pending);
  CWindow(GetDlgItem(IDC_STATUS_APPLY))
      .EnableWindow(settings_navigation::HasAnyUnappliedChanges());
  ::SetDlgItemTextW(m_hWnd, IDC_STATUS_MESSAGE,
                    (pending ? LocalText(L"有设置等待应用", L"有設定等待套用",
                                         L"Settings are ready to apply")
                             : L"")
                        .c_str());
}

bool StatusIconSettingsDialog::ChooseIconFile(std::wstring* path,
                                              bool english,
                                              bool caps,
                                              bool inherit_global) {
  std::vector<std::wstring> protected_paths = {
      draft_.chinese,
      draft_.english,
      draft_.caps,
      initial_.chinese,
      initial_.english,
      initial_.caps,
      draft_input_method_.source,
      initial_input_method_.source,
  };
  for (const auto& schema : schemas_) {
    for (const auto* settings : {&schema.initial, &schema.draft}) {
      protected_paths.push_back(settings->chinese);
      protected_paths.push_back(settings->ascii);
      protected_paths.push_back(settings->caps);
    }
  }
  const std::wstring inherited_default =
      caps ? draft_.caps : (english ? draft_.english : draft_.chinese);
  const std::wstring preferred =
      weasel::StatusIconUsesGlobal(*path) ? std::wstring() : *path;
  StatusIconPicker picker(m_hWnd, preferred, english, caps, inherit_global,
                          inherited_default, std::move(protected_paths));
  std::wstring selected;
  if (!picker.Show(&selected))
    return false;
  *path = inherit_global && selected.empty()
              ? std::wstring(weasel::kStatusIconUseGlobalMarker)
              : std::move(selected);
  return true;
}

LRESULT StatusIconSettingsDialog::OnChooseIcon(WORD, WORD id, HWND, BOOL&) {
  std::wstring* target = nullptr;
  bool english = false;
  bool caps = false;
  const bool schema_scope = edit_scope_ == EditScope::Schema;
  SchemaEntry* schema = schema_scope ? SelectedSchema() : nullptr;
  switch (id) {
    case IDC_STATUS_CHINESE_CHANGE:
      target = schema_scope ? (schema ? &schema->draft.chinese : nullptr)
                            : &draft_.chinese;
      break;
    case IDC_STATUS_ENGLISH_CHANGE:
      target = schema_scope ? (schema ? &schema->draft.ascii : nullptr)
                            : &draft_.english;
      english = true;
      break;
    case IDC_STATUS_CHINESE_CAPS_CHANGE:
      target = schema_scope ? (schema ? &schema->draft.caps : nullptr)
                            : &draft_.caps;
      caps = true;
      break;
  }
  if (target && ChooseIconFile(target, english, caps, schema_scope)) {
    RefreshPreviews();
    RefreshApplyState();
  }
  return 0;
}

LRESULT StatusIconSettingsDialog::OnScopeChanged(WORD, WORD id, HWND, BOOL&) {
  edit_scope_ =
      id == IDC_STATUS_CAPS_CUSTOM ? EditScope::Schema : EditScope::Global;
  if (edit_scope_ == EditScope::Schema && schemas_.empty())
    edit_scope_ = EditScope::Global;
  RefreshScope();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnChooseInputMethodIcon(WORD,
                                                          WORD,
                                                          HWND,
                                                          BOOL&) {
  std::filesystem::path file;
  if (!BrowseIconFile(m_hWnd, &file, true))
    return 0;
  if (!input_method_icon::Validate(input_method_icon::Read(file))) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"请选择有效的 ICO 图标文件。",
                  L"請選擇有效的 ICO 圖示檔案。",
                  L"Choose a valid ICO icon file.")
            .c_str(),
        LocalText(L"无法使用该图标", L"無法使用此圖示", L"Invalid icon")
            .c_str(),
        MB_OK | MB_ICONERROR);
    return 0;
  }
  draft_input_method_.source = file.wstring();
  RefreshPreviews();
  RefreshApplyState();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnRestoreInputMethodIcon(WORD,
                                                           WORD,
                                                           HWND,
                                                           BOOL&) {
  draft_input_method_ = {};
  RefreshPreviews();
  RefreshApplyState();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnSchemaChanged(WORD, WORD, HWND, BOOL&) {
  RefreshPreviews();
  return 0;
}

LRESULT StatusIconSettingsDialog::OnUseGlobal(WORD, WORD id, HWND, BOOL&) {
  SchemaEntry* schema = SelectedSchema();
  if (!schema)
    return 0;
  std::wstring* target = id == IDC_STATUS_CHINESE_INHERIT
                             ? &schema->draft.chinese
                         : id == IDC_STATUS_ASCII_INHERIT ? &schema->draft.ascii
                                                          : &schema->draft.caps;
  *target = weasel::kStatusIconUseGlobalMarker;
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

LRESULT StatusIconSettingsDialog::OnPreviewThemeChanged(WORD,
                                                        WORD id,
                                                        HWND,
                                                        BOOL&) {
  preview_dark_ = id == IDC_STATUS_PREVIEW_DARK;
  CheckRadioButton(IDC_STATUS_PREVIEW_LIGHT, IDC_STATUS_PREVIEW_DARK, id);
  if (HWND card = GetDlgItem(IDC_STATUS_TASKBAR_CARD)) {
    const RECT taskbar = TaskbarPreviewBarBounds(m_hWnd, card);
    ::RedrawWindow(card, &taskbar, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
  }
  return 0;
}

LRESULT StatusIconSettingsDialog::OnRestore(WORD, WORD, HWND, BOOL&) {
  if (edit_scope_ == EditScope::Schema) {
    if (SchemaEntry* schema = SelectedSchema())
      schema->draft = {};
  } else {
    draft_ = {};
  }
  RefreshPreviews();
  RefreshApplyState();
  return 0;
}

std::wstring StatusIconSettingsDialog::ImportIcon(const std::wstring& source,
                                                  std::wstring* error) const {
  return StoreIconInLibrary(source, error).wstring();
}

bool StatusIconSettingsDialog::Persist(std::wstring* error) {
  if (draft_input_method_ != initial_input_method_) {
    const DWORD result =
        input_method_icon::Apply(m_hWnd, draft_input_method_.source);
    if (result != ERROR_SUCCESS) {
      *error = result == ERROR_CANCELLED
                   ? LocalText(
                         L"未授权更换输入法标识图标，设置尚未应用。",
                         L"未授權更換輸入法識別圖示，設定尚未套用。",
                         L"Permission was cancelled. The icon was not applied.")
                   : LocalText(L"无法更新输入法标识图标：\n",
                               L"無法更新輸入法識別圖示：\n",
                               L"Could not update the input method icon:\n") +
                         input_method_icon::SystemError(result);
      return false;
    }
    initial_input_method_ = weasel::InputMethodIconSettings::Load();
    draft_input_method_ = initial_input_method_;
  }
  weasel::StatusIconSettings saved = draft_;
  const struct {
    const std::wstring* source;
    std::wstring* destination;
  } imports[] = {
      {&draft_.chinese, &saved.chinese},
      {&draft_.english, &saved.english},
      {&draft_.caps, &saved.caps},
  };
  for (const auto& import : imports) {
    *import.destination = ImportIcon(*import.source, error);
    if (!import.source->empty() && import.destination->empty())
      return false;
  }
  if (saved.Save() != ERROR_SUCCESS) {
    *error = LocalText(L"无法保存状态图标设置。", L"無法儲存狀態圖示設定。",
                       L"Could not save the status icon settings.");
    return false;
  }
  if (weasel::StatusIconSettings::Load() != saved) {
    *error = LocalText(L"无法验证已保存的状态图标设置。",
                       L"無法驗證已儲存的狀態圖示設定。",
                       L"Could not verify the saved status icon settings.");
    return false;
  }
  for (auto& schema : schemas_) {
    weasel::SchemaStatusIconSettings saved_schema = schema.draft;
    const struct {
      const std::wstring* source;
      std::wstring* destination;
    } schema_imports[] = {
        {&schema.draft.chinese, &saved_schema.chinese},
        {&schema.draft.ascii, &saved_schema.ascii},
        {&schema.draft.caps, &saved_schema.caps},
    };
    for (const auto& import : schema_imports) {
      if (import.source->empty() ||
          weasel::StatusIconUsesGlobal(*import.source))
        continue;
      *import.destination = ImportIcon(*import.source, error);
      if (import.destination->empty())
        return false;
    }
    if (saved_schema.Save(schema.id) != ERROR_SUCCESS) {
      *error = LocalText(L"无法保存输入方案专属图标设置。",
                         L"無法儲存輸入方案專屬圖示設定。",
                         L"Could not save the scheme-specific icon settings.");
      return false;
    }
    if (weasel::SchemaStatusIconSettings::Load(schema.id) != saved_schema) {
      *error = LocalText(L"无法验证已保存的输入方案专属图标设置。",
                         L"無法驗證已儲存的輸入方案專屬圖示設定。",
                         L"Could not verify the saved scheme-specific icon "
                         L"settings.");
      return false;
    }
    schema.draft = saved_schema;
    schema.initial = saved_schema;
  }
  draft_ = saved;
  initial_ = saved;
  weasel::NotifyUserSettingsChanged();
  return true;
}

bool StatusIconSettingsDialog::ApplyChanges() {
  std::wstring error;
  if (!Persist(&error)) {
    ::MessageBoxW(m_hWnd, error.c_str(),
                  LocalText(L"应用失败", L"套用失敗", L"Apply failed").c_str(),
                  MB_OK | MB_ICONERROR);
    return false;
  }
  RefreshApplyState();
  RefreshPreviews();
  return true;
}

LRESULT StatusIconSettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  if (settings_navigation::RequestApply(m_hWnd))
    return 0;
  ApplyChanges();
  return 0;
}

bool StatusIconSettingsDialog::ConfirmDiscard() {
  if (!HasUnappliedChanges())
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

bool StatusIconSettingsDialog::HasUnappliedChanges() const {
  return draft_ != initial_ || HasSchemaChanges() ||
         draft_input_method_ != initial_input_method_;
}

bool StatusIconSettingsDialog::ConfirmClose() {
  return ConfirmDiscard();
}

LRESULT StatusIconSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (ConfirmClose())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT StatusIconSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    return 0;
  if (ConfirmClose())
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT StatusIconSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  const auto page = settings_navigation::PageFromCommand(id);
  if (page == settings_navigation::Page::StatusIcons)
    return 0;
  if (settings_navigation::RequestNavigate(m_hWnd, id))
    return 0;
  if (!ConfirmDiscard())
    return 0;
  EndDialog(id);
  return 0;
}
