#include "../ChildBackdropTarget.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

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
