#include <WeaselUI.h>

#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace {
using weasel::FontLanguage;
using weasel::FontRole;
using weasel::FontSettings;

IDWriteTextFormat1* FormatFor(weasel::DirectWriteResources& resources,
                              FontRole role) {
  switch (role) {
    case FontRole::Preedit:
      return resources.pPreeditTextFormat.Get();
    case FontRole::Label:
      return resources.pLabelTextFormat.Get();
    case FontRole::Comment:
      return resources.pCommentTextFormat.Get();
    default:
      return resources.pTextFormat.Get();
  }
}

bool Verify(weasel::DirectWriteResources& resources,
            const FontSettings& expected) {
  for (size_t role_index = 0; role_index < FontSettings::kRoleCount;
       ++role_index) {
    const auto role = static_cast<FontRole>(role_index);
    for (size_t language_index = 0;
         language_index < FontSettings::kLanguageCount; ++language_index) {
      const auto language = static_cast<FontLanguage>(language_index);
      const wchar_t* text = language == FontLanguage::Chinese ? L"中" : L"a";
      const auto& choice = expected.At(role, language);
      if (FAILED(resources.CreateTextLayout(text, 1, FormatFor(resources, role),
                                            300.0f, 100.0f))) {
        return false;
      }
      UINT32 length = 0;
      if (FAILED(resources.pTextLayout->GetFontFamilyNameLength(0, &length)))
        return false;
      std::wstring family(length + 1, L'\0');
      if (FAILED(resources.pTextLayout->GetFontFamilyName(
              0, family.data(), static_cast<UINT32>(family.size())))) {
        return false;
      }
      family.resize(length);
      FLOAT size = 0;
      if (FAILED(resources.pTextLayout->GetFontSize(0, &size)))
        return false;
      const float expected_size = choice.point * (96.0f / 72.0f);
      if (_wcsicmp(family.c_str(), choice.family.c_str()) != 0 ||
          std::abs(size - expected_size) > 0.01f) {
        std::wcerr << L"role=" << role_index << L" language=" << language_index
                   << L" expected=" << choice.family << L"/" << expected_size
                   << L" actual=" << family << L"/" << size << std::endl;
        return false;
      }
    }
  }
  return true;
}

FontSettings MakeSettings(const wchar_t* family, DWORD point) {
  FontSettings result = FontSettings::Defaults();
  result.enabled = true;
  for (size_t index = 0; index < FontSettings::kChoiceCount; ++index) {
    result.choices[index].family = family;
    result.choices[index].point = point + static_cast<DWORD>(index);
  }
  return result;
}

bool VerifyReportedHelveticaFace(weasel::DirectWriteResources& resources) {
  FontSettings settings = MakeSettings(L"HelveticaNeueLT Pro 65 Md", 13);
  if (settings.Save() != ERROR_SUCCESS)
    return false;
  resources.ReloadUserSettings();
  if (FAILED(resources.CreateTextLayout(
          L"1.", 2, resources.pLabelTextFormat.Get(), 300.0f, 100.0f))) {
    return false;
  }
  UINT32 length = 0;
  if (FAILED(resources.pTextLayout->GetFontFamilyNameLength(0, &length)))
    return false;
  std::wstring family(length + 1, L'\0');
  if (FAILED(resources.pTextLayout->GetFontFamilyName(
          0, family.data(), static_cast<UINT32>(family.size())))) {
    return false;
  }
  family.resize(length);
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  if (FAILED(resources.pTextLayout->GetFontWeight(0, &weight)))
    return false;
  if (family.find(L"Helvetica") == std::wstring::npos ||
      weight < DWRITE_FONT_WEIGHT_MEDIUM) {
    std::wcerr << L"reported Helvetica face resolved unexpectedly: " << family
               << L", weight=" << static_cast<unsigned>(weight) << std::endl;
    return false;
  }
  std::wcout << L"HelveticaNeueLT Pro 65 Md resolved to " << family
             << L", weight=" << static_cast<unsigned>(weight) << L"."
             << std::endl;
  return true;
}
}  // namespace

int wmain() {
  _wputenv_s(L"WEASEL_SETTINGS_PREVIEW", L"1");
  const FontSettings original = FontSettings::Load();
  struct Restore {
    FontSettings value;
    ~Restore() { value.Save(); }
  } restore{original};

  const FontSettings first = MakeSettings(L"Arial", 8);
  if (first.Save() != ERROR_SUCCESS)
    return 10;

  weasel::UIStyle style;
  style.font_face = L"Segoe UI";
  style.label_font_face = L"Segoe UI";
  style.comment_font_face = L"Segoe UI";
  style.font_point = 11;
  style.label_font_point = 11;
  style.comment_font_point = 11;
  weasel::DirectWriteResources resources(style, 96);
  if (!Verify(resources, first))
    return 11;

  const FontSettings second = MakeSettings(L"Times New Roman", 12);
  if (second.Save() != ERROR_SUCCESS)
    return 12;
  resources.ReloadUserSettings();
  if (!Verify(resources, second))
    return 13;
  if (!VerifyReportedHelveticaFace(resources))
    return 14;

  std::wcout << L"PASS: all 8 role/language font choices loaded and live "
                L"reload replaced family and point size."
             << std::endl;
  return 0;
}
