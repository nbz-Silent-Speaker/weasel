$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$navigationPath = Join-Path $root 'WeaselDeployer/SettingsNavigation.h'
$navigation = Get-Content -LiteralPath $navigationPath -Raw
$resourcePath = Join-Path $root 'WeaselDeployer/WeaselDeployer.rc'
$resource = Get-Content -LiteralPath $resourcePath -Raw

$requiredConstants = [ordered]@{
  kContentWidthDlu = 540
  kContentHeightDlu = 286
  kSidebarWidthDlu = 116
  kPageInsetDlu = 14
  kPageBodyWidthDlu = 512
  kBottomActionLeftDlu = 14
  kBottomActionTopDlu = 258
  kFirstCardTopDlu = 14
  kActionButtonWidthDlu = 80
  kSecondaryButtonWidthDlu = 68
  kTransientButtonWidthDlu = 60
  kToggleGapDlu = 2
  kButtonHeightDlu = 18
  kCardRadiusDlu = 8
  kControlRadiusDlu = 5
  kSingleRowCardHeightDlu = 32
  kCompactToggleHeightDlu = 16
}
foreach ($entry in $requiredConstants.GetEnumerator()) {
  $pattern = 'inline constexpr int ' + [regex]::Escape($entry.Key) +
    '\s*=\s*' + $entry.Value + ';'
  if ($navigation -notmatch $pattern) {
    throw "Settings layout contract changed or missing: $($entry.Key)"
  }
}

$resourceTemplates = [ordered]@{
  'IDD_SWITCHER_SETTING' = 3
  'IDD_STYLE_SETTING' = 3
  'IDD_FONT_SETTING' = 1
  'IDD_STATUS_ICON_SETTING' = 1
}
foreach ($entry in $resourceTemplates.GetEnumerator()) {
  $pattern = '(?m)^' + [regex]::Escape($entry.Key) +
    '\s+DIALOGEX\s+0,\s+0,\s+540,\s+286\s*$'
  $count = [regex]::Matches($resource, $pattern).Count
  if ($count -ne $entry.Value) {
    throw "$($entry.Key) must use the shared 540 x 286 content frame " +
      "in all resource variants; found $count of $($entry.Value)."
  }
}

$dialogHeaders = Get-ChildItem -LiteralPath (Join-Path $root 'WeaselDeployer') `
  -Filter '*SettingsDialog.h'
foreach ($header in $dialogHeaders) {
  $headerText = Get-Content -LiteralPath $header.FullName -Raw
  if ($headerText -notmatch 'SettingsNavigation\.h') {
    continue
  }
  $idMatch = [regex]::Match($headerText, 'enum\s*\{\s*IDD\s*=\s*(\w+)\s*\}')
  if (-not $idMatch.Success) {
    throw "$($header.Name) has no dialog resource identifier."
  }
  $dialogId = $idMatch.Groups[1].Value
  $pattern = '(?m)^' + [regex]::Escape($dialogId) +
    '\s+DIALOGEX\s+0,\s+0,\s+540,\s+286\s*$'
  if (-not [regex]::IsMatch($resource, $pattern)) {
    throw "$($header.Name) does not use the shared 540 x 286 resource frame."
  }
}

$pageMatch = [regex]::Match($navigation, 'enum class Page\s*\{([^}]+)\}')
if (-not $pageMatch.Success) {
  throw 'Settings page registry is missing.'
}
$registeredPages = @(
  $pageMatch.Groups[1].Value.Split(',') |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ }
)

$installedPages = [System.Collections.Generic.HashSet[string]]::new()
$dialogSources = Get-ChildItem -LiteralPath (Join-Path $root 'WeaselDeployer') `
  -Filter '*SettingsDialog.cpp'
foreach ($source in $dialogSources) {
  $text = Get-Content -LiteralPath $source.FullName -Raw
  $matches = [regex]::Matches(
    $text,
    'settings_navigation::Install\([\s\S]{0,240}?Page::(\w+)'
  )
  foreach ($match in $matches) {
    [void]$installedPages.Add($match.Groups[1].Value)
  }
}

$missing = @($registeredPages | Where-Object { -not $installedPages.Contains($_) })
if ($missing.Count -ne 0) {
  throw 'Settings pages bypass the shared fixed-size frame: ' +
    ($missing -join ', ')
}
if ($navigation -notmatch 'ResizeContentFrame\(dialog\);') {
  throw 'Shared settings frame no longer enforces its client size.'
}
if ($navigation -notmatch 'SetWindowTextW\([\s\S]{0,80}?LocalText\(L"小狼毫设置"') {
  throw 'Shared settings window title is not applied by the common frame.'
}
if ($navigation -notmatch 'LocalText\(L"小狼毫设置"') {
  throw 'Shared settings window title is not centralized.'
}
if ($navigation -notmatch 'DisableWindowTransitions\(dialog\);') {
  throw 'Shared settings frame allows top-level transition flashes.'
}

$configuratorPath = Join-Path $root 'WeaselDeployer/Configurator.cpp'
$configurator = Get-Content -LiteralPath $configuratorPath -Raw
if ($configurator -notmatch 'class\s+SettingsPageInstance' -or
    $configurator -notmatch 'kHostNavigateMessage') {
  throw 'Settings pages no longer use the shared navigation host.'
}
$showReplacement = $configurator.IndexOf('SWP_NOACTIVATE | SWP_SHOWWINDOW')
$hideCurrent = $configurator.IndexOf('ShowWindow(active->window(), SW_HIDE)')
if ($showReplacement -lt 0 -or $hideCurrent -lt 0 -or
    $showReplacement -gt $hideCurrent) {
  throw 'A replacement settings page must be painted before the current page is hidden.'
}

