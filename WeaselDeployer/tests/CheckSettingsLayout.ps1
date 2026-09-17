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
  kToggleGapDlu = 0
  kButtonHeightDlu = 18
  kCardRadiusDlu = 8
  kControlCornerRadiusPx = 5
  kSingleRowCardHeightDlu = 32
  kCompactToggleHeightDlu = 14
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
$freezeHost = $configurator.IndexOf('SendMessageW(host.window(), WM_SETREDRAW, FALSE')
$hideCurrent = $configurator.IndexOf('ShowWindow(active->window(), SW_HIDE)')
$showReplacement = $configurator.IndexOf('SWP_SHOWWINDOW', $hideCurrent)
$thawHost = $configurator.IndexOf('SendMessageW(host.window(), WM_SETREDRAW, TRUE')
if ($freezeHost -lt 0 -or $hideCurrent -lt $freezeHost -or
    $showReplacement -lt $hideCurrent -or $thawHost -lt $showReplacement) {
  throw 'Settings pages must swap in a frozen host using hide-then-show order.'
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
  if ($text -notmatch 'settings_navigation::RequestApply\(m_hWnd\)') {
    throw "$source bypasses the shared apply command."
  }
}

$inputPage = Get-Content -LiteralPath (
  Join-Path $root 'WeaselDeployer/SwitcherSettingsDialog.cpp') -Raw
if ($inputPage -match 'IDC_SCHEMA_DETAIL_VERSION,[\s\S]{0,120}?kInstalledVersion' -or
    $inputPage -notmatch 'wanxiang\s*\?\s*WanxiangUpdateManager::LoadInstalledSchemeVersion\(\)') {
  throw 'Scheme details must display the installed Lite version, not the bundled baseline.'
}
if ($inputPage -notmatch
    'operation->success[\s\S]{0,420}?ShowDetails\(selected_schema_\)') {
  throw 'Scheme details are not refreshed after a successful package update.'
}

$configuratorPath = Join-Path $root 'WeaselDeployer/Configurator.cpp'
$configurator = Get-Content -LiteralPath $configuratorPath -Raw
if ($navigation -notmatch 'HasAnyUnappliedChanges\(\)' -or
    $navigation -notmatch 'kHostStateChangedMessage' -or
    $configurator -notmatch 'kHostApplyMessage' -or
    $configurator -notmatch 'refresh_shared_state' -or
    $configurator -notmatch 'Page::Appearance,[\s\S]{0,180}?Page::Fonts,[\s\S]{0,180}?Page::StatusIcons,[\s\S]{0,180}?Page::Input') {
  throw 'Apply must remain enabled across pages and commit every dirty page.'
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
  'MoveControl\(\s*dialog,\s*IDC_APPEARANCE_THEME_CARD,[\s\S]{0,520}?' +
    'kSingleRowCardHeightDlu\);'
  'constexpr int kSettingsColumnWidthDlu = 302;'
  'constexpr int kPreviewColumnWidthDlu = 198;'
  'MoveControl\(dialog, IDC_COLOR_FAMILY, 182, 174, 124,'
  'MoveControl\(dialog, IDC_COLOR_LIGHT, 182, 174, 124,'
  'MoveControl\(dialog, IDC_COLOR_DARK, 182, 205, 124,'
  'MoveControl\(dialog, IDC_PREVIEW_LIGHT, kPreviewColumnLeftDlu,'
  'MoveControl\(dialog, IDC_PREVIEW_DARK, kPreviewColumnLeftDlu,'
  'L"恢复本页默认"'
)
foreach ($pattern in $appearanceContracts) {
  if ($appearance -notmatch $pattern) {
    throw "Candidate-window layout contract changed or missing: $pattern"
  }
}

if ($navigation -notmatch
    'SwitchProc[\s\S]{0,3200}?SmoothingModeAntiAlias' -or
    $navigation -notmatch
    'AddControlPath\(track_path, track_shape, track_shape\.Height / 2\.0f\)' -or
    $navigation -notmatch
    'StyleSegmentedToggle\([\s\S]{0,180}?ToggleState::Segment') {
  throw 'Candidate-window switches no longer use the shared anti-aliased ' +
    'capsule and integrated segmented-control rules.'
}

