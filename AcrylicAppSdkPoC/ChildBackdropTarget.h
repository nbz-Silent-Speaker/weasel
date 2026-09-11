#pragma once

#include <windows.ui.composition.h>
#include <wrl.h>

#include <atomic>
#include <mutex>

namespace weasel_acrylic {

namespace child_abi = ABI::Windows::UI::Composition;

// R22 opt-in target. The controller's logical SystemBackdrop is the actual
// child brush; the native desktop SystemBackdrop is never assigned here.
class ChildBackdropTarget final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>,
          child_abi::ICompositionTarget,
          child_abi::ICompositionObject,
          child_abi::ICompositionSupportsSystemBackdrop,
          Microsoft::WRL::FtmBase> {
  InspectableClass(L"WeaselAcrylic.ChildBackdropTarget", BaseTrust);

 public:
  HRESULT RuntimeClassInitialize(IUnknown* desktop, IUnknown* child) noexcept {
    if (!desktop || !child)
      return E_INVALIDARG;
    HRESULT hr = desktop->QueryInterface(IID_PPV_ARGS(&target_));
    if (FAILED(hr))
      return hr;
    hr = desktop->QueryInterface(IID_PPV_ARGS(&object_));
    if (FAILED(hr))
      return hr;
    hr = desktop->QueryInterface(IID_PPV_ARGS(&backdrop_));
    if (FAILED(hr))
      return hr;
    hr = child->QueryInterface(IID_PPV_ARGS(&child_));
    if (FAILED(hr))
      return hr;

    Microsoft::WRL::ComPtr<IAgileObject> agile;
    hr = desktop->QueryInterface(IID_PPV_ARGS(&agile));
    if (FAILED(hr))
      return hr;
    agile.Reset();
    hr = child->QueryInterface(IID_PPV_ARGS(&agile));
    if (FAILED(hr))
      return hr;

    // The helper creates both objects on its compositor and verifies that
    // identity again with the root/clip/child geometry before exposing them.
    return CheckNativeSlot();
  }

  HRESULT STDMETHODCALLTYPE
  get_Root(child_abi::IVisual** value) noexcept override {
    if (!value)
      return E_POINTER;
    *value = nullptr;
    return Call([&] { return target_->get_Root(value); }, true);
  }

  HRESULT STDMETHODCALLTYPE
  put_Root(child_abi::IVisual* value) noexcept override {
    return Call([&] { return target_->put_Root(value); });
  }

  HRESULT STDMETHODCALLTYPE
  get_Compositor(child_abi::ICompositor** value) noexcept override {
    if (!value)
      return E_POINTER;
    *value = nullptr;
    return Call([&] { return object_->get_Compositor(value); }, true);
  }

  HRESULT STDMETHODCALLTYPE get_Dispatcher(
      ABI::Windows::UI::Core::ICoreDispatcher** value) noexcept override {
    if (!value)
      return E_POINTER;
    *value = nullptr;
    return Call([&] { return object_->get_Dispatcher(value); }, true);
  }

  HRESULT STDMETHODCALLTYPE
  get_Properties(child_abi::ICompositionPropertySet** value) noexcept override {
    if (!value)
      return E_POINTER;
    *value = nullptr;
    return Call([&] { return object_->get_Properties(value); }, true);
  }

  HRESULT STDMETHODCALLTYPE StartAnimation(
      HSTRING name,
      child_abi::ICompositionAnimation* animation) noexcept override {
    return Call([&] { return object_->StartAnimation(name, animation); });
  }

  HRESULT STDMETHODCALLTYPE StopAnimation(HSTRING name) noexcept override {
    return Call([&] { return object_->StopAnimation(name); });
  }

  HRESULT STDMETHODCALLTYPE
  get_SystemBackdrop(child_abi::ICompositionBrush** value) noexcept override {
    if (!value)
      return E_POINTER;
    *value = nullptr;
    return Call([&] { return child_->get_Brush(value); }, true);
  }

  HRESULT STDMETHODCALLTYPE
  put_SystemBackdrop(child_abi::ICompositionBrush* value) noexcept override {
    return Call(
        [&]() -> HRESULT {
          if (writing_)
            return RPC_E_CALL_REJECTED;
          writing_ = true;
          struct WritingScope {
            bool& writing;
            ~WritingScope() { writing = false; }
          } scope{writing_};
          if (value) {
            const HRESULT empty = CheckNativeSlot();
            if (FAILED(empty))
              return empty;
          }
          const HRESULT hr = child_->put_Brush(value);
          lastSetter_.store(hr);
          lastThread_.store(::GetCurrentThreadId());
          if (SUCCEEDED(hr))
            ++publications_;
          // Stop may have re-entered while the native setter was running. A
          // late completion must not leave a brush in a permanently stopped
          // target.
          if (stopped_.load())
            Remember(child_->put_Brush(nullptr));
          return hr;
        },
        value == nullptr);
  }

  HRESULT VerifyNativeSlotEmpty() noexcept {
    return Call([&] { return CheckNativeSlot(); });
  }

  HRESULT Stop() noexcept {
    stopped_.store(true);
    try {
      std::unique_lock<std::recursive_mutex> lock(gate_, std::try_to_lock);
      // The in-flight writer clears again when it returns. The owner also
      // disconnects the root; this is not an acknowledgement of native clear.
      if (!lock.owns_lock() || writing_)
        return S_FALSE;
      return child_ ? Remember(child_->put_Brush(nullptr)) : S_OK;
    } catch (...) {
      return Remember(E_UNEXPECTED);
    }
  }

  HRESULT Failure() const noexcept { return failure_.load(); }
  HRESULT LastSetter() const noexcept { return lastSetter_.load(); }
  ULONG Publications() const noexcept { return publications_.load(); }
  DWORD LastThread() const noexcept { return lastThread_.load(); }

 private:
  HRESULT CheckNativeSlot() noexcept {
    Microsoft::WRL::ComPtr<child_abi::ICompositionBrush> brush;
    const HRESULT hr = backdrop_->get_SystemBackdrop(&brush);
    if (FAILED(hr))
      return hr;
    return brush ? E_UNEXPECTED : hr;
  }

  HRESULT Remember(HRESULT hr) noexcept {
    if (FAILED(hr)) {
      HRESULT expected = S_OK;
      failure_.compare_exchange_strong(expected, hr);
    }
    return hr;
  }

  template <typename F>
  HRESULT Call(F&& call, bool allowStopped = false) noexcept {
    try {
      std::unique_lock<std::recursive_mutex> lock(gate_, std::try_to_lock);
      // Never block the UI waiting for a controller callback, or queue a
      // synchronous WinRT setter and report success before it executes.
      if (!lock.owns_lock())
        return Remember(RPC_E_CALL_REJECTED);
      // Stop fences new material, not observation or the controller's NULL
      // publication during Close. Cleanup must not depend on a healthy slot.
      if (stopped_.load() && !allowStopped)
        return RO_E_CLOSED;
      return Remember(call());
    } catch (...) {
      return Remember(E_UNEXPECTED);
    }
  }

  Microsoft::WRL::ComPtr<child_abi::ICompositionTarget> target_;
  Microsoft::WRL::ComPtr<child_abi::ICompositionObject> object_;
  Microsoft::WRL::ComPtr<child_abi::ICompositionSupportsSystemBackdrop>
      backdrop_;
  Microsoft::WRL::ComPtr<child_abi::ISpriteVisual> child_;
  std::recursive_mutex gate_;
  bool writing_ = false;
  std::atomic<bool> stopped_{false};
  std::atomic<HRESULT> failure_{S_OK};
  std::atomic<HRESULT> lastSetter_{E_PENDING};
  std::atomic<ULONG> publications_{0};
  std::atomic<DWORD> lastThread_{0};
};

}  // namespace weasel_acrylic