$hostedSources = @(
  'SwitcherSettingsDialog.cpp'
  'UIStyleSettingsDialog.cpp'
  'FontSettingsDialog.cpp'
  'StatusIconSettingsDialog.cpp'
)
foreach ($source in $hostedSources) {
  $path = Join-Path $root ('WeaselDeployer/' + $source)
  $text = Get-Content -LiteralPath $path -Raw
  if ($text -notmatch 'settings_navigation::RequestNavigate\(m_hWnd, id\)') {
    throw "$source bypasses the shared navigation host."
  }
}

$restoreButtons = [ordered]@{
  'UIStyleSettingsDialog.cpp' = 'IDC_RESTORE_APPEARANCE'
  'FontSettingsDialog.cpp' = 'IDC_FONT_RESTORE'
  'StatusIconSettingsDialog.cpp' = 'IDC_STATUS_RESTORE'
}
foreach ($entry in $restoreButtons.GetEnumerator()) {
  $path = Join-Path $root ('WeaselDeployer/' + $entry.Key)
  $text = Get-Content -LiteralPath $path -Raw
  if ($text -notmatch 'StyleActionButton\(' -or
      $text -notmatch [regex]::Escape($entry.Value)) {
    throw "$($entry.Key) does not use the shared restore-button style."
  }
  if ($text -notmatch 'kBottomActionLeftDlu' -or
      $text -notmatch 'kBottomActionTopDlu') {
    throw "$($entry.Key) does not use the shared bottom-left restore position."
  }
}

$appearancePath = Join-Path $root 'WeaselDeployer/UIStyleSettingsDialog.cpp'
$appearance = Get-Content -LiteralPath $appearancePath -Raw
$appearanceContracts = @(
  'MoveControl\(dialog, IDC_APPEARANCE_ACRYLIC_CARD,[\s\S]{0,180}?' +
    'kSingleRowCardHeightDlu\);'
  'MoveControl\(dialog, IDC_APPEARANCE_THEME_CARD,[\s\S]{0,160}?' +
    'kSingleRowCardHeightDlu\);'
  'MoveControl\(dialog, IDC_COLOR_FAMILY, 78, 154, 124,'
  'MoveControl\(dialog, IDC_COLOR_LIGHT, 78, 154, 124,'
  'MoveControl\(dialog, IDC_COLOR_DARK, 78, 185, 124,'
  'MoveControl\(dialog, IDC_PREVIEW_LIGHT, kPreviewColumnLeftDlu,'
  'MoveControl\(dialog, IDC_PREVIEW_DARK, kPreviewColumnLeftDlu,'
  'L"恢复本页默认"'
)
foreach ($pattern in $appearanceContracts) {
  if ($appearance -notmatch $pattern) {
    throw "Candidate-window layout contract changed or missing: $pattern"
  }
}

if ($appearance -notmatch 'single_\.fill\(single\)' -or
    $appearance -notmatch 'single_\.fill\(false\)' -or
    $appearance -notmatch 'AppearanceThemeMode::FollowSystem' -or
    $appearance -notmatch 'RefreshThemeAvailability\(\)') {
  throw 'Candidate-window theme mode no longer drives the paired/single editor.'
}

$userSettingsPath = Join-Path $root 'include/WeaselUserSettings.h'
$userSettings = Get-Content -LiteralPath $userSettingsPath -Raw
if ($userSettings -notmatch 'enum class AppearanceThemeMode' -or
    $userSettings -notmatch 'ResolveAppearanceDarkMode') {
  throw 'Candidate-window theme mode is not persisted and resolved centrally.'
}

$previewPath = Join-Path $root 'WeaselDeployer/AppearancePreview.h'
$preview = Get-Content -LiteralPath $previewPath -Raw
if ($preview -notmatch 'std::array<std::wstring, 4> candidates' -or
    $preview -notmatch 'L"1\.", L"2\.", L"3\.", L"4\."') {
  throw 'Candidate-window previews must render four candidates.'
}

$hiddenPageTitles = [ordered]@{
  'SwitcherSettingsDialog.cpp' = 'IDC_SWITCHER_TITLE'
  'UIStyleSettingsDialog.cpp' = 'IDC_APPEARANCE_TITLE'
  'FontSettingsDialog.cpp' = 'IDC_FONT_TITLE'
  'StatusIconSettingsDialog.cpp' = 'IDC_STATUS_TITLE'
}
foreach ($entry in $hiddenPageTitles.GetEnumerator()) {
  $path = Join-Path $root ('WeaselDeployer/' + $entry.Key)
  $text = Get-Content -LiteralPath $path -Raw
  $pattern = 'settings_navigation::Install\([\s\S]{0,420}?' +
    [regex]::Escape($entry.Value)
  if ($text -notmatch $pattern) {
    throw "$($entry.Key) exposes a duplicate page title."
  }
}

Write-Output ('Settings layout contract verified for: ' +
  (($registeredPages | Sort-Object) -join ', '))