if ($navigation -match
    'ComboListProc[\s\S]{0,500}?WM_WINDOWPOSCHANGED[\s\S]{0,220}?Round\(') {
  throw 'Combo popup rounding is applied after display and can visibly jump.'
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
if ($preview -notmatch 'std::array<std::wstring, 5> candidates' -or
    $preview -notmatch 'L"1\.", L"2\.", L"3\.", L"4\.",' -or
    $preview -notmatch 'L"5\."' -or
    $preview -notmatch 'canvas\.SetClip\(&scene_path\)') {
  throw 'Candidate-window previews must render five candidates.'
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

$statusPath = Join-Path $root 'WeaselDeployer/StatusIconSettingsDialog.cpp'
$status = Get-Content -LiteralPath $statusPath -Raw
$trayPath = Join-Path $root 'WeaselServer/WeaselTrayIcon.cpp'
$tray = Get-Content -LiteralPath $trayPath -Raw
$serverResources = Get-Content -LiteralPath (Join-Path $root 'WeaselServer/resource.h') -Raw
$serverRc = Get-Content -LiteralPath (Join-Path $root 'WeaselServer/WeaselServer.rc') -Raw
if ($userSettings -notmatch 'kStatusIconCapsSetting' -or
    $userSettings -notmatch 'std::wstring caps;' -or
    $userSettings -notmatch 'SchemaStatusIconSettings' -or
    $userSettings -notmatch 'kStatusIconUseGlobalMarker' -or
    $status -notmatch 'PreviewMode::Western' -or
    $status -notmatch 'PreviewMode::Caps' -or
    $status -notmatch 'EditScope::Schema' -or
    $status -notmatch 'IDC_STATUS_SCHEMA_COMBO' -or
    $status -notmatch 'StatusIconUsesGlobal' -or
    $tray -notmatch 'mode == ASCII_CAPS \|\| mode == ZHUNG_CAPS' -or
    $tray -notmatch 'LoadResolvedStatusIcon\(schema_icons\.caps' -or
    $tray -notmatch 'SchemaStatusIconSettings::Load\(state\.schema_id\)' -or
    $tray -notmatch 'icons\.caps, IDI_CAPS' -or
    $serverResources -notmatch 'IDI_CAPS' -or
    $serverRc -notmatch 'resource\\\\caps\.ico' -or
    -not (Test-Path -LiteralPath (Join-Path $root 'resource/caps.ico'))) {
  throw 'Status icons must support the three-state global and scheme override model.'
}

$bundledStatusIcons = @(
  'ascii-black.ico',
  'ascii-red.ico',
  'caps-blue.ico',
  'caps-red.ico',
  'chinese-black.ico',
  'chinese-blue.ico'
)
foreach ($icon in $bundledStatusIcons) {
  if (-not (Test-Path -LiteralPath (
        Join-Path $root "resource/status-icons/$icon"))) {
    throw "Missing bundled status icon: $icon"
  }
}
$installer = Get-Content -LiteralPath (Join-Path $root 'output/install.nsi') -Raw
if ($status -notmatch 'BundledStatusIconDirectory' -or
    $status -notmatch 'IsBundledStatusIcon' -or
    $installer -notmatch 'resource\\status-icons\\\*\.ico') {
  throw 'Bundled status icon alternatives must be selectable and installed read-only.'
}

if ($status -notmatch 'set_visible_without_redraw' -or
    $status -notmatch 'SWP_NOREDRAW' -or
    $status -notmatch 'RedrawWindow\(card, nullptr, nullptr,[\s\S]{0,100}?' +
      'RDW_INVALIDATE \| RDW_NOERASE \| RDW_UPDATENOW\)' -or
    $status -match 'RedrawWindow\(m_hWnd, &bounds, nullptr,[\s\S]{0,120}?' +
      'RDW_ALLCHILDREN') {
  throw 'Status-icon scope switching must repaint the card without staging child windows.'
}

$configurator = Get-Content -LiteralPath (
  Join-Path $root 'WeaselDeployer/Configurator.cpp') -Raw
if ($configurator -notmatch
    'message\.message == settings_navigation::kHostApplyMessage[\s\S]{0,420}?' +
      'std::any_of\(pages\.begin\(\), pages\.end\(\)' -or
    $configurator -notmatch
    'apply_order = \{settings_navigation::Page::Appearance,[\s\S]{0,180}?' +
      'settings_navigation::Page::Fonts,[\s\S]{0,180}?' +
      'settings_navigation::Page::StatusIcons,[\s\S]{0,180}?' +
      'settings_navigation::Page::Input\}') {
  throw 'The shared Apply command must accept every loaded settings page and ' +
    'apply all persistent pages in the safe order.'
}

if ($appearance -notmatch
    'OnThemeMode\([\s\S]{0,720}?SetThemeMode[\s\S]{0,360}?RefreshPreview\(\)') {
  throw 'Changing the candidate-window theme mode must mark the page pending.'
}

$fontPath = Join-Path $root 'WeaselDeployer/FontSettingsDialog.cpp'
$font = Get-Content -LiteralPath $fontPath -Raw
if ($font -notmatch
    'draft_\.Save\(\)[\s\S]{0,100}?FontSettings::Load\(\) != draft_') {
  throw 'Font settings must be read back before Apply reports success.'
}
if ($status -notmatch
    'saved\.Save\(\)[\s\S]{0,320}?StatusIconSettings::Load\(\) != saved' -or
    $status -notmatch
    'saved_schema\.Save\(schema\.id\)[\s\S]{0,420}?' +
      'SchemaStatusIconSettings::Load\(schema\.id\) != saved_schema') {
  throw 'Status icon settings must be read back before Apply reports success.'
}

$traySdk = Get-Content -LiteralPath (
  Join-Path $root 'WeaselServer/SystemTraySDK.cpp') -Raw
$serverApp = Get-Content -LiteralPath (
  Join-Path $root 'WeaselServer/WeaselServerApp.cpp') -Raw
$panel = Get-Content -LiteralPath (
  Join-Path $root 'WeaselUI/WeaselPanel.cpp') -Raw
$directWrite = Get-Content -LiteralPath (
  Join-Path $root 'WeaselUI/DirectWriteResources.cpp') -Raw
$runtime = Get-Content -LiteralPath (
  Join-Path $root 'RimeWithWeasel/RimeWithWeasel.cpp') -Raw
$ipcServer = Get-Content -LiteralPath (
  Join-Path $root 'WeaselIPCServer/WeaselServerImpl.cpp') -Raw
$ipcClient = Get-Content -LiteralPath (
  Join-Path $root 'WeaselIPC/WeaselClientImpl.cpp') -Raw
$keySink = Get-Content -LiteralPath (
  Join-Path $root 'WeaselTSF/KeyEventSink.cpp') -Raw
$languageBar = Get-Content -LiteralPath (
  Join-Path $root 'WeaselTSF/LanguageBar.cpp') -Raw
$tsfResource = Get-Content -LiteralPath (
  Join-Path $root 'WeaselTSF/WeaselTSF.rc') -Raw
$uiHost = Get-Content -LiteralPath (
  Join-Path $root 'WeaselUI/WeaselUI.cpp') -Raw
$deployer = Get-Content -LiteralPath (
  Join-Path $root 'WeaselDeployer/WeaselDeployer.cpp') -Raw
$serverResource = Get-Content -LiteralPath (
  Join-Path $root 'WeaselServer/WeaselServer.rc') -Raw
$switcher = Get-Content -LiteralPath (
  Join-Path $root 'WeaselDeployer/SwitcherSettingsDialog.cpp') -Raw
$updateManager = Get-Content -LiteralPath (
  Join-Path $root 'WeaselDeployer/WanxiangUpdateManager.cpp') -Raw
if ($traySdk -notmatch 'm_hCurrentIcon = icon \? ::CopyIcon\(icon\)' -or
    $traySdk -notmatch 'HICON replacement = ::CopyIcon\(hIcon\)' -or
    $traySdk -notmatch 'Shell_NotifyIcon\(NIM_DELETE, &m_tnd\)[\s\S]{0,160}?AddIcon\(\)' -or
    $tray -notmatch 'SetIcon\(mode_icon\[mode\]\)[\s\S]{0,80}?ShowIcon\(\)' -or
    $tray -match 'LoadResolvedStatusIcon\([^\)]*native_schema' -or
    $status -match 'LoadIconFile\(schema->native\[state\]\)' -or
    $serverApp -notmatch 'm_ui\.ReloadUserSettings\(\)[\s\S]{0,100}?tray_icon\.ReloadSettings\(\)' -or
    $panel -notmatch 'styleChanged = m_ostyle != m_style' -or
    $panel -notmatch 'acrylicModeChanged \|\| styleChanged' -or
    $directWrite -notmatch 'CreateFontFromLOGFONT' -or
    $directWrite -notmatch 'SetFontFamilyName\(resolved\.family' -or
    $directWrite -notmatch 'pPreeditTextFormat\.Get\(\)[\s\S]{0,80}?FontRole::Preedit' -or
    $directWrite -notmatch 'pLabelTextFormat\.Get\(\)[\s\S]{0,80}?FontRole::Label' -or
    $directWrite -notmatch 'pCommentTextFormat\.Get\(\)[\s\S]{0,80}?FontRole::Comment' -or
    $directWrite -notmatch 'return FontRole::Candidate' -or
    $ipcServer -notmatch 'WEASEL_IPC_UPDATE_CAPS_LOCK, OnCapsLockState' -or
    $ipcClient -notmatch 'WEASEL_IPC_UPDATE_CAPS_LOCK, enabled \? 1 : 0' -or
    $keySink -notmatch 'GetKeyboardState\(_lpbKeyState\)[\s\S]{0,520}?' +
      'UpdateCapsLockState\(caps_lock\)' -or
    $keySink -notmatch '_UpdateCapsLockState\(caps_lock\)' -or
    $languageBar -notmatch 'if \(caps_lock\)[\s\S]{0,100}?' +
      'LoadResolvedStatusIcon\(schema\.caps, global\.caps, IDI_CAPS\)' -or
    $languageBar -notmatch 'SchemaStatusIconSettings::Load\(_schema_id\)' -or
    $tsfResource -notmatch 'IDI_CAPS\s+ICON\s+"\.\.\\\\resource\\\\caps\.ico"' -or
    $tsfResource -match 'ID_WEASELTRAY_EXTENDED_SETTINGS|ID_WEASELTRAY_ACRYLIC' -or
    $serverApp -notmatch 'SetCapsLockStateCallback' -or
    $uiHost -notmatch 'panel\.Create\([\s\S]{0,450}?' +
      'panel\.ReloadUserSettings\(\)' -or
    $deployer -notmatch 'SetProcessDpiAwareness\(PROCESS_PER_MONITOR_DPI_AWARE\)' -or
    $serverApp -notmatch 'ID_WEASELTRAY_SETTINGS,[\s\S]{0,160}?L"/input"' -or
    $deployer -notmatch 'L"/input"[\s\S]{0,80}?configurator\.Run\(false\)' -or
    $serverResource -match 'ID_WEASELTRAY_EXTENDED_SETTINGS|ID_WEASELTRAY_ACRYLIC' -or
    $runtime -notmatch 'm_ui->Refresh\(\)[\s\S]{0,20}?\n\}') {
  throw 'Runtime settings must resolve selected font faces, track Caps Lock, ' +
    'recover missing tray icons, and repaint font/theme changes.'
}

if ($switcher -notmatch 'SWP_NOCOPYBITS \| SWP_NOREDRAW' -or
    $switcher -notmatch 'SetWindowRgn\(control, nullptr, FALSE\)' -or
    $switcher -notmatch 'GetWindowLongPtrW\(control, GWL_STYLE\) & WS_VISIBLE' -or
    $switcher -match 'IsWindowVisible\(control\)' -or
    $switcher -notmatch 'ModelUiNeedsRefresh\(progress\)' -or
    $switcher -notmatch 'progress\.state == State::Downloading &&[\s\S]{0,100}?' +
      'progress\.transferred != model_ui_transferred_' -or
    $switcher -notmatch 'RedrawWindow\(control, nullptr, nullptr,[\s\S]{0,100}?' +
      'RDW_INVALIDATE \| RDW_ERASE \| RDW_FRAME \| RDW_UPDATENOW' -or
    $switcher -notmatch 'RDW_INVALIDATE \| RDW_ERASE \| RDW_FRAME \| RDW_ALLCHILDREN' -or
    $switcher -notmatch 'scheme_update_available_ \+ model_update_available_' -or
    $switcher -notmatch 'open_update_list_after_check_ = true' -or
    $switcher -notmatch 'result\.scheme_update_available = scheme_update_available_' -or
    $switcher -notmatch 'StartSchemeUpdate\(latest_scheme_release_\)' -or
    $switcher -notmatch 'QueryGithubSchemeRelease' -or
    $updateManager -notmatch 'kCnbReleasesPath' -or
    $updateManager -notmatch 'ParseCnbSchemeReleases\(response, release\)' -or
    $updateManager -notmatch 'ParseGithubSchemeReleases\(response, release\)' -or
    $updateManager -notmatch 'result\.scheme_update_available \|\|' -or
    $updateManager -notmatch 'SaveLastRelease\(result\.latest_tag\)[\s\S]{0,220}?' +
      'QueryLatestModel') {
  throw 'Wanxiang update checks must keep scheme and model results independent, ' +
    'refresh cached results before use, leave stable model UI idle, preserve ' +
    'child visibility while the parent is hidden, and repaint moved rounded ' +
    'buttons including their labels.'
}

Write-Output ('Settings layout contract verified for: ' +
  (($registeredPages | Sort-Object) -join ', '))
