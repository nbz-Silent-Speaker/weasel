[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $root 'build/settings-appearance-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  throw 'Run from a Visual Studio Developer PowerShell.'
}
foreach ($test in @('SettingsColorTests', 'SettingsAppearanceTests')) {
  $exe = Join-Path $build "$test.exe"
  $obj = Join-Path $build "$test.obj"
  & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE `
    "/I$(Join-Path $root 'include')" "/Fe:$exe" "/Fo:$obj" `
    (Join-Path $PSScriptRoot "$test.cpp")
  if ($LASTEXITCODE -ne 0) { throw "$test failed to compile." }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "$test failed." }
}
