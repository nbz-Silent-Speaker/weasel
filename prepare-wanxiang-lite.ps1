param(
  [string]$ManifestPath = (Join-Path $PSScriptRoot 'data\packages\wanxiang-lite.json'),
  [string]$Destination = (Join-Path $PSScriptRoot 'output\data'),
  [string]$ArchivePath = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Get-NormalizedHash([string]$Path) {
  $stream = [IO.File]::Open(
    $Path,
    [IO.FileMode]::Open,
    [IO.FileAccess]::Read,
    [IO.FileShare]::Read
  )
  try {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
      $bytes = $sha256.ComputeHash($stream)
    } finally {
      $sha256.Dispose()
    }
  } finally {
    $stream.Dispose()
  }

  return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
}

function Assert-SafeRelativePath([string]$Path) {
  $normalized = $Path.Replace('\', '/')
  $segments = $normalized.Split('/')
  if ([string]::IsNullOrWhiteSpace($normalized) -or
      [IO.Path]::IsPathRooted($normalized) -or
      $normalized.Contains(':') -or
      $segments -contains '..' -or
      $segments -contains '.' -or
      $segments -contains '') {
    throw "Unsafe package path: $Path"
  }
}

function Enable-WanxiangLiteDefaultSchema([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Rime default config was not prepared before Wanxiang integration: $Path"
  }

  $text = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
  $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
  $lines = [regex]::Split($text, '\r?\n')
  $headers = @()
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^schema_list:\s*(?:#.*)?$') {
      $headers += $i
    }
  }
  if ($headers.Count -ne 1) {
    throw "Expected exactly one top-level schema_list in $Path"
  }

  $start = $headers[0]
  $end = $start + 1
  while ($end -lt $lines.Count -and $lines[$end] -notmatch '^[^\s#]') {
    $end++
  }
  $schemaIds = @()
  for ($i = $start + 1; $i -lt $end; $i++) {
    if ($lines[$i] -match '^\s{2}-\s+schema:\s*["'']?([^\s#"'']+)["'']?\s*(?:#.*)?$') {
      $schemaIds += $Matches[1]
    }
  }
  if ($schemaIds.Count -eq 0) {
    throw "Unsupported schema_list layout in $Path"
  }
  if (($schemaIds | Where-Object { $_ -eq 'wanxiang_lite' }).Count -gt 1) {
    throw "Duplicate wanxiang_lite entries in $Path"
  }
  if ($schemaIds -contains 'wanxiang_lite') {
    return
  }

  $updated = @($lines[0..$start]) +
    '  - schema: wanxiang_lite' +
    @($lines[($start + 1)..($lines.Count - 1)])
  [IO.File]::WriteAllText($Path, ($updated -join $newline),
                          [Text.UTF8Encoding]::new($false))
}

$manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
if ($manifest.format_version -ne 1 -or $manifest.package_id -ne 'wanxiang-lite') {
  throw "Unsupported Wanxiang package manifest: $ManifestPath"
}

foreach ($source in @($manifest.source, $manifest.model)) {
  $uri = [Uri]$source.url
  if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'cnb.cool') {
    throw "Wanxiang downloads must use the verified CNB HTTPS source: $($source.url)"
  }
  if ([int64]$source.size -le 0 -or
      [string]$source.sha256 -notmatch '^[0-9a-f]{64}$') {
    throw "Wanxiang source is missing a verified size or SHA256: $($source.url)"
  }
}

$forbiddenFiles = @(
  'default.yaml', 'weasel.yaml', 'installation.yaml', 'user.yaml'
)
foreach ($relativePath in $manifest.files) {
  $normalized = ([string]$relativePath).Replace('\', '/')
  $leaf = [IO.Path]::GetFileName($normalized)
  if ($forbiddenFiles -contains $leaf -or
      $leaf -like '*.custom.yaml' -or
      $normalized -match '(?i)\.userdb(?:/|$)') {
    throw "Protected Rime file must not be bundled from Wanxiang: $relativePath"
  }
}

if (-not $ArchivePath) {
  $cacheDirectory = Join-Path $PSScriptRoot 'output\package-cache'
  New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null
  $ArchivePath = Join-Path $cacheDirectory "wanxiang-lite-$($manifest.version).zip"
  $partialPath = "$ArchivePath.part"
  if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
    Invoke-WebRequest -UseBasicParsing -Uri $manifest.source.url -OutFile $partialPath
    Move-Item -LiteralPath $partialPath -Destination $ArchivePath
  }
}

