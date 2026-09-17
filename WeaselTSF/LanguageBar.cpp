#include "stdafx.h"
#include <resource.h>
#include <thread>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "LanguageBar.h"
#include "CandidateList.h"
#include <WeaselUtility.h>
#include <WeaselUserSettings.h>
#include <WeaselMenu.h>
#include <WeaselMenuPlacement.h>

static const DWORD LANGBARITEMSINK_COOKIE = 0x42424242;

namespace {
HICON LoadStatusIconFile(const std::wstring& path) {
  if (path.empty() ||
      ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return nullptr;
  }
  return reinterpret_cast<HICON>(::LoadImageW(
      nullptr, path.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
}

HICON LoadStatusIconResource(UINT resource) {
  return reinterpret_cast<HICON>(::LoadImageW(
      g_hInst, MAKEINTRESOURCEW(resource), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
}

HICON LoadResolvedStatusIcon(const std::wstring& schema_override,
                             const std::wstring& global,
                             UINT fallback) {
  if (!schema_override.empty() &&
      !weasel::StatusIconUsesGlobal(schema_override)) {
    if (HICON icon = LoadStatusIconFile(schema_override))
      return icon;
  }
  if (HICON icon = LoadStatusIconFile(global))
    return icon;
  return LoadStatusIconResource(fallback);
}
}  // namespace

static LPCWSTR GetWeaselRegName() {
  LPCWSTR WEASEL_REG_NAME_;
  if (is_wow64())
    WEASEL_REG_NAME_ = L"Software\\WOW6432Node\\Rime\\Weasel";
  else
    WEASEL_REG_NAME_ = L"Software\\Rime\\Weasel";

  return WEASEL_REG_NAME_;
}

static bool open(const std::wstring& path) {
  return (uintptr_t)ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL,
                                  SW_SHOWNORMAL) > 32;
}

CLangBarItemButton::CLangBarItemButton(com_ptr<WeaselTSF> pTextService,
                                       REFGUID guid,
                                       weasel::UIStyle& style)
    : _status(0),
      ascii_mode(false),
      caps_lock((::GetKeyState(VK_CAPITAL) & 1) != 0),
      _style(style),
      _schema_id() {
  DllAddRef();

  _pLangBarItemSink = NULL;
  _cRef = 1;
  _pTextService = pTextService;
  _guid = guid;
}

CLangBarItemButton::~CLangBarItemButton() {
  DllRelease();
}

STDMETHODIMP CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, IID_ITfLangBarItemButton))
    *ppvObject = (ITfLangBarItemButton*)this;
  else if (IsEqualIID(riid, IID_ITfSource))
    *ppvObject = (ITfSource*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::AddRef() {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::Release() {
  LONG cr = --_cRef;
  assert(_cRef >= 0);
  if (_cRef == 0)
    delete this;
  return cr;
}

STDMETHODIMP CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
  pInfo->clsidService = c_clsidTextService;
  pInfo->guidItem = _guid;
  pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
                   TF_LBI_STYLE_SHOWNINTRAY;
  pInfo->ulSort = 1;
  lstrcpyW(pInfo->szDescription, L"WeaselTSF Button");
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetStatus(DWORD* pdwStatus) {
  *pdwStatus = _status;
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::Show(BOOL fShow) {
  SetLangbarStatus(TF_LBI_STATUS_HIDDEN, fShow ? FALSE : TRUE);
  return S_OK;
}

static LANGID GetActiveProfileLangId() {
  CComPtr<ITfInputProcessorProfileMgr> pInputProcessorProfileMgr;
  HRESULT hr = pInputProcessorProfileMgr.CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_ALL);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

  TF_INPUTPROCESSORPROFILE profile;
  hr = pInputProcessorProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,
                                                   &profile);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
  return profile.langid;
}

STDMETHODIMP CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
  LANGID langid = get_language_id();
  if (langid == TEXTSERVICE_LANGID_HANS) {
    *pbstrToolTip = SysAllocString(L"左键切换模式，右键打开菜单");
  } else if (langid == TEXTSERVICE_LANGID_HANT) {
    *pbstrToolTip = SysAllocString(L"左鍵切換模式，右鍵打開菜單");
  } else {
    *pbstrToolTip = SysAllocString(
        L"Left-click to switch modes\n\nRight-click for more options");
  }

  return (*pbstrToolTip == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click,
                                         POINT pt,
                                         const RECT* prcArea) {
  if (click == TF_LBI_CLK_LEFT) {
    _pTextService->_HandleLangBarMenuSelect(
        ascii_mode ? ID_WEASELTRAY_DISABLE_ASCII : ID_WEASELTRAY_ENABLE_ASCII);
    ascii_mode = !ascii_mode;
    if (_pLangBarItemSink) {
      _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }
  } else if (click == TF_LBI_CLK_RIGHT) {
    /* Open menu */
    HWND hwnd = _pTextService->_GetFocusedContextWindow();
    if (hwnd != NULL) {
      LANGID langid = get_language_id();
      HMENU menu;
      if (langid == TEXTSERVICE_LANGID_HANS) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANS));
      } else if (langid == TEXTSERVICE_LANGID_HANT) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANT));
      } else {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
      }
      HMENU popupMenu = GetSubMenu(menu, 0);
      UINT wID = weasel::TrackTrayMenu(popupMenu, pt, hwnd,
                                       TPM_NONOTIFY | TPM_RETURNCMD, prcArea);
      DestroyMenu(menu);
      _pTextService->_HandleLangBarMenuSelect(wID);
    }
  }
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
  HMENU menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
  HMENU popupMenu = GetSubMenu(menu, 0);
  const HRESULT result = weasel::CopyMenuToTfMenu(popupMenu, pMenu);
  DestroyMenu(menu);
  return result;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID) {
  _pTextService->_HandleLangBarMenuSelect(wID);
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon) {
  if (!phIcon)
    return E_INVALIDARG;
  const auto global = weasel::StatusIconSettings::Load();
  const auto schema = weasel::SchemaStatusIconSettings::Load(_schema_id);
  if (caps_lock)
    *phIcon = LoadResolvedStatusIcon(schema.caps, global.caps, IDI_CAPS);
  else if (ascii_mode)
    *phIcon = LoadResolvedStatusIcon(schema.ascii, global.english, IDI_EN);
  else
    *phIcon = LoadResolvedStatusIcon(schema.chinese, global.chinese, IDI_ZH);
  return (*phIcon == NULL) ? E_FAIL : S_OK;
}

