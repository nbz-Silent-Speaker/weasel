#pragma once

#include "WeaselGaussianBlurEffect.h"
#include <winrt/Windows.UI.Composition.h>

namespace weasel_acrylic {

// AcrylicBackgroundFillColorBaseBrush in Microsoft's WinUI theme resources:
// controls/dev/Materials/Acrylic/AcrylicBrush_themeresources.xaml.
// Light also matches the installed DesktopAcrylicController Base defaults.
// These are material colors, independent of the scheme's foreground overlay.
struct AcrylicMaterialColors {
  winrt::Windows::UI::Color luminosity;
  winrt::Windows::UI::Color tint;
};

inline AcrylicMaterialColors BaseAcrylicColors(bool dark) {
  return dark ? AcrylicMaterialColors{{245, 32, 32, 32}, {128, 32, 32, 32}}
              : AcrylicMaterialColors{{230, 243, 243, 243}, {0, 243, 243, 243}};
}

// A D2D blend description, consumed by the system compositor without Win2D
// or Windows App SDK activation in the host process.
struct AcrylicBlendEffect
    : winrt::implements<AcrylicBlendEffect,
                        wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
  D2D1_BLEND_MODE mode = D2D1_BLEND_MODE_COLOR;
  wge::IGraphicsEffectSource background{nullptr};
  wge::IGraphicsEffectSource foreground{nullptr};

  HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
    if (!id)
      return E_INVALIDARG;
    *id = CLSID_D2D1Blend;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(
      LPCWSTR name,
      UINT* index,
      awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override {
    if (!name || !index || !mapping || _wcsicmp(name, L"Mode"))
      return E_INVALIDARG;
    *index = D2D1_BLEND_PROP_MODE;
    *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override {
    if (!count)
      return E_INVALIDARG;
    *count = 1;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetProperty(
      UINT index,
      ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
    if (!value)
      return E_INVALIDARG;
    *value = nullptr;
    if (index != D2D1_BLEND_PROP_MODE)
      return E_INVALIDARG;
    try {
      auto property =
          wf::PropertyValue::CreateUInt32(static_cast<UINT32>(mode));
      winrt::copy_to_abi(property, *reinterpret_cast<void**>(value));
      return S_OK;
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
    if (index > 1)
      return E_INVALIDARG;
    const auto& input = index == 0 ? background : foreground;
    if (input)
      winrt::copy_to_abi(input, *reinterpret_cast<void**>(source));
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override {
    if (!count)
      return E_INVALIDARG;
    *count = 2;
    return S_OK;
  }
  winrt::hstring Name() { return name_; }
  void Name(winrt::hstring value) { name_ = std::move(value); }

 private:
  winrt::hstring name_{L"WeaselAcrylicBlend"};
};

inline wge::IGraphicsEffect BuildAcrylicMaterial(
    const wge::IGraphicsEffectSource& blurred) {
  using winrt::Windows::UI::Composition::CompositionEffectSourceParameter;
  auto luminosity = winrt::make_self<AcrylicBlendEffect>();
  luminosity->Name(L"LuminosityBlend");
  // Follow Microsoft's AcrylicBrush::CombineNoiseWithTintEffect_Luminosity:
  // D2D's Color/Luminosity blend modes have reversed naming in this recipe.
  luminosity->mode = D2D1_BLEND_MODE_COLOR;
  luminosity->background = blurred;
  luminosity->foreground = CompositionEffectSourceParameter(L"luminosity");
  auto tint = winrt::make_self<AcrylicBlendEffect>();
  tint->Name(L"TintBlend");
  tint->mode = D2D1_BLEND_MODE_LUMINOSITY;
  tint->background = *luminosity;
  tint->foreground = CompositionEffectSourceParameter(L"tint");
  return *tint;
}

inline void SetAcrylicMaterialColors(
    const winrt::Windows::UI::Composition::CompositionColorBrush& luminosity,
    const winrt::Windows::UI::Composition::CompositionColorBrush& tint,
    bool dark) {
  const auto colors = BaseAcrylicColors(dark);
  luminosity.Color(colors.luminosity);
  tint.Color(colors.tint);
}

}  // namespace weasel_acrylic