$archive = Get-Item -LiteralPath $ArchivePath
if ($archive.Length -ne [int64]$manifest.source.size) {
  throw "Wanxiang archive size mismatch: expected $($manifest.source.size), got $($archive.Length)"
}
$archiveHash = Get-NormalizedHash $archive.FullName
if ($archiveHash -ne $manifest.source.sha256) {
  throw "Wanxiang archive SHA256 mismatch: expected $($manifest.source.sha256), got $archiveHash"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive.FullName)
try {
  $archiveEntries = @{}
  foreach ($entry in $zip.Entries) {
    if (-not $entry.Name) {
      continue
    }
    $entryName = $entry.FullName.Replace('\', '/')
    Assert-SafeRelativePath $entryName
    if ($archiveEntries.ContainsKey($entryName)) {
      throw "Duplicate package path: $entryName"
    }
    $archiveEntries[$entryName] = $entry
  }

  $expected = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
  foreach ($relativePath in $manifest.files) {
    Assert-SafeRelativePath $relativePath
    if (-not $expected.Add([string]$relativePath)) {
      throw "Duplicate manifest path: $relativePath"
    }
    if (-not $archiveEntries.ContainsKey([string]$relativePath)) {
      throw "Wanxiang archive is missing required file: $relativePath"
    }
  }

  $destinationRoot = [IO.Path]::GetFullPath($Destination)
  New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
  $contentFiles = @()
  foreach ($relativePath in $manifest.files) {
    $relativePath = [string]$relativePath
    $targetPath = [IO.Path]::GetFullPath(
      (Join-Path $destinationRoot $relativePath.Replace('/', '\')))
    $rootPrefix = $destinationRoot.TrimEnd('\') + '\'
    if (-not $targetPath.StartsWith($rootPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
      throw "Package path escapes destination: $relativePath"
    }
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($targetPath)) | Out-Null
    $inputStream = $archiveEntries[$relativePath].Open()
    try {
      $outputStream = [IO.File]::Open(
        $targetPath, [IO.FileMode]::Create, [IO.FileAccess]::Write,
        [IO.FileShare]::None)
      try {
        $inputStream.CopyTo($outputStream)
      } finally {
        $outputStream.Dispose()
      }
    } finally {
      $inputStream.Dispose()
    }

    $role = if ($relativePath.EndsWith('.schema.yaml')) {
      'schema'
    } elseif ($relativePath.EndsWith('.dict.yaml')) {
      'dictionary'
    } elseif ($relativePath.EndsWith('.lua')) {
      'lua'
    } else {
      'data'
    }
    $contentFiles += [ordered]@{
      relative_path = $relativePath
      size = (Get-Item -LiteralPath $targetPath).Length
      sha256 = Get-NormalizedHash $targetPath
      role = $role
    }
  }
} finally {
  $zip.Dispose()
}

$mainSchemaPath = Join-Path $destinationRoot 'wanxiang_lite.schema.yaml'
$mainSchema = [IO.File]::ReadAllText($mainSchemaPath, [Text.Encoding]::UTF8)
if ($mainSchema -notmatch '(?m)^\s*schema_id:\s*wanxiang_lite\s*(?:#.*)?$' -or
    $mainSchema -notmatch '(?m)^\s*-\s*wanxiang_algebra:/lite/全拼\s*(?:#.*)?$' -or
    $mainSchema -notmatch '(?m)^\s*language:\s*wanxiang-lts-zh-hans\s*(?:#.*)?$') {
  throw 'Wanxiang Lite no longer matches the verified full-pinyin and model integration contract.'
}

Enable-WanxiangLiteDefaultSchema (Join-Path $destinationRoot 'default.yaml')

$packageDirectory = Join-Path $destinationRoot 'packages'
$licenseDirectory = Join-Path $destinationRoot 'licenses'
New-Item -ItemType Directory -Force -Path $packageDirectory, $licenseDirectory | Out-Null
Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $packageDirectory 'wanxiang-lite.json') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'data\licenses\CC-BY-4.0.txt') -Destination $licenseDirectory -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'data\licenses\wanxiang-lite-NOTICE.txt') -Destination $licenseDirectory -Force

$contentManifest = [ordered]@{
  format_version = 1
  package_id = $manifest.package_id
  package_version = $manifest.version
  source_archive_sha256 = $archiveHash
  default_schema = 'wanxiang_lite'
  files = $contentFiles
}
$contentManifest | ConvertTo-Json -Depth 6 |
  Set-Content -LiteralPath (Join-Path $packageDirectory 'wanxiang-lite-content.json') -Encoding utf8

Write-Host "Prepared $($contentFiles.Count) Wanxiang Lite files from verified CNB archive."
