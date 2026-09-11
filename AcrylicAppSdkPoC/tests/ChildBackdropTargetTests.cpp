#include "../ChildBackdropTarget.h"
#include "../EdgeClipDiagnostic.h"
#include "../AlignedAcrylicClip.h"
#include "../../include/WeaselUserSettings.h"
#include "../../include/WeaselMenu.h"
#include "ModeColorSchemeTests.h"
#include "../../include/WeaselMenuPlacement.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <climits>
#include <string>

namespace {

namespace abi = weasel_acrylic::child_abi;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::MakeAndInitialize;
using weasel_acrylic::ChildBackdropTarget;

int liveNatives = 0;
int liveBrushes = 0;
int liveMenus = 0;

struct MenuItem {
  UINT command;
  DWORD flags;
  std::wstring text;
  ComPtr<ITfMenu> child;
};

class Menu final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          ITfMenu> {
 public:
  std::vector<MenuItem> items;
  UINT failCommand = 0;
  bool omitChild = false;
  Menu() { ++liveMenus; }
  ~Menu() { --liveMenus; }
  HRESULT STDMETHODCALLTYPE AddMenuItem(UINT command,
                                        DWORD flags,
                                        HBITMAP,
                                        HBITMAP,
                                        const WCHAR* text,
                                        ULONG length,
                                        ITfMenu** child) override {
    if (failCommand && command == failCommand)
      return E_ACCESSDENIED;
    if ((flags & TF_LBMENUF_SUBMENU) ? (!child || *child) : child != nullptr)
      return E_INVALIDARG;
    MenuItem item{command, flags, text ? std::wstring(text, length) : L"", {}};
    if (child && !omitChild) {
      auto nested = Make<Menu>();
      if (!nested)
        return E_OUTOFMEMORY;
      nested->failCommand = failCommand;
      item.child = nested;
      const HRESULT result = item.child.CopyTo(child);
      if (FAILED(result))
        return result;
    }
    items.push_back(std::move(item));
    return S_OK;
  }
};

struct MenuFixture {
  HMENU root = ::CreatePopupMenu();
  HMENU settings = ::CreatePopupMenu();
  HMENU future = ::CreatePopupMenu();
  MenuFixture();
  ~MenuFixture() { ::DestroyMenu(root); }
};

class Brush final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
          abi::ICompositionBrush,
          Microsoft::WRL::FtmBase> {
  InspectableClass(L"WeaselTests.Brush", BaseTrust);

 public:
  Brush() { ++liveBrushes; }
  ~Brush() override { --liveBrushes; }
};

// An ABI collaborator for error/ordering tests, not an Acrylic renderer.
class Native final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>,
          abi::ICompositionTarget,
          abi::ICompositionObject,
          abi::ICompositionSupportsSystemBackdrop,
          abi::ISpriteVisual,
          Microsoft::WRL::FtmBase> {
  InspectableClass(L"WeaselTests.Native", BaseTrust);

 public:
  Native() { ++liveNatives; }
  ~Native() override { --liveNatives; }

  ComPtr<abi::ICompositionBrush> system, brush;
  HRESULT putResult = S_OK;
  HRESULT getResult = S_OK;
  HRESULT systemGetResult = S_OK;
  unsigned childWrites = 0;
  unsigned systemWrites = 0;
  std::function<void()> onWrite;

  HRESULT STDMETHODCALLTYPE get_Root(abi::IVisual** value) override {
    *value = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE put_Root(abi::IVisual*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE get_Compositor(abi::ICompositor** value) override {
    *value = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE
  get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) override {
    *value = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE
  get_Properties(abi::ICompositionPropertySet** value) override {
    *value = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE
  StartAnimation(HSTRING, abi::ICompositionAnimation*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE StopAnimation(HSTRING) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE
  get_SystemBackdrop(abi::ICompositionBrush** value) override {
    *value = nullptr;
    if (FAILED(systemGetResult))
      return systemGetResult;
    const HRESULT hr = system.CopyTo(value);
    return FAILED(hr) ? hr : systemGetResult;
  }
  HRESULT STDMETHODCALLTYPE
  put_SystemBackdrop(abi::ICompositionBrush* value) override {
    ++systemWrites;
    system = value;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_Brush(abi::ICompositionBrush** value) override {
    *value = nullptr;
    if (FAILED(getResult))
      return getResult;
    const HRESULT hr = brush.CopyTo(value);
    return FAILED(hr) ? hr : getResult;
  }
  HRESULT STDMETHODCALLTYPE put_Brush(abi::ICompositionBrush* value) override {
    ++childWrites;
    auto callback = std::move(onWrite);
    onWrite = nullptr;
    if (callback)
      callback();
    if (FAILED(putResult))
      return putResult;
    brush = value;
    return putResult;
  }
};

void Check(bool value) {
  if (!value)
    throw std::runtime_error("contract assertion failed");
}

MenuFixture::MenuFixture() {
  Check(root && settings && future);
  Check(::AppendMenuW(root, MF_STRING, 40008, L"Original settings (&S)"));
  Check(::AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(settings),
                      L"Additional settings (&X)"));
  Check(::AppendMenuW(settings, MF_STRING, 40017, L"Acrylic effect (&A)"));
  Check(::AppendMenuW(settings, MF_SEPARATOR, 0, nullptr));
  Check(::AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(future),
                      L"Future group"));
  Check(::AppendMenuW(future, MF_STRING | MF_GRAYED | MF_CHECKED, 45000,
                      L"Future option"));
  Check(::AppendMenuW(root, MF_STRING, 40015, L"Restart (&E)"));
}

struct SettingsFixture {
  std::wstring path = L"Software\\WeaselSettingsTests-" +
                      std::to_wstring(::GetCurrentProcessId()) + L"-" +
                      std::to_wstring(::GetTickCount64());
  HKEY key = nullptr;
  weasel::UserSettingsStore store{HKEY_CURRENT_USER, path.c_str()};
  SettingsFixture() {
    DWORD disposition = 0;
    Check(::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                            REG_OPTION_VOLATILE,
                            KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &key,
                            &disposition) == ERROR_SUCCESS);
    Check(disposition == REG_CREATED_NEW_KEY);
  }
  ~SettingsFixture() {
    if (key) {
      ::RegCloseKey(key);
      ::RegDeleteKeyExW(HKEY_CURRENT_USER, path.c_str(), KEY_WOW64_64KEY, 0);
    }
  }
  void Finish() {
    Check(::RegCloseKey(key) == ERROR_SUCCESS);
    key = nullptr;
    Check(::RegDeleteKeyExW(HKEY_CURRENT_USER, path.c_str(), KEY_WOW64_64KEY,
                            0) == ERROR_SUCCESS);
  }
};

struct Fixture {
  ComPtr<Native> native = Make<Native>();
  ComPtr<Brush> brush = Make<Brush>();
  ComPtr<ChildBackdropTarget> target;
  Fixture() {
    Check(native && brush);
    Check(SUCCEEDED(MakeAndInitialize<ChildBackdropTarget>(
        &target, static_cast<abi::ICompositionTarget*>(native.Get()),
        static_cast<abi::ISpriteVisual*>(native.Get()))));
  }
  HRESULT Publish() { return target->put_SystemBackdrop(brush.Get()); }
};

void Run(const char* name, const std::function<void()>& test) {
  std::cout << "RUN " << name << '\n';
  test();
  Check(liveNatives == 0 && liveBrushes == 0 && liveMenus == 0);
  std::cout << "PASS " << name << '\n';
}

}  // namespace

int main() {
  const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(apartment))
    return 2;
  int result = 0;
  try {
    Run("tray popup follows four edges and signed secondary monitor "
        "coordinates",
        [] {
          const RECT work{0, 0, 1920, 1080};
          auto left = weasel::PlaceMenu({0, 1000}, work, ABE_LEFT);
          Check(!left.cascade_left && !(left.flags & TPM_RIGHTALIGN));
          auto right = weasel::PlaceMenu({1920, 1000}, work, ABE_RIGHT);
          Check(right.cascade_left && (right.flags & TPM_RIGHTALIGN));
          Check(right.anchor.x == 1919);
          auto top = weasel::PlaceMenu({1800, -40}, work, ABE_TOP);
          Check(top.cascade_left && !(top.flags & TPM_BOTTOMALIGN));
          Check(top.anchor.y == 0);
          auto bottom = weasel::PlaceMenu({100, 1120}, work, ABE_BOTTOM);
          Check(!bottom.cascade_left && (bottom.flags & TPM_BOTTOMALIGN));
          Check(bottom.anchor.y == 1079);
          const RECT secondary{-2560, -1440, 0, 0};
          auto hidden = weasel::PlaceMenu({-1, -100}, secondary);
          Check(hidden.cascade_left && (hidden.flags & TPM_BOTTOMALIGN));
          auto opposite = weasel::PlaceMenu({-2500, -1400}, secondary);
          Check(!opposite.cascade_left && !(opposite.flags & TPM_BOTTOMALIGN));
        });
    Run("palette groups require explicit valid light and dark members", [] {
      ModeSchemeConfig f;
      Check(f.Open());
      Check(f.api->config_load_string(&f.config, R"(
preset_color_schemes:
  day: {back_color: 0xffffff}
  night: {back_color: 0x111111}
  broken: scalar
color_scheme_groups:
  complete: {light: day, dark: night}
  missing: {light: day}
  unavailable: {light: day, dark: removed}
  malformed: {light: day, dark: broken}
)"));
      weasel::ColorSchemePair pair;
      Check(weasel::ReadColorSchemePair(f.api, &f.config, &f.config,
                                        "color_scheme_groups/complete", &pair));
      Check(pair.light == "day" && pair.dark == "night");
      for (auto id : {"missing", "unavailable", "malformed", "absent"})
        Check(!weasel::ReadColorSchemePair(
            f.api, &f.config, &f.config,
            std::string("color_scheme_groups/") + id, &pair));
      Check(std::string(weasel::ColorSchemeConfigKey(
                weasel::ColorSchemeTarget::Normal)) == "style/color_scheme");
      Check(std::string(weasel::ColorSchemeConfigKey(
                weasel::ColorSchemeTarget::NormalDark)) ==
            "style/color_scheme_dark");
      // Without legacy normal overrides the existing normal/schema resolution
      // remains authoritative, including when an Acrylic scheme is configured.
      Check(weasel::ModeColorScheme(f.api, &f.config, false, false).empty());
      Check(weasel::ModeColorScheme(f.api, &f.config, false, true).empty());
    });
    Run("mode palettes independently resolve real YAML without changing legacy "
        "keys",
        [] {
          ModeSchemeConfig f;
          Check(f.Open());
          Check(f.api->config_load_string(&f.config, R"(
style:
  color_scheme: daylight
  color_scheme_dark: midnight
  color_scheme_acrylic: glass
  color_scheme_acrylic_dark: glass_dark
  color_scheme_normal: solid
  color_scheme_normal_dark: solid_dark
preset_color_schemes:
  glass: {back_color: 0x112233}
  glass_dark: {back_color: 0x223344}
  solid: {back_color: 0x445566}
  solid_dark: {back_color: 0x556677}
)"));
          for (bool dark : {false, true, false}) {
            for (bool acrylic : {true, false, true})
              Check(weasel::ModeColorScheme(f.api, &f.config, acrylic, dark) ==
                    (acrylic ? (dark ? "glass_dark" : "glass")
                             : (dark ? "solid_dark" : "solid")));
          }
          Check(std::string(f.api->config_get_cstring(
                    &f.config, "style/color_scheme")) == "daylight");
          Check(std::string(f.api->config_get_cstring(
                    &f.config, "style/color_scheme_dark")) == "midnight");
          Check(f.api->config_set_string(
              &f.config, "style/color_scheme_acrylic", "solid"));
          Check(weasel::ModeColorScheme(f.api, &f.config, true, false) ==
                "solid");
          Check(weasel::ModeColorScheme(f.api, &f.config, false, false) ==
                "solid");
          Check(weasel::ModeColorScheme(f.api, &f.config, true, true) ==
                "glass_dark");
          Check(weasel::ModeColorScheme(f.api, &f.config, false, true) ==
                "solid_dark");
        });
    Run("missing empty and invalid mode palettes preserve legacy fallback", [] {
      ModeSchemeConfig f;
      Check(f.Open());
      Check(f.api->config_load_string(&f.config, R"(
style:
  color_scheme: original
  color_scheme_normal: solid
preset_color_schemes:
  solid: {back_color: 0x445566}
  malformed: scalar
)"));
      Check(weasel::ModeColorScheme(f.api, &f.config, true, false).empty());
      Check(weasel::ModeColorScheme(f.api, &f.config, false, true).empty());
      for (const char* value : {"", "missing", "malformed"}) {
        Check(f.api->config_set_string(&f.config, "style/color_scheme_acrylic",
                                       value));
        Check(f.api->config_set_string(
            &f.config, "style/color_scheme_acrylic_dark", value));
        Check(weasel::ModeColorScheme(f.api, &f.config, true, false).empty());
        Check(weasel::ModeColorScheme(f.api, &f.config, true, true).empty());
        Check(weasel::ModeColorScheme(f.api, &f.config, false, false) ==
              "solid");
        Check(weasel::ModeColorScheme(f.api, &f.config, false, true).empty());
      }
      Check(f.api->config_load_string(&f.config,
                                      "style: {color_scheme_acrylic: [bad]}"));
      for (bool dark : {false, true}) {
        Check(weasel::ModeColorScheme(f.api, &f.config, true, dark).empty());
        Check(weasel::ModeColorScheme(f.api, &f.config, false, dark).empty());
      }
    });
    Run("language bar retains nested settings original commands and states",
        [] {
          MenuFixture f;
          for (bool enabled : {true, false, true}) {
            Check(weasel::SetMenuCommandChecked(f.root, 40017, enabled));
            auto menu = Make<Menu>();
            Check(menu && weasel::CopyMenuToTfMenu(f.root, menu.Get()) == S_OK);
            Check(menu->items.size() == 3);
            Check(menu->items[0].command == 40008 &&
                  menu->items[0].text == L"Original settings (&S)");
            Check(menu->items[2].command == 40015 &&
                  menu->items[2].text == L"Restart (&E)");
            Check(menu->items[1].flags == TF_LBMENUF_SUBMENU &&
                  menu->items[1].text == L"Additional settings (&X)");
            auto settings = static_cast<Menu*>(menu->items[1].child.Get());
            Check(settings && settings->items.size() == 3);
            Check(settings->items[0].command == 40017 &&
                  settings->items[0].text == L"Acrylic effect (&A)" &&
                  settings->items[0].flags ==
                      (enabled ? TF_LBMENUF_CHECKED : 0));
            Check(settings->items[1].flags ==
                      (TF_LBMENUF_SEPARATOR | TF_LBMENUF_GRAYED) &&
                  settings->items[1].text.empty());
            auto future = static_cast<Menu*>(settings->items[2].child.Get());
            Check(future && future->items.size() == 1);
            Check(future->items[0].command == 45000 &&
                  future->items[0].flags ==
                      (TF_LBMENUF_GRAYED | TF_LBMENUF_CHECKED));
          }
          Check(!weasel::SetMenuCommandChecked(f.root, 49999, true));
        });
    Run("language bar reports nested menu failures and releases returned "
        "objects",
        [] {
          MenuFixture f;
          auto menu = Make<Menu>();
          Check(menu != nullptr);
          menu->failCommand = 45000;
          Check(weasel::CopyMenuToTfMenu(f.root, menu.Get()) == E_ACCESSDENIED);
          Check(menu->items.size() == 2);
          menu.Reset();
          Check(liveMenus == 0);
        });
    Run("language bar rejects missing menus and missing submenu objects", [] {
      MenuFixture f;
      auto menu = Make<Menu>();
      Check(menu != nullptr);
      Check(weasel::CopyMenuToTfMenu(f.root, nullptr) == E_POINTER);
      Check(weasel::CopyMenuToTfMenu(nullptr, menu.Get()) == E_INVALIDARG);
      menu->omitChild = true;
      Check(weasel::CopyMenuToTfMenu(f.root, menu.Get()) == E_UNEXPECTED);
    });
    Run("aligned clip defaults to ordinary hosts and preserves compatibility "
        "routes",
        [] {
          using weasel_acrylic::UseAlignedAcrylicClip;
          Check(UseAlignedAcrylicClip(1, false, false));
          Check(!UseAlignedAcrylicClip(1, true, false));
          Check(!UseAlignedAcrylicClip(1, false, true));
          Check(!UseAlignedAcrylicClip(2, false, false));
          Check(!UseAlignedAcrylicClip(0, false, false));
        });
    Run("aligned clip scales with eligible content instead of a fixed sample",
        [] {
          using namespace weasel_acrylic;
          for (int radius = 3; radius <= 17; ++radius) {
            const int sizes[][2] = {{2 * radius + 1, 2 * radius + 2},
                                    {2 * radius + 2, 2 * radius + 1},
                                    {195, 266},
                                    {194, 265},
                                    {800, 160}};
            for (const auto& size : sizes) {
              Check(SupportsAlignedAcrylicClip(size[0], size[1], 1, 255, radius,
                                               1));
              const int w = size[0] + 2, h = size[1] + 2, r = radius + 1;
              const auto clip = SelectAlignedAcrylicClip(w, h, r, w, h, r);
              Check(clip.x == 1 && clip.y == 1 && clip.width == w - 1 &&
                    clip.height == h - 1 && clip.radius == r);
              Check(clip.x + clip.width == w && clip.y + clip.height == h);
            }
          }
        });
    Run("aligned clip rejects unverified borders and compressed corners", [] {
      using weasel_acrylic::SupportsAlignedAcrylicClip;
      for (int border : {0, 2, 3, 4, -1})
        Check(!SupportsAlignedAcrylicClip(195, 266, border, 255, 16, 1));
      for (unsigned alpha : {0U, 24U, 254U, 256U})
        Check(!SupportsAlignedAcrylicClip(195, 266, 1, alpha, 16, 1));
      for (int r : {0, 1, 2, 18, 48, 64, -1, INT_MAX})
        Check(!SupportsAlignedAcrylicClip(195, 266, 1, 255, r, 1));
      for (int r = 3; r <= 17; ++r) {
        Check(!SupportsAlignedAcrylicClip(2 * r, 266, 1, 255, r, 1));
        Check(!SupportsAlignedAcrylicClip(195, 2 * r, 1, 255, r, 1));
      }
      Check(!SupportsAlignedAcrylicClip(195, 266, 1, 255, 16, 0));
      Check(!SupportsAlignedAcrylicClip(-1, 0, 1, 255, 16, 1));
    });
    Run("aligned clip rejects stale or cleared layout publications and "
        "reapplies",
        [] {
          using namespace weasel_acrylic;
          const int publications[][3] = {
              {-1, -1, -1}, {198, 268, 17}, {197, 269, 17}, {197, 268, 18}};
          for (const auto& p : publications) {
            const auto clip =
                SelectAlignedAcrylicClip(197, 268, 17, p[0], p[1], p[2]);
            Check(clip.x == 0 && clip.y == 0 && clip.width == 197 &&
                  clip.height == 268 && clip.radius == 17);
          }
          const auto clip =
              SelectAlignedAcrylicClip(197, 268, 17, 197, 268, 17);
          Check(EdgeClipReadbackMatches(clip, 1, 1, 196, 267, 17, 17));
          Check(!EdgeClipReadbackMatches(clip, 0, 0, 197, 268, 17, 17));
        });
    Run("user settings persist off and on without replacing other preferences",
        [] {
          SettingsFixture f;
          Check(f.store.ReadBool(weasel::kAcrylicEnabledSetting, true));
          Check(!f.store.ReadBool(L"FutureSetting", false));
          Check(f.store.WriteBool(L"FutureSetting", true) == ERROR_SUCCESS);
          for (bool enabled : {false, true, false}) {
            Check(f.store.WriteBool(weasel::kAcrylicEnabledSetting, enabled) ==
                  ERROR_SUCCESS);
            const weasel::UserSettingsStore reopened(HKEY_CURRENT_USER,
                                                     f.path.c_str());
            Check(reopened.ReadBool(weasel::kAcrylicEnabledSetting, true) ==
                  enabled);
            Check(reopened.ReadBool(L"FutureSetting", false));
          }
          f.Finish();
        });
    Run("malformed user settings cannot enable optional material", [] {
      SettingsFixture f;
      const DWORD invalid = 2;
      Check(::RegSetValueExW(f.key, weasel::kAcrylicEnabledSetting, 0,
                             REG_DWORD, reinterpret_cast<const BYTE*>(&invalid),
                             sizeof(invalid)) == ERROR_SUCCESS);
      Check(!f.store.ReadBool(weasel::kAcrylicEnabledSetting, true));
      const BYTE shortValue = 1;
      Check(::RegSetValueExW(f.key, weasel::kAcrylicEnabledSetting, 0,
                             REG_DWORD, &shortValue,
                             sizeof(shortValue)) == ERROR_SUCCESS);
      Check(!f.store.ReadBool(weasel::kAcrylicEnabledSetting, true));
      const wchar_t text[] = L"true";
      Check(::RegSetValueExW(f.key, weasel::kAcrylicEnabledSetting, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(text),
                             sizeof(text)) == ERROR_SUCCESS);
      Check(!f.store.ReadBool(weasel::kAcrylicEnabledSetting, true));
      f.Finish();
    });
    Run("failed settings storage is reported and notification identity is "
        "stable",
        [] {
          weasel::UserSettingsStore invalid(nullptr, L"invalid");
          Check(invalid.WriteBool(weasel::kAcrylicEnabledSetting, false) !=
                ERROR_SUCCESS);
          Check(!invalid.ReadBool(weasel::kAcrylicEnabledSetting, true));
          const UINT message = weasel::UserSettingsChangedMessage();
          Check(message >= 0xc000 && message <= 0xffff);
          Check(message == weasel::UserSettingsChangedMessage());
        });
    Run("edge clip rejects unmeasured foreground styles", [] {
      using weasel_acrylic::IsMeasuredEdgeClipSample;
      Check(IsMeasuredEdgeClipSample(144, 195, 266, 1, 255, 16, 1, true, true));
      const int original[] = {144, 195, 266, 1, 255, 16, 1};
      for (int changed = 0; changed < 7; ++changed) {
        for (int delta : {-1, 1}) {
          int input[7];
          for (int i = 0; i < 7; ++i)
            input[i] = original[i];
          input[changed] += delta;
          Check(!IsMeasuredEdgeClipSample(input[0], input[1], input[2],
                                          input[3], input[4], input[5],
                                          input[6], true, true));
        }
      }
      Check(
          !IsMeasuredEdgeClipSample(144, 195, 266, 1, 255, 16, 1, false, true));
      Check(
          !IsMeasuredEdgeClipSample(144, 195, 266, 1, 255, 16, 1, true, false));
    });
    Run("edge clip requires opt-in and matching live host", [] {
      using weasel_acrylic::SelectEdgeClipGeometry;
      Check(SelectEdgeClipGeometry(true, true, 144, 197, 268, 17, true).x == 1);
      Check(SelectEdgeClipGeometry(false, true, 144, 197, 268, 17, true).x ==
            0);
      Check(SelectEdgeClipGeometry(true, false, 144, 197, 268, 17, true).x ==
            0);
      Check(SelectEdgeClipGeometry(true, true, 144, 197, 268, 17, false).x ==
            0);
      const int inputs[][4] = {
          {96, 197, 268, 17},  {192, 197, 268, 17}, {144, 196, 268, 17},
          {144, 198, 268, 17}, {144, 197, 267, 17}, {144, 197, 269, 17},
          {144, 197, 268, 16}, {144, 197, 268, 18}, {144, 0, 268, 17},
          {144, 197, 0, 17},   {144, -1, -1, -1}};
      for (const auto& v : inputs) {
        const auto clip =
            SelectEdgeClipGeometry(true, true, v[0], v[1], v[2], v[3], true);
        Check(clip.x == 0 && clip.y == 0 && clip.width == v[1] &&
              clip.height == v[2] && clip.radius == v[3]);
      }
    });
    Run("edge clip preserves far edges and restores the complete rectangle",
        [] {
          using weasel_acrylic::SelectEdgeClipGeometry;
          const auto applied =
              SelectEdgeClipGeometry(true, true, 144, 197, 268, 17, true);
          Check(applied.x == 1 && applied.y == 1 && applied.width == 196 &&
                applied.height == 267 && applied.radius == 17);
          Check(applied.x + applied.width == 197 &&
                applied.y + applied.height == 268);
          const auto restored =
              SelectEdgeClipGeometry(true, false, 144, 197, 268, 17, true);
          Check(restored.x == 0 && restored.y == 0 && restored.width == 197 &&
                restored.height == 268 && restored.radius == 17);
          const auto reapplied =
              SelectEdgeClipGeometry(true, true, 144, 197, 268, 17, true);
          Check(reapplied.x == applied.x && reapplied.y == applied.y &&
                reapplied.width == applied.width &&
                reapplied.height == applied.height);
        });
    Run("edge clip readback rejects stale offsets sizes and radii", [] {
      using weasel_acrylic::EdgeClipReadbackMatches;
      const auto expected = weasel_acrylic::SelectEdgeClipGeometry(
          true, true, 144, 197, 268, 17, true);
      Check(EdgeClipReadbackMatches(expected, 1, 1, 196, 267, 17, 17));
      const float original[] = {1, 1, 196, 267, 17, 17};
      for (int changed = 0; changed < 6; ++changed) {
        float values[6];
        for (int i = 0; i < 6; ++i)
          values[i] = original[i];
        values[changed] += 0.5f;
        Check(!EdgeClipReadbackMatches(expected, values[0], values[1],
                                       values[2], values[3], values[4],
                                       values[5]));
      }
      Check(!EdgeClipReadbackMatches(expected, 0, 0, 197, 268, 17, 17));
    });
    Run("own COM identity and interface closure", [] {
      Fixture f;
      ComPtr<IUnknown> primary, viaObject, viaBackdrop, inner;
      ComPtr<abi::ICompositionObject> object;
      ComPtr<abi::ICompositionSupportsSystemBackdrop> backdrop;
      Check(SUCCEEDED(f.target.As(&primary)) && SUCCEEDED(f.native.As(&inner)));
      Check(SUCCEEDED(f.target.As(&object)) &&
            SUCCEEDED(f.target.As(&backdrop)));
      Check(SUCCEEDED(object.As(&viaObject)) &&
            SUCCEEDED(backdrop.As(&viaBackdrop)));
      Check(primary.Get() == viaObject.Get() &&
            primary.Get() == viaBackdrop.Get());
      Check(primary.Get() != inner.Get());
    });
    Run("native S_FALSE setter and getter are preserved", [] {
      Fixture f;
      f.native->putResult = S_FALSE;
      Check(f.Publish() == S_FALSE && f.native->brush.Get() == f.brush.Get());
      Check(f.target->Publications() == 1 && f.target->LastSetter() == S_FALSE);
      f.native->getResult = S_FALSE;
      ComPtr<abi::ICompositionBrush> actual;
      Check(f.target->get_SystemBackdrop(&actual) == S_FALSE);
      Check(actual.Get() == f.brush.Get() && f.native->systemWrites == 0);
    });
    Run("failed native setter preserves previous child and HRESULT", [] {
      Fixture f;
      Check(f.Publish() == S_OK);
      f.native->putResult = E_ACCESSDENIED;
      Check(f.target->put_SystemBackdrop(nullptr) == E_ACCESSDENIED);
      Check(f.native->brush.Get() == f.brush.Get());
      Check(f.target->Failure() == E_ACCESSDENIED &&
            f.target->Publications() == 1);
    });
    Run("NULL publication clears child only", [] {
      Fixture f;
      Check(f.Publish() == S_OK &&
            f.target->put_SystemBackdrop(nullptr) == S_OK);
      Check(!f.native->brush && !f.native->system &&
            f.native->systemWrites == 0);
    });
    Run("unexpected native system slot is never overwritten", [] {
      Fixture f;
      f.native->system = f.brush;
      Check(f.Publish() == E_UNEXPECTED && f.native->childWrites == 0);
      Check(f.native->system.Get() == f.brush.Get() &&
            f.native->systemWrites == 0);
    });
    Run("native slot read failure is returned before write", [] {
      Fixture f;
      f.native->systemGetResult = E_ACCESSDENIED;
      Check(f.Publish() == E_ACCESSDENIED && f.native->childWrites == 0);
    });
    Run("reentrant publication is rejected without hiding native outer result",
        [] {
          Fixture f;
          HRESULT nested = S_OK;
          f.native->onWrite = [&] {
            nested = f.target->put_SystemBackdrop(nullptr);
          };
          Check(f.Publish() == S_OK && nested == RPC_E_CALL_REJECTED);
          Check(f.native->brush.Get() == f.brush.Get() &&
                f.target->Failure() == RPC_E_CALL_REJECTED);
        });
    Run("Stop inside an in-flight write clears its late completion", [] {
      Fixture f;
      HRESULT stop = E_FAIL;
      f.native->onWrite = [&] { stop = f.target->Stop(); };
      Check(f.Publish() == S_OK && stop == S_FALSE);
      Check(!f.native->brush && f.native->childWrites == 2);
      Check(f.Publish() == RO_E_CLOSED && !f.native->brush);
    });
    Run("concurrent publication never waits on the callback owner", [] {
      Fixture f;
      HRESULT nested = S_OK;
      f.native->onWrite = [&] {
        std::thread worker([&] {
          const HRESULT initialized =
              ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
          nested = FAILED(initialized) ? initialized
                                       : f.target->put_SystemBackdrop(nullptr);
          if (SUCCEEDED(initialized))
            ::CoUninitialize();
        });
        worker.join();
      };
      Check(f.Publish() == S_OK && nested == RPC_E_CALL_REJECTED);
      Check(f.native->childWrites == 1 &&
            f.native->brush.Get() == f.brush.Get());
    });
    Run("concurrent Stop fences a late native completion", [] {
      Fixture f;
      HRESULT stop = E_FAIL;
      f.native->onWrite = [&] {
        std::thread worker([&] { stop = f.target->Stop(); });
        worker.join();
      };
      Check(f.Publish() == S_OK && stop == S_FALSE);
      Check(!f.native->brush && f.Publish() == RO_E_CLOSED);
    });
    Run("stopped target permits observation and Close but rejects new material",
        [] {
          Fixture f;
          Check(f.Publish() == S_OK && f.target->Stop() == S_OK);
          ComPtr<abi::ICompositionBrush> actual;
          Check(f.target->get_SystemBackdrop(&actual) == S_OK && !actual);
          Check(f.target->put_SystemBackdrop(nullptr) == S_OK);
          Check(f.Publish() == RO_E_CLOSED && !f.native->brush);
        });
    Run("failed initialization releases all acquired interfaces", [] {
      auto native = Make<Native>();
      auto brush = Make<Brush>();
      native->system = brush;
      ComPtr<ChildBackdropTarget> target;
      Check(MakeAndInitialize<ChildBackdropTarget>(
                &target, static_cast<abi::ICompositionTarget*>(native.Get()),
                static_cast<abi::ISpriteVisual*>(native.Get())) ==
            E_UNEXPECTED);
      Check(!target);
    });
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    result = 1;
  }
  ::CoUninitialize();
  return result;
}
