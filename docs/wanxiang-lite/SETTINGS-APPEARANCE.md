# Settings window appearance

The independent Settings appearance entry sits between two sidebar separators,
above Apply. Navigation labels remain text-only; the active page retains its
pale accent tint and narrow accent marker.

This preference affects settings chrome only. It does not modify candidate
palettes, fonts, Rime YAML, page drafts, deployment state, or the Apply button.
The candidate previews retain their own configured light/dark colors.

## Behavior

- Window mode: Windows app mode, always light, or always dark.
- Accent: Windows accent, custom, or the default `#0A9DA1`.
- Solid selection text uses white through relative luminance 0.30, and black
  above it. Default teal therefore uses white. Tinted navigation/dropdown rows
  use the normal light/dark foreground.
- Edits take effect immediately. A 250 ms debounce saves them; closing flushes
  the pending value. The pane has no Apply/Cancel controls.
- The popup remains 460 by 542 logical pixels across modes. Only Custom shows
  the saturation/value plane and right-hand vertical hue strip. Other modes
  retain the space and show read-only color values.
- HEX/RGB/HSV/HSL/CMYK representations synchronize without changing the current
  color when switching representation. Invalid or incomplete input preserves
  the last valid color. The swatch, universal code field and representation
  selector share one row. The next row contains evenly distributed RGB, HSV,
  HSL or CMYK components and remains empty in HEX mode.

## Storage and supported input

`SettingsUiAppearance` is one registry string under
`HKCU\Software\Rime\Weasel\UserSettings`, in the existing common registry view.
It contains mode index, accent index and remembered custom `#RRGGBB`. The preview
uses `PreviewUserSettings` instead. Missing/malformed values use system mode and
the default teal accent. Save failures are reported rather than silently ignored.

The code field accepts `#RGB`, `#RRGGBB`, bare RGB hex, opaque `#RRGGBBAA`,
Rime `0xBBGGRR`/opaque `0xAABBGGRR`, and comma-separated `rgb`, `rgba`, `hsv`,
`hsl`, `hsla`, `cmyk` forms. RGB percentages are accepted; hue is degrees, HSV/HSL
S/V/L and CMYK are percentages. Settings accents are opaque; non-opaque alpha is
rejected. CMYK uses mathematical conversion, not a print color profile.

## Implementation and verification

`SettingsColor.h` provides pure parsing/conversion and contrast rules.
`SettingsColorPicker.h` is a reusable native child control with a color callback;
it has no preference writes or Rime dependency. `SettingsTheme.h` owns the palette,
Windows theme notifications and preference storage. `SettingsAppearancePopup.h`
connects the picker and the shared controls to that policy.

Run `WeaselDeployer/tests/RunSettingsAppearanceTests.ps1` from a Visual Studio
developer shell for color roundtrips and native-control state/lifecycle checks.
Run `RunAppearancePreviewTests.ps1` for preview rendering/cache invalidation,
including the settings page background, and `CheckSettingsLayout.ps1` for the
four-page layout contract. Visual acceptance still requires the native isolated
preview; these automated checks do not prove every monitor/DPI configuration.
