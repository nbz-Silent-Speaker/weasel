#include "stdafx.h"
#include <string>
#include <algorithm>
#include <map>
#include <WeaselUI.h>

using namespace weasel;
#define STYLEORWEIGHT (L":[^:]*[^a-f0-9:]+[^:]*")

vector<wstring> ws_split(const wstring& in, const wstring& delim) {
  std::wregex re{delim};
  return vector<wstring>{
      std::wsregex_token_iterator(in.begin(), in.end(), re, -1),
      std::wsregex_token_iterator()};
}

DirectWriteResources::DirectWriteResources(weasel::UIStyle& style,
                                           UINT dpi = 96)
    : _style(style),
      font_settings_(FontSettings::Load()),
      dpiScaleFontPoint(0),
      dpiScaleLayout(0),
      pD2d1Factory(NULL),
      pDWFactory(NULL),
      pRenderTarget(NULL),
      pBrush(NULL),
      pTextLayout(NULL),
      pPreeditTextFormat(NULL),
      pTextFormat(NULL),
      pLabelTextFormat(NULL),
      pCommentTextFormat(NULL) {
  D2D1_TEXT_ANTIALIAS_MODE mode =
      _style.antialias_mode <= 3
          ? (D2D1_TEXT_ANTIALIAS_MODE)(_style.antialias_mode)
          : D2D1_TEXT_ANTIALIAS_MODE_FORCE_DWORD;  // prepare d2d1 resources
                                                   // create factory
  HR(::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                         pD2d1Factory.ReleaseAndGetAddressOf()));
  // create IDWriteFactory
  HR(DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(pDWFactory.ReleaseAndGetAddressOf())));
  _ResolveFontSettings();
  /* ID2D1HwndRenderTarget */
  const D2D1_PIXEL_FORMAT format = D2D1::PixelFormat(
      DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
  const D2D1_RENDER_TARGET_PROPERTIES properties =
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, format);
  HR(pD2d1Factory->CreateDCRenderTarget(&properties, &pRenderTarget));
  pRenderTarget->SetTextAntialiasMode(mode);
  pRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  HR(pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
                                          pBrush.ReleaseAndGetAddressOf()));
  // get the dpi information
  dpiScaleFontPoint = dpiScaleLayout = (float)dpi;
  dpiScaleFontPoint /= 72.0f;
  dpiScaleLayout /= 96.0f;

  InitResources(style, dpi);
}

DirectWriteResources::~DirectWriteResources() {}

