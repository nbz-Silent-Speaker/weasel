$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$resolver = Join-Path $repoRoot 'tools\resolve-weasel-version.bat'
$base = '9cc96e20dc71b80876b12f689bb5863c76c2a7ed'

function Invoke-Resolver([string]$directory) {
  Push-Location $directory
  try {
    $result = & cmd.exe /d /c "call `"$resolver`" && set WEASEL_BUILD && set PRODUCT_VERSION && set FILE_VERSION"
    if ($LASTEXITCODE -ne 0) { throw "Version resolver failed in $directory" }
    $values = @{}
    foreach ($line in $result) {
      if ($line -match '^(WEASEL_BUILD|PRODUCT_VERSION|FILE_VERSION)=(.*)$') {
        $values[$Matches[1]] = $Matches[2]
      }
    }
    if ($values.Count -ne 3) { throw "Incomplete version fields: $($result -join ', ')" }
    return $values
  }
  finally { Pop-Location }
}

function Invoke-Git([string]$directory, [string[]]$gitArgs) {
  $result = & git -C $directory @gitArgs
  if ($LASTEXITCODE -ne 0) { throw "Git failed: $($gitArgs -join ' ')" }
  return $result
}

$savedBase = $env:WEASEL_VERSION_BASE_REF
$savedBuild = $env:WEASEL_BUILD
$savedVersion = $env:WEASEL_VERSION
$savedFileVersion = $env:FILE_VERSION
$savedRelease = $env:RELEASE_BUILD
$testDir = Join-Path ([IO.Path]::GetTempPath()) ("weasel-version-test-" + [guid]::NewGuid().ToString('N'))

try {
  $env:WEASEL_VERSION_BASE_REF = $null
  $env:WEASEL_BUILD = $null
  $env:WEASEL_VERSION = $null
  $env:FILE_VERSION = $null
  $env:RELEASE_BUILD = $null

  $expected = [int](Invoke-Git $repoRoot @('rev-list', "$base..HEAD", '--count'))
  $actual = Invoke-Resolver $repoRoot
  if ([int]$actual.WEASEL_BUILD -ne $expected -or $actual.FILE_VERSION -ne "0.17.4.$expected" -or
      $actual.PRODUCT_VERSION -notmatch "^0\.17\.4\.$expected\.[0-9a-f]+$") {
    throw "Current checkout version does not count from the original 0.17.4 baseline."
  }

  New-Item -ItemType Directory -Path $testDir | Out-Null
  Invoke-Git $testDir @('init', '-q') | Out-Null
  Invoke-Git $testDir @('config', 'user.name', 'Version Test') | Out-Null
  Invoke-Git $testDir @('config', 'user.email', 'version-test@example.invalid') | Out-Null
  Invoke-Git $testDir @('commit', '--allow-empty', '-qm', 'base') | Out-Null
  $env:WEASEL_VERSION_BASE_REF = Invoke-Git $testDir @('rev-parse', 'HEAD')
  Invoke-Git $testDir @('tag', '0.17.4') | Out-Null
  Invoke-Git $testDir @('commit', '--allow-empty', '-qm', 'first') | Out-Null
  Invoke-Git $testDir @('tag', 'acrylic-0.17.4.184') | Out-Null
  Invoke-Git $testDir @('commit', '--allow-empty', '-qm', 'second') | Out-Null
  Invoke-Git $testDir @('tag', 'acrylic-0.17.4-ci148') | Out-Null
  Invoke-Git $testDir @('commit', '--allow-empty', '-qm', 'third') | Out-Null

  $tagged = Invoke-Resolver $testDir
  if ($tagged.WEASEL_BUILD -ne '3' -or $tagged.FILE_VERSION -ne '0.17.4.3' -or
      $tagged.PRODUCT_VERSION -notmatch '^0\.17\.4\.3\.[0-9a-f]+$') {
    throw 'Later acrylic/CI release tags reset the version counter.'
  }

  Write-Host "Version numbering passed: repository build $expected; acrylic/CI tag fixture build 3."
}
finally {
  $env:WEASEL_VERSION_BASE_REF = $savedBase
  $env:WEASEL_BUILD = $savedBuild
  $env:WEASEL_VERSION = $savedVersion
  $env:FILE_VERSION = $savedFileVersion
  $env:RELEASE_BUILD = $savedRelease
  $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  $resolvedTestDir = [IO.Path]::GetFullPath($testDir)
  if ($resolvedTestDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
      (Split-Path $resolvedTestDir -Leaf) -like 'weasel-version-test-*' -and
      (Test-Path -LiteralPath $resolvedTestDir)) {
    Remove-Item -LiteralPath $resolvedTestDir -Recurse -Force
  }
}