STDMETHODIMP CLangBarItemButton::GetText(BSTR* pbstrText) {
  *pbstrText = SysAllocString(L"WeaselTSF Button");
  return (*pbstrText == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CLangBarItemButton::AdviseSink(REFIID riid,
                                            IUnknown* punk,
                                            DWORD* pdwCookie) {
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
    return CONNECT_E_CANNOTCONNECT;
  if (_pLangBarItemSink != NULL)
    return CONNECT_E_ADVISELIMIT;

  if (punk->QueryInterface(IID_ITfLangBarItemSink,
                           (LPVOID*)&_pLangBarItemSink) != S_OK) {
    _pLangBarItemSink = NULL;
    return E_NOINTERFACE;
  }
  *pdwCookie = LANGBARITEMSINK_COOKIE;
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::UnadviseSink(DWORD dwCookie) {
  if (dwCookie != LANGBARITEMSINK_COOKIE || _pLangBarItemSink == NULL)
    return CONNECT_E_NOCONNECTION;
  _pLangBarItemSink = NULL;
  return S_OK;
}

void CLangBarItemButton::UpdateWeaselStatus(weasel::Status stat) {
  ascii_mode = stat.ascii_mode;
  _schema_id = stat.schema_id;
  if (_pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }
}

void CLangBarItemButton::UpdateCapsLockState(bool enabled) {
  if (caps_lock == enabled)
    return;
  caps_lock = enabled;
  if (_pLangBarItemSink)
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
}

void CLangBarItemButton::SetLangbarStatus(DWORD dwStatus, BOOL fSet) {
  BOOL isChange = FALSE;

  if (fSet) {
    if (!(_status & dwStatus)) {
      _status |= dwStatus;
      isChange = TRUE;
    }
  } else {
    if (_status & dwStatus) {
      _status &= ~dwStatus;
      isChange = TRUE;
    }
  }

  if (isChange && _pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }

  return;
}

std::wstring WeaselTSF::_GetRootDir() {
  std::wstring dir{};
  RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(), L"WeaselRoot", dir);
  return dir;
}

void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
  std::wstring dir{};
  switch (wID) {
    case ID_WEASELTRAY_RERUN_SERVICE:
    case ID_WEASELTRAY_INSTALLDIR:
      if (RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(),
                            L"WeaselRoot", dir) == ERROR_SUCCESS) {
        if (wID == ID_WEASELTRAY_RERUN_SERVICE) {
          std::thread th([dir]() {
            ShellExecuteW(NULL, L"open", (dir + L"\\start_service.bat").c_str(),
                          NULL, dir.c_str(), SW_HIDE);
          });
          th.detach();
        } else
          open(dir);
      }
      break;
    case ID_WEASELTRAY_USERCONFIG:
      if (FAILED(RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel",
                                   L"RimeUserDir", dir)) ||
          dir.empty()) {
        WCHAR _path[MAX_PATH] = {0};
        ExpandEnvironmentStringsW(L"%AppData%\\Rime", _path, _countof(_path));
        dir = std::wstring(_path);
      }
      if (!dir.empty() && fs::exists(dir))
        open(dir);
      else
        MessageBoxW(NULL, (L"Not found: " + dir).c_str(), L"RimeUserDir",
                    MB_ICONERROR | MB_OK);
      break;
    case ID_WEASELTRAY_LOGDIR:
      open(WeaselLogPath().wstring());
      break;
    case ID_WEASELTRAY_WIKI:
      open(L"https://rime.im/docs/");
      break;
    case ID_WEASELTRAY_FORUM:
      open(L"https://rime.im/discuss/");
      break;
    default:
      m_client.TrayCommand(wID);
      break;
  }
}