namespace {
bool IsChineseCodePoint(UINT32 codepoint) {
  return (codepoint >= 0x2e80 && codepoint <= 0x303f) ||
         (codepoint >= 0x31c0 && codepoint <= 0x31ef) ||
         (codepoint >= 0x3400 && codepoint <= 0x4dbf) ||
         (codepoint >= 0x4e00 && codepoint <= 0x9fff) ||
         (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
         (codepoint >= 0x20000 && codepoint <= 0x2fa1f) ||
         (codepoint >= 0xff01 && codepoint <= 0xff60);
}

std::wstring LocalizedFontName(IDWriteLocalizedStrings* names) {
  if (!names || names->GetCount() == 0)
    return {};

  UINT32 index = 0;
  BOOL exists = FALSE;
  wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
  if (::GetUserDefaultLocaleName(locale, _countof(locale)))
    names->FindLocaleName(locale, &index, &exists);
  if (!exists)
    names->FindLocaleName(L"en-us", &index, &exists);
  if (!exists)
    index = 0;

  UINT32 length = 0;
  if (FAILED(names->GetStringLength(index, &length)))
    return {};
  std::wstring name(static_cast<size_t>(length) + 1, L'\0');
  if (FAILED(names->GetString(index, name.data(), length + 1)))
    return {};
  name.resize(length);
  return name;
}

ResolvedFontChoice ResolveFontChoice(IDWriteFactory2* factory,
                                     const FontChoice& choice) {
  ResolvedFontChoice resolved;
  resolved.family = std::regex_replace(
      choice.family, std::wregex(STYLEORWEIGHT, std::wregex::icase), L"");

  ComPtr<IDWriteGdiInterop> interop;
  ComPtr<IDWriteFont> font;
  LOGFONTW logical = {};
  logical.lfCharSet = DEFAULT_CHARSET;
  logical.lfWeight = FW_DONTCARE;
  wcsncpy_s(logical.lfFaceName, resolved.family.c_str(), _TRUNCATE);
  if (factory && SUCCEEDED(factory->GetGdiInterop(&interop)) && interop &&
      SUCCEEDED(interop->CreateFontFromLOGFONT(&logical, &font)) && font) {
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    if (SUCCEEDED(font->GetFontFamily(&family)) && family &&
        SUCCEEDED(family->GetFamilyNames(&names))) {
      const std::wstring family_name = LocalizedFontName(names.Get());
      if (!family_name.empty())
        resolved.family = family_name;
    }
    resolved.weight = font->GetWeight();
    resolved.style = font->GetStyle();
  }

  if (choice.shape == FontShape::Bold)
    resolved.weight = DWRITE_FONT_WEIGHT_BOLD;
  else if (choice.shape == FontShape::Italic)
    resolved.style = DWRITE_FONT_STYLE_ITALIC;
  return resolved;
}

FontRole TextRole(const DirectWriteResources& resources,
                  IDWriteTextFormat1* format) {
  if (format == resources.pPreeditTextFormat.Get())
    return FontRole::Preedit;
  if (format == resources.pLabelTextFormat.Get())
    return FontRole::Label;
  if (format == resources.pCommentTextFormat.Get())
    return FontRole::Comment;
  return FontRole::Candidate;
}
}  // namespace

HRESULT DirectWriteResources::CreateTextLayout(
    const std::wstring& text,
    const int& nCount,
    IDWriteTextFormat1* const txtFormat,
    const float& width,
    const float& height) {
  const HRESULT result = pDWFactory->CreateTextLayout(
      text.c_str(), nCount, txtFormat, width, height,
      reinterpret_cast<IDWriteTextLayout**>(
          pTextLayout.ReleaseAndGetAddressOf()));
  if (FAILED(result) || !pTextLayout || !font_settings_.enabled)
    return result;

  const UINT32 count =
      static_cast<UINT32>((std::min)(text.size(), static_cast<size_t>(nCount)));
  const FontRole role = TextRole(*this, txtFormat);
  UINT32 start = 0;
  while (start < count) {
    const auto language_at = [&](UINT32 index) {
      UINT32 codepoint = text[index];
      if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < count) {
        const UINT32 low = text[index + 1];
        if (low >= 0xdc00 && low <= 0xdfff) {
          codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
        }
      }
      return IsChineseCodePoint(codepoint) ? FontLanguage::Chinese
                                           : FontLanguage::Latin;
    };
    const FontLanguage language = language_at(start);
    UINT32 end = start + 1;
    if (text[start] >= 0xd800 && text[start] <= 0xdbff && end < count &&
        text[end] >= 0xdc00 && text[end] <= 0xdfff) {
      ++end;
    }
    while (end < count && language_at(end) == language) {
      if (text[end] >= 0xd800 && text[end] <= 0xdbff && end + 1 < count &&
          text[end + 1] >= 0xdc00 && text[end + 1] <= 0xdfff) {
        end += 2;
      } else {
        ++end;
      }
    }
    const auto& choice = font_settings_.At(role, language);
    const auto& resolved =
        resolved_font_settings_[FontSettings::Index(role, language)];
    const DWRITE_TEXT_RANGE range{start, end - start};
    if (!resolved.family.empty())
      pTextLayout->SetFontFamilyName(resolved.family.c_str(), range);
    pTextLayout->SetFontSize(choice.point * dpiScaleFontPoint, range);
    pTextLayout->SetFontWeight(resolved.weight, range);
    pTextLayout->SetFontStyle(resolved.style, range);
    start = end;
  }
  return result;
}

void DirectWriteResources::ReloadUserSettings() {
  font_settings_ = FontSettings::Load();
  _ResolveFontSettings();
}

void DirectWriteResources::_ResolveFontSettings() {
  for (size_t index = 0; index < FontSettings::kChoiceCount; ++index) {
    resolved_font_settings_[index] =
        ResolveFontChoice(pDWFactory.Get(), font_settings_.choices[index]);
  }
}

