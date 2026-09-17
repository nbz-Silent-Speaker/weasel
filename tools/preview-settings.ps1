[CmdletBinding()]
param(
  [ValidateSet("Input", "Candidate", "Fonts", "StatusIcons")]
  [string]$Page = "Candidate",
  [switch]$SkipBuild,
  [switch]$NoLaunch,
  [switch]$Wait
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-PathUnderRoot {
  param([string]$Path, [string]$Root)
  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Preview path is outside the repository: $fullPath"
  }
}

function Get-VisualStudioPath {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found."
  }
  $path = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
  if (-not $path) {
    throw "Visual Studio C++ build tools were not found."
  }
  return $path.Trim()
}

function Enter-WeaselBuildEnvironment {
  param([string]$VisualStudioPath)
  $module = Join-Path $VisualStudioPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  if (-not (Test-Path -LiteralPath $module)) {
    throw "Visual Studio developer shell was not found: $module"
  }
  Import-Module $module
  Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation `
    -DevCmdArguments "-arch=x64 -host_arch=x64 -vcvars_ver=14.44" | Out-Null
}

function Copy-PreviewUserFiles {
  param([string]$Source, [string]$Destination)
  if (-not (Test-Path -LiteralPath $Source)) {
    return
  }
  $extensions = @(".yaml", ".yml", ".txt", ".png", ".ico")
  Get-ChildItem -LiteralPath $Source -File | Where-Object {
    $extensions -contains $_.Extension.ToLowerInvariant()
  } | Copy-Item -Destination $Destination -Force

  $sourceBuild = Join-Path $Source "build"
  if (Test-Path -LiteralPath $sourceBuild) {
    $destinationBuild = Join-Path $Destination "build"
    New-Item -ItemType Directory -Path $destinationBuild -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceBuild -File -Filter "*.yaml" |
      Copy-Item -Destination $destinationBuild -Force
  }
}

function Copy-RegistryTree {
  param(
    [Microsoft.Win32.RegistryKey]$Source,
    [Microsoft.Win32.RegistryKey]$Destination
  )
  foreach ($name in $Source.GetValueNames()) {
    $Destination.SetValue(
      $name,
      $Source.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames),
      $Source.GetValueKind($name))
  }
  foreach ($subKeyName in $Source.GetSubKeyNames()) {
    $sourceChild = $Source.OpenSubKey($subKeyName, $false)
    $destinationChild = $Destination.CreateSubKey($subKeyName, $true)
    try {
      Copy-RegistryTree -Source $sourceChild -Destination $destinationChild
    } finally {
      $destinationChild.Dispose()
      $sourceChild.Dispose()
    }
  }
}

function Copy-PreviewRegistrySettings {
  $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::CurrentUser,
    [Microsoft.Win32.RegistryView]::Registry64)
  try {
    $source = $base.OpenSubKey("Software\Rime\Weasel\UserSettings", $false)
    try {
      try {
        $base.DeleteSubKeyTree("Software\Rime\Weasel\PreviewUserSettings", $false)
      } catch [System.ArgumentException] {
      }
      $destination = $base.CreateSubKey("Software\Rime\Weasel\PreviewUserSettings", $true)
      try {
        if ($source) {
          Copy-RegistryTree -Source $source -Destination $destination
        }
      } finally {
        $destination.Dispose()
      }
    } finally {
      if ($source) {
        $source.Dispose()
      }
    }
  } finally {
    $base.Dispose()
  }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gitCommonDir = (& git -C $repoRoot rev-parse --git-common-dir).Trim()
if (-not [System.IO.Path]::IsPathRooted($gitCommonDir)) {
  $gitCommonDir = Join-Path $repoRoot $gitCommonDir
}
$sharedRepoRoot = Split-Path ([System.IO.Path]::GetFullPath($gitCommonDir)) -Parent
$boostRoot = Join-Path $sharedRepoRoot "deps\boost_1_84_0"
$previewRoot = Join-Path $repoRoot "build\settings-preview"
$previewApp = Join-Path $previewRoot "app"
$previewProfile = Join-Path $previewRoot "profile"
Assert-PathUnderRoot -Path $previewRoot -Root $repoRoot

if (-not $SkipBuild) {
  if (-not (Test-Path -LiteralPath (Join-Path $boostRoot "stage\lib"))) {
    throw "Boost 1.84 build was not found: $boostRoot"
  }
  if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "include\rime_api.h")) -or
      -not (Test-Path -LiteralPath (Join-Path $repoRoot "lib64\rime.lib"))) {
    throw "Rime development files are missing from the worktree."
  }

  Enter-WeaselBuildEnvironment -VisualStudioPath (Get-VisualStudioPath)
  $env:BOOST_ROOT = $boostRoot
  $env:VERSION_MAJOR = "0"
  $env:VERSION_MINOR = "17"
  $env:VERSION_PATCH = "4"
  $env:FILE_VERSION = "0.17.4.0"
  $env:PRODUCT_VERSION = "0.17.4.0.local"
  $manifestTarget = Join-Path $repoRoot "PerMonitorHighDPIAware.manifest"
  $removeManifestAfterBuild = -not (Test-Path -LiteralPath $manifestTarget)
  if ($removeManifestAfterBuild) {
    $manifestSource = Join-Path $env:VCToolsInstallDir "include\Manifest\PerMonitorHighDPIAware.manifest"
    if (-not (Test-Path -LiteralPath $manifestSource)) {
      throw "The Visual Studio per-monitor DPI manifest was not found: $manifestSource"
    }
    Copy-Item -LiteralPath $manifestSource -Destination $manifestTarget
  }
  Push-Location $repoRoot
  try {
    & xmake f -P . -p windows -a x64 -m release --vs_sdkver=10.0.26100.0
    if ($LASTEXITCODE -ne 0) {
      throw "Xmake configuration failed."
    }
    & xmake build -P . WeaselDeployer
    if ($LASTEXITCODE -ne 0) {
      throw "WeaselDeployer build failed."
    }
  } finally {
    Pop-Location
    if ($removeManifestAfterBuild -and (Test-Path -LiteralPath $manifestTarget)) {
      Remove-Item -LiteralPath $manifestTarget -Force
    }
  }
}

$builtExe = Join-Path $repoRoot "output\WeaselDeployer.exe"
$builtRime = Join-Path $repoRoot "output\rime.dll"
if (-not (Test-Path -LiteralPath $builtExe) -or -not (Test-Path -LiteralPath $builtRime)) {
  throw "The local preview executable is incomplete. Run without -SkipBuild first."
}

$weaselKey = Get-ItemProperty -LiteralPath "HKLM:\Software\WOW6432Node\Rime\Weasel" -ErrorAction Stop
$installedRoot = $weaselKey.WeaselRoot
$sharedData = Join-Path $installedRoot "data"
if (-not (Test-Path -LiteralPath (Join-Path $sharedData "weasel.yaml"))) {
  throw "Installed Weasel data was not found: $sharedData"
}

$userKey = Get-ItemProperty -LiteralPath "HKCU:\Software\Rime\Weasel" -ErrorAction SilentlyContinue
$configuredUserDir = if ($userKey -and $userKey.PSObject.Properties["RimeUserDir"]) {
  $userKey.RimeUserDir
} else {
  $null
}
if (-not $configuredUserDir) {
  $configuredUserDir = Join-Path $env:APPDATA "Rime"
}

New-Item -ItemType Directory -Path $previewApp -Force | Out-Null
if (Test-Path -LiteralPath $previewProfile) {
  Remove-Item -LiteralPath $previewProfile -Recurse -Force
}
New-Item -ItemType Directory -Path $previewProfile -Force | Out-Null
Copy-PreviewUserFiles -Source $configuredUserDir -Destination $previewProfile
Copy-Item -LiteralPath $builtExe -Destination $previewApp -Force
Copy-Item -LiteralPath $builtRime -Destination $previewApp -Force
foreach ($icon in @("zh.ico", "en.ico", "caps.ico")) {
  Copy-Item -LiteralPath (Join-Path $repoRoot "resource\$icon") `
    -Destination (Join-Path $previewApp $icon) -Force
}
$previewStatusIcons = Join-Path $previewApp "icons\status"
New-Item -ItemType Directory -Path $previewStatusIcons -Force | Out-Null
Copy-Item -Path (Join-Path $repoRoot "resource\status-icons\*.ico") `
  -Destination $previewStatusIcons -Force
Copy-PreviewRegistrySettings

$env:WEASEL_SETTINGS_PREVIEW = "1"
$env:WEASEL_PREVIEW_USER_DIR = $previewProfile
$env:WEASEL_PREVIEW_DISPLAY_USER_DIR = $configuredUserDir
$env:WEASEL_PREVIEW_SHARED_DIR = $sharedData

$arguments = @(switch ($Page) {
  "Input" { @() }
  "Candidate" { @("/settings") }
  "Fonts" { @("/fonts") }
  "StatusIcons" { @("/status-icons") }
})

$previewExe = Join-Path $previewApp "WeaselDeployer.exe"
if (-not $NoLaunch) {
  if ($arguments.Count -eq 0) {
    Start-Process -FilePath $previewExe -WorkingDirectory $previewApp -Wait:$Wait
  } else {
    Start-Process -FilePath $previewExe -ArgumentList $arguments -WorkingDirectory $previewApp -Wait:$Wait
  }
  Write-Host "Local settings preview opened. Real user files and the running input method are isolated."
}
