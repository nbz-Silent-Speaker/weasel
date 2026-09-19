#pragma once

#include <windows.h>
#include <d2d1effects.h>
#include <windows.graphics.effects.interop.h>

#include <utility>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>

namespace weasel_acrylic {

namespace wf = winrt::Windows::Foundation;
namespace wge = winrt::Windows::Graphics::Effects;
namespace awge = ABI::Windows::Graphics::Effects;

// Minimal effect description consumed by Windows.UI.Composition. This avoids a
// Win2D runtime dependency while using the documented D2D Gaussian blur effect
// through IGraphicsEffectD2D1Interop.
struct GaussianBlurEffect
    : winrt::implements<GaussianBlurEffect,
                        wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
  HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
    if (!id)
      return E_INVALIDARG;
    *id = CLSID_D2D1GaussianBlur;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(
      LPCWSTR name,
      UINT* index,
      awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override {
    if (!name || !index || !mapping)
      return E_INVALIDARG;
    if (_wcsicmp(name, L"BlurAmount") == 0) {
      *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
      *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
      return S_OK;
    }
    if (_wcsicmp(name, L"Optimization") == 0) {
      *index = D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION;
      *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
      return S_OK;
    }
    if (_wcsicmp(name, L"BorderMode") == 0) {
      *index = D2D1_GAUSSIANBLUR_PROP_BORDER_MODE;
      *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
      return S_OK;
    }
    return E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override {
    if (!count)
      return E_INVALIDARG;
    *count = 3;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetProperty(
      UINT index,
      ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
    if (!value)
      return E_INVALIDARG;
    *value = nullptr;
    try {
      switch (index) {
        case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION: {
          auto property = wf::PropertyValue::CreateSingle(BlurAmount);
          winrt::copy_to_abi(property, *reinterpret_cast<void**>(value));
          return S_OK;
        }
        case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION: {
          auto property = wf::PropertyValue::CreateUInt32(
              static_cast<UINT32>(Optimization));
          winrt::copy_to_abi(property, *reinterpret_cast<void**>(value));
          return S_OK;
        }
        case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE: {
          auto property =
              wf::PropertyValue::CreateUInt32(static_cast<UINT32>(BorderMode));
          winrt::copy_to_abi(property, *reinterpret_cast<void**>(value));
          return S_OK;
        }
        default:
          return E_INVALIDARG;
      }
    } catch (...) {
      return winrt::to_hresult();
    }
  }

  HRESULT STDMETHODCALLTYPE
  GetSource(UINT index,
            awge::IGraphicsEffectSource** source) noexcept override {
    if (!source)
      return E_INVALIDARG;
    *source = nullptr;
    if (index != 0)
      return E_INVALIDARG;
    if (!Source)
      return S_OK;
    winrt::copy_to_abi(Source, *reinterpret_cast<void**>(source));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override {
    if (!count)
      return E_INVALIDARG;
    *count = 1;
    return S_OK;
  }

  winrt::hstring Name() { return name_; }
  void Name(winrt::hstring value) { name_ = std::move(value); }

  wge::IGraphicsEffectSource Source{nullptr};
  float BlurAmount = 18.0f;
  D2D1_GAUSSIANBLUR_OPTIMIZATION Optimization =
      D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED;
  D2D1_BORDER_MODE BorderMode = D2D1_BORDER_MODE_HARD;

 private:
  winrt::hstring name_{L"WeaselGaussianBlurEffect"};
};

}  // namespace weasel_acrylic