HRESULT DirectWriteResources::InitResources(const wstring& label_font_face,
                                            const int& label_font_point,
                                            const wstring& font_face,
                                            const int& font_point,
                                            const wstring& comment_font_face,
                                            const int& comment_font_point,
                                            const bool& vertical_text) {
  // prepare d2d1 resources
  DWRITE_WORD_WRAPPING wrapping =
      ((_style.max_width == 0 &&
        _style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT) ||
       (_style.max_height == 0 &&
        _style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT))
          ? DWRITE_WORD_WRAPPING_NO_WRAP
          : DWRITE_WORD_WRAPPING_WHOLE_WORD;
  DWRITE_WORD_WRAPPING wrapping_preedit =
      ((_style.max_width == 0 &&
        _style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT) ||
       (_style.max_height == 0 &&
        _style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT))
          ? DWRITE_WORD_WRAPPING_NO_WRAP
          : DWRITE_WORD_WRAPPING_CHARACTER;
  DWRITE_FLOW_DIRECTION flow = _style.vertical_text_left_to_right
                                   ? DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT
                                   : DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;

  // set main font a invalid font name, to make every font range customizable
  const wstring _mainFontFace = L"_InvalidFontName_";
  DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
  DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL;
  // convert percentage to float
  float linespacing = dpiScaleFontPoint * ((float)_style.linespacing / 100.0f);
  float baseline = dpiScaleFontPoint * ((float)_style.baseline / 100.0f);
  if (_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
    baseline = linespacing / 2;

  auto init_font = [&](const wstring& fontface, int fontpoint,
                       ComPtr<IDWriteTextFormat1>& _pTextFormat,
                       DWRITE_WORD_WRAPPING wrap) {
    vector<wstring> fontFaceStrVector;
    // text font text format set up
    fontFaceStrVector = ws_split(fontface, L",");
    // setup weight and style by the first unit of fontface setting string
    _ParseFontFace(fontface, fontWeight, fontStyle);
    fontFaceStrVector[0] =
        std::regex_replace(fontFaceStrVector[0],
                           std::wregex(STYLEORWEIGHT, std::wregex::icase), L"");
    // create text format with invalid font point will 'FAILED', no HR
    pDWFactory->CreateTextFormat(_mainFontFace.c_str(), NULL, fontWeight,
                                 fontStyle, DWRITE_FONT_STRETCH_NORMAL,
                                 fontpoint * dpiScaleFontPoint, L"",
                                 reinterpret_cast<IDWriteTextFormat**>(
                                     _pTextFormat.ReleaseAndGetAddressOf()));
    if (_pTextFormat != NULL) {
      if (vertical_text) {
        HR(_pTextFormat->SetFlowDirection(flow));
        HR(_pTextFormat->SetReadingDirection(
            DWRITE_READING_DIRECTION_TOP_TO_BOTTOM));
        HR(_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
      } else
        HR(_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));

      HR(_pTextFormat->SetParagraphAlignment(
          DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
      HR(_pTextFormat->SetWordWrapping(wrapping));
      _SetFontFallback(_pTextFormat, fontFaceStrVector);
      if (_style.linespacing && _style.baseline)
        _pTextFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                     fontpoint * linespacing,
                                     fontpoint * baseline);
    }
    decltype(fontFaceStrVector)().swap(fontFaceStrVector);
  };
  init_font(font_face, font_point, pTextFormat, wrapping);
  init_font(font_face, font_point, pPreeditTextFormat, wrapping_preedit);
  init_font(label_font_face, label_font_point, pLabelTextFormat, wrapping);
  init_font(comment_font_face, comment_font_point, pCommentTextFormat,
            wrapping);
  return S_OK;
}

HRESULT DirectWriteResources::InitResources(const UIStyle& style,
                                            const UINT& dpi = 96) {
  _style = style;
  if (dpi) {
    dpiScaleFontPoint = dpi / 72.0f;
    dpiScaleLayout = dpi / 96.0f;
  }
  return InitResources(style.label_font_face, style.label_font_point,
                       style.font_face, style.font_point,
                       style.comment_font_face, style.comment_font_point,
                       style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT);
}

void weasel::DirectWriteResources::SetDpi(const UINT& dpi) {
  dpiScaleFontPoint = dpi / 72.0f;
  dpiScaleLayout = dpi / 96.0f;

  InitResources(_style);
}

static wstring _MatchWordsOutLowerCaseTrim1st(const wstring& wstr,
                                              const wstring& pat) {
  wstring mat = L"";
  std::wsmatch mc;
  std::wregex pattern(pat, std::wregex::icase);
  wstring::const_iterator iter = wstr.cbegin();
  wstring::const_iterator end = wstr.cend();
  while (regex_search(iter, end, mc, pattern)) {
    for (const auto& m : mc) {
      mat = m;
      mat = mat.substr(1);
      break;
    }
    iter = mc.suffix().first;
  }
  wstring res;
  std::transform(mat.begin(), mat.end(), std::back_inserter(res), ::tolower);
  return res;
}

void DirectWriteResources::_ParseFontFace(const wstring& fontFaceStr,
                                          DWRITE_FONT_WEIGHT& fontWeight,
                                          DWRITE_FONT_STYLE& fontStyle) {
  const wstring patWeight(
      L"(:thin|:extra_light|:ultra_light|:light|:semi_light|:medium|:demi_bold|"
      L":semi_bold|:bold|:extra_bold|:ultra_bold|:black|:heavy|:extra_black|:"
      L"ultra_black)");
  const std::map<wstring, DWRITE_FONT_WEIGHT> _mapWeight = {
      {L"thin", DWRITE_FONT_WEIGHT_THIN},
      {L"extra_light", DWRITE_FONT_WEIGHT_EXTRA_LIGHT},
      {L"ultra_light", DWRITE_FONT_WEIGHT_ULTRA_LIGHT},
      {L"light", DWRITE_FONT_WEIGHT_LIGHT},
      {L"semi_light", DWRITE_FONT_WEIGHT_SEMI_LIGHT},
      {L"medium", DWRITE_FONT_WEIGHT_MEDIUM},
      {L"demi_bold", DWRITE_FONT_WEIGHT_DEMI_BOLD},
      {L"semi_bold", DWRITE_FONT_WEIGHT_SEMI_BOLD},
      {L"bold", DWRITE_FONT_WEIGHT_BOLD},
      {L"extra_bold", DWRITE_FONT_WEIGHT_EXTRA_BOLD},
      {L"ultra_bold", DWRITE_FONT_WEIGHT_ULTRA_BOLD},
      {L"black", DWRITE_FONT_WEIGHT_BLACK},
      {L"heavy", DWRITE_FONT_WEIGHT_HEAVY},
      {L"extra_black", DWRITE_FONT_WEIGHT_EXTRA_BLACK},
      {L"normal", DWRITE_FONT_WEIGHT_NORMAL},
      {L"ultra_black", DWRITE_FONT_WEIGHT_ULTRA_BLACK}};
  wstring weight = _MatchWordsOutLowerCaseTrim1st(fontFaceStr, patWeight);
  auto it = _mapWeight.find(weight);
  fontWeight =
      (it != _mapWeight.end()) ? it->second : DWRITE_FONT_WEIGHT_NORMAL;

  const wstring patStyle(L"(:italic|:oblique|:normal)");
  const std::map<wstring, DWRITE_FONT_STYLE> _mapStyle = {
      {L"italic", DWRITE_FONT_STYLE_ITALIC},
      {L"oblique", DWRITE_FONT_STYLE_OBLIQUE},
      {L"normal", DWRITE_FONT_STYLE_NORMAL},
  };
  wstring style = _MatchWordsOutLowerCaseTrim1st(fontFaceStr, patStyle);
  auto it2 = _mapStyle.find(style);
  fontStyle = (it2 != _mapStyle.end()) ? it2->second : DWRITE_FONT_STYLE_NORMAL;
}

static UINT GetValue(const wstring& wstr, const int fallback) {
  try {
    return std::stoul(wstr.c_str(), 0, 16);
  } catch (...) {
    return fallback;
  }
}

void DirectWriteResources::_SetFontFallback(
    ComPtr<IDWriteTextFormat1>& textFormat,
    const vector<wstring>& fontVector) {
  ComPtr<IDWriteFontFallback> pSysFallback;
  HR(pDWFactory->GetSystemFontFallback(pSysFallback.ReleaseAndGetAddressOf()));
  ComPtr<IDWriteFontFallback> pFontFallback = NULL;
  ComPtr<IDWriteFontFallbackBuilder> pFontFallbackBuilder = NULL;
  HR(pDWFactory->CreateFontFallbackBuilder(
      pFontFallbackBuilder.ReleaseAndGetAddressOf()));
  vector<wstring> fallbackFontsVector;
  for (UINT32 i = 0; i < fontVector.size(); i++) {
    fallbackFontsVector = ws_split(fontVector[i], L":");
    wstring _fontFaceWstr, firstWstr, lastWstr;
    if (fallbackFontsVector.size() == 3) {
      _fontFaceWstr = fallbackFontsVector[0];
      firstWstr = fallbackFontsVector[1];
      lastWstr = fallbackFontsVector[2];
      if (lastWstr.empty())
        lastWstr = L"10ffff";
      if (firstWstr.empty())
        firstWstr = L"0";
    } else if (fallbackFontsVector.size() == 2)  // fontName : codepoint
    {
      _fontFaceWstr = fallbackFontsVector[0];
      firstWstr = fallbackFontsVector[1];
      if (firstWstr.empty())
        firstWstr = L"0";
      lastWstr = L"10ffff";
    } else if (fallbackFontsVector.size() ==
               1)  // if only font defined, use all range
    {
      _fontFaceWstr = fallbackFontsVector[0];
      firstWstr = L"0";
      lastWstr = L"10ffff";
    }
    UINT first = GetValue(firstWstr, 0), last = GetValue(lastWstr, 0x10ffff);
    DWRITE_UNICODE_RANGE range = {first, last};
    const WCHAR* familys = {_fontFaceWstr.c_str()};
    HR(pFontFallbackBuilder->AddMapping(&range, 1, &familys, 1));
    decltype(fallbackFontsVector)().swap(fallbackFontsVector);
  }
  // add system defalt font fallback
  HR(pFontFallbackBuilder->AddMappings(pSysFallback.Get()));
  HR(pFontFallbackBuilder->CreateFontFallback(
      pFontFallback.ReleaseAndGetAddressOf()));
  HR(textFormat->SetFontFallback(pFontFallback.Get()));
  decltype(fallbackFontsVector)().swap(fallbackFontsVector);
}
