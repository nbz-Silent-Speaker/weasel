#include "../ChildBackdropTarget.h"
#include "../EdgeClipDiagnostic.h"
#include "../AlignedAcrylicClip.h"
#include "../../include/WeaselUserSettings.h"

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
  test();
  Check(liveNatives == 0 && liveBrushes == 0);
  std::cout << "PASS " << name << '\n';
}

}  // namespace

int main() {
  const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(apartment))
    return 2;
  int result = 0;
  try {
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