HWND WeaselTSF::_GetFocusedContextWindow() {
  HWND hwnd = NULL;
  ITfDocumentMgr* pDocMgr;
  if (_pThreadMgr->GetFocus(&pDocMgr) == S_OK && pDocMgr != NULL) {
    ITfContext* pContext;
    if (pDocMgr->GetTop(&pContext) == S_OK && pContext != NULL) {
      ITfContextView* pContextView;
      if (pContext->GetActiveView(&pContextView) == S_OK &&
          pContextView != NULL) {
        pContextView->GetWnd(&hwnd);
        pContextView->Release();
      }
      pContext->Release();
    }
    pDocMgr->Release();
  }

  if (hwnd == NULL) {
    HWND hwndForeground = GetForegroundWindow();
    if (GetWindowThreadProcessId(hwndForeground, NULL) == GetCurrentThreadId())
      hwnd = hwndForeground;
  }

  return hwnd;
}

BOOL WeaselTSF::_InitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;
  BOOL fRet = FALSE;

  if (_pThreadMgr->QueryInterface(&pLangBarItemMgr) != S_OK)
    return FALSE;

  if ((_pLangBarButton = new CLangBarItemButton(this, GUID_LBI_INPUTMODE,
                                                _cand->style())) == NULL)
    return FALSE;

  if (pLangBarItemMgr->AddItem(_pLangBarButton) != S_OK) {
    _pLangBarButton = NULL;
    return FALSE;
  }

  _pLangBarButton->Show(TRUE);
  fRet = TRUE;

  return fRet;
}

void WeaselTSF::_UninitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;

  if (_pLangBarButton == NULL)
    return;

  if (_pThreadMgr->QueryInterface(&pLangBarItemMgr) == S_OK) {
    pLangBarItemMgr->RemoveItem(_pLangBarButton);
  }

  _pLangBarButton = NULL;
}

void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  if (!_pLangBarButton)
    return;
  DWORD flags;
  _GetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
  if (stat.ascii_mode)
    flags &= (~TF_CONVERSIONMODE_NATIVE);
  else
    flags |= TF_CONVERSIONMODE_NATIVE;
  if (stat.full_shape)
    flags |= TF_CONVERSIONMODE_FULLSHAPE;
  else
    flags &= (~TF_CONVERSIONMODE_FULLSHAPE);
  _SetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);

  _pLangBarButton->UpdateWeaselStatus(stat);
}

void WeaselTSF::_UpdateCapsLockState(bool enabled) {
  if (_pLangBarButton)
    _pLangBarButton->UpdateCapsLockState(enabled);
}

void WeaselTSF::_ShowLanguageBar(BOOL show) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->Show(show);
}

void WeaselTSF::_EnableLanguageBar(BOOL enable) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->SetLangbarStatus(TF_LBI_STATUS_DISABLED, !enable);
}
