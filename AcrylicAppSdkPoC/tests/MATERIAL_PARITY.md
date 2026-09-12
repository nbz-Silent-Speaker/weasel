# Packaged-host Acrylic material correction

Baseline: `eab8f34` (CI 76). The generic packaged-host fallback blurred the
host backdrop but never applied the Acrylic material's luminosity/tint recipe.
Word's DesktopAcrylicController uses Base, while this fallback was blur-only.

The correction adds luminosity and tint blends only to the opted-in generic
packaged route. SearchHost, forced R22 comparisons, the native App SDK route,
candidate foreground geometry and TSF composition code are unchanged.

Reference recipe and Base theme parameters:

- https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/Materials/Acrylic/AcrylicBrush.cpp
  (`CombineNoiseWithTintEffect_Luminosity`, including the Color/Luminosity naming
  reversal documented by Microsoft).
- https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/Materials/Acrylic/AcrylicBrush_themeresources.xaml
  (`AcrylicBackgroundFillColorBaseBrush` in Default/Dark and Light).

Light: RGB F3F3F3, luminosity opacity 0.90, tint opacity 0.
Dark: RGB 202020, luminosity opacity 0.96, tint opacity 0.50.
CompositionColorBrush represents alpha as a byte, rounded to 230/245/128.

An independent local controller fixture also read the installed SDK's Base
defaults as F3F3F3 / 0.90 / 0. The property getters did not change with the
fixture's configuration theme, so they are not evidence for effective dark
output. Dark values above come from the official Base theme resources.
Local probe evidence is retained in R22-Material-Parity-20260912 (not shipped).

The fallback retains its existing blur radius/host-backdrop source; it does not
replicate the controller's noise texture or policy transitions. This correction
targets excessive backdrop contrast and theme response, not pixel-identical
rendering across different backends.

## Validation

`AcrylicMaterialTests` renders the production effect graph through Direct2D on
WARP and reads pixels over solid red and white backgrounds. It checks light
output suppresses strong backdrop color, dark output is dark, backdrop colors
remain distinguishable, alpha stays opaque, and light-dark-light restores the
same pixels. It also instantiates the graph with the real system compositor and
checks source brushes retain identity while colors change. CI executes x64 and
Win32 builds. The existing child-target suite checks routing exclusions.

The WARP adapter translates Composition's reversed Color/Luminosity blend
semantics to native Direct2D modes. WinUI's
`CombineNoiseWithTintEffect_Luminosity` explicitly documents the reversal;
its effect wrapper passes enum values and input order through unchanged.
Passing those values straight to native Direct2D instead gave light red
backdrop pixels of 94,69,69,255 in CI 78, failing the unchanged brightness
assertion. The translation is test-only; the production Composition recipe
retains Microsoft's mode selection. This is a semantic reference test, not a
readback of pixels rendered by the real compositor; compositor graph acceptance
and brush identity are checked separately below the pixel checks.

After installation, compare Codex and Word over the same colored background in
both themes, including candidate hide/show. User visual acceptance is separate
from the automated pixel tests. Download and installation are user-managed.

## Separate remaining issue

Screenshot/focus interruption can leave stale inline composition and suppress
the next candidate UI. Do not reintroduce CI 75's synchronous
`_FinalizeComposition` in `_EndComposition`: it caused host crashes and was
reverted in CI 76. That lifecycle fix must be reviewed/tested separately after
the material build is validated.
