#include "../AcrylicMaterial.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace weasel_acrylic;
using namespace winrt::Windows::UI::Composition;
using winrt::check_hresult;
using winrt::com_ptr;

void Check(bool value) {
  if (!value)
    throw std::runtime_error("Acrylic material check failed");
}

// Interpret the production effect descriptions through D2D on WARP. This
// checks actual blend output, including input order and D2D blend semantics,
// without screenshots, a visible window, or the host application's runtime.
struct Renderer {
  com_ptr<ID3D11Device> d3d;
  com_ptr<ID2D1Device> device;
  com_ptr<ID2D1DeviceContext> dc;
  std::map<std::wstring, com_ptr<ID2D1Image>> sources;

  Renderer() {
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                    0, D3D11_SDK_VERSION, d3d.put(), nullptr,
                                    nullptr));
    auto dxgi = d3d.as<IDXGIDevice>();
    check_hresult(D2D1CreateDevice(dxgi.get(), nullptr, device.put()));
    check_hresult(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                              dc.put()));
  }

  void Source(const wchar_t* name, winrt::Windows::UI::Color color) {
    const auto channel = [color](unsigned value) {
      return (value * color.A + 127) / 255;
    };
    const UINT32 bgra = channel(color.B) | (channel(color.G) << 8) |
                        (channel(color.R) << 16) | (unsigned(color.A) << 24);
    std::vector<UINT32> pixels(32 * 32, bgra);
    auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    com_ptr<ID2D1Bitmap1> bitmap;
    check_hresult(dc->CreateBitmap(D2D1::SizeU(32, 32), pixels.data(), 32 * 4,
                                   &properties, bitmap.put()));
    sources[name] = bitmap.as<ID2D1Image>();
  }

  com_ptr<ID2D1Image> Graph(const wge::IGraphicsEffectSource& source) {
    if (auto parameter = source.try_as<CompositionEffectSourceParameter>())
      return sources.at(std::wstring(parameter.Name()));
    auto description = source.as<awge::IGraphicsEffectD2D1Interop>();
    GUID id{};
    check_hresult(description->GetEffectId(&id));
    com_ptr<ID2D1Effect> effect;
    check_hresult(dc->CreateEffect(id, effect.put()));
    UINT count = 0;
    check_hresult(description->GetPropertyCount(&count));
    for (UINT i = 0; i < count; ++i) {
      wf::IPropertyValue value{nullptr};
      check_hresult(description->GetProperty(
          i, reinterpret_cast<ABI::Windows::Foundation::IPropertyValue**>(
                 winrt::put_abi(value))));
      if (value.Type() == wf::PropertyType::Single) {
        const float scalar = value.GetSingle();
        check_hresult(effect->SetValue(i, scalar));
      } else {
        UINT32 enumeration = value.GetUInt32();
        // Composition's Color/Luminosity names are reversed, as documented
        // by WinUI's CombineNoiseWithTintEffect_Luminosity. Native D2D does
        // not have that reversal. Translate only in this WARP adapter so
        // it renders the production Composition graph with the same meaning.
        if (id == CLSID_D2D1Blend && i == D2D1_BLEND_PROP_MODE) {
          if (enumeration == D2D1_BLEND_MODE_COLOR)
            enumeration = D2D1_BLEND_MODE_LUMINOSITY;
          else if (enumeration == D2D1_BLEND_MODE_LUMINOSITY)
            enumeration = D2D1_BLEND_MODE_COLOR;
        }
        check_hresult(effect->SetValue(
            i, D2D1_PROPERTY_TYPE_ENUM,
            reinterpret_cast<const BYTE*>(&enumeration), sizeof(enumeration)));
      }
    }
    check_hresult(description->GetSourceCount(&count));
    for (UINT i = 0; i < count; ++i) {
      wge::IGraphicsEffectSource input{nullptr};
      check_hresult(description->GetSource(
          i, reinterpret_cast<awge::IGraphicsEffectSource**>(
                 winrt::put_abi(input))));
      effect->SetInput(i, Graph(input).get());
    }
    com_ptr<ID2D1Image> output;
    effect->GetOutput(output.put());
    return output;
  }

  std::array<unsigned, 4> Pixel(const wge::IGraphicsEffectSource& source) {
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_PREMULTIPLIED);
    auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format);
    com_ptr<ID2D1Bitmap1> target;
    check_hresult(dc->CreateBitmap(D2D1::SizeU(32, 32), nullptr, 0, &properties,
                                   target.put()));
    auto output = Graph(source);
    dc->SetTarget(target.get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc->DrawImage(output.get());
    check_hresult(dc->EndDraw());
    dc->SetTarget(nullptr);
    properties.bitmapOptions =
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    com_ptr<ID2D1Bitmap1> readback;
    check_hresult(dc->CreateBitmap(D2D1::SizeU(32, 32), nullptr, 0, &properties,
                                   readback.put()));
    check_hresult(readback->CopyFromBitmap(nullptr, target.get(), nullptr));
    D2D1_MAPPED_RECT map{};
    check_hresult(readback->Map(D2D1_MAP_OPTIONS_READ, &map));
    const BYTE* pixel = map.bits + 16 * map.pitch + 16 * 4;
    std::array<unsigned, 4> result{pixel[2], pixel[1], pixel[0], pixel[3]};
    check_hresult(readback->Unmap());
    return result;
  }
};

int main() {
  try {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    {
      Renderer renderer;
      auto blur = winrt::make_self<GaussianBlurEffect>();
      blur->Source = CompositionEffectSourceParameter(L"source");
      auto graph = BuildAcrylicMaterial(*blur);
      auto source = graph.as<wge::IGraphicsEffectSource>();
      std::array<unsigned, 4> first{};
      for (bool dark : {false, true, false}) {
        const auto colors = BaseAcrylicColors(dark);
        renderer.Source(L"luminosity", colors.luminosity);
        renderer.Source(L"tint", colors.tint);
        renderer.Source(L"source", {255, 255, 0, 0});
        const auto red = renderer.Pixel(source);
        std::cout << (dark ? "dark" : "light") << " red backdrop: " << red[0]
                  << ',' << red[1] << ',' << red[2] << ',' << red[3] << '\n';
        Check(red[3] == 255);
        if (dark) {
          Check(red[0] < 120 && red[1] < 100 && red[2] < 100);
        } else {
          Check(red[0] > 200 && red[1] > 150 && red[2] > 150);
          if (first[3])
            Check(first == red);
          first = red;
        }
        renderer.Source(L"source", {255, 255, 255, 255});
        const auto white = renderer.Pixel(source);
        Check(white[3] == 255 && white != red);
        Check(dark ? white[1] < 100 : white[1] > 240);
      }

      // The real compositor must also accept the production graph and keep
      // source brush identity through a theme change.
      Compositor compositor;
      auto factory = compositor.CreateEffectFactory(graph);
      auto brush = factory.CreateBrush();
      auto luminosity = compositor.CreateColorBrush();
      auto tint = compositor.CreateColorBrush();
      brush.SetSourceParameter(L"source",
                               compositor.CreateColorBrush({255, 255, 0, 0}));
      brush.SetSourceParameter(L"luminosity", luminosity);
      brush.SetSourceParameter(L"tint", tint);
      for (bool dark : {false, true, false}) {
        SetAcrylicMaterialColors(luminosity, tint, dark);
        Check(luminosity.Color() == BaseAcrylicColors(dark).luminosity);
        Check(tint.Color() == BaseAcrylicColors(dark).tint);
        Check(brush.GetSourceParameter(L"luminosity") == luminosity);
        Check(brush.GetSourceParameter(L"tint") == tint);
      }
      brush.Close();
      factory.Close();
      compositor.Close();
    }
    winrt::uninit_apartment();
    std::cout << "PASS: WARP pixel output and compositor theme roundtrip\n";
    return 0;
  } catch (winrt::hresult_error const& error) {
    std::cerr << "HRESULT " << std::hex << error.code().value << '\n';
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
