[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $root 'build/appearance-cache-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  throw 'Run from a Visual Studio Developer PowerShell or Command Prompt.'
}
$exe = Join-Path $build 'cache-tests.exe'
$obj = Join-Path $build 'cache-tests.obj'
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE `
  "/I$(Join-Path $root 'include')" "/Fe:$exe" "/Fo:$obj" `
  (Join-Path $PSScriptRoot 'AppearancePreviewCacheTests.cpp')
if ($LASTEXITCODE -ne 0) { throw 'Preview cache tests failed to compile.' }
& $exe (Join-Path $build 'profile')
if ($LASTEXITCODE -ne 0) { throw 'Preview cache tests failed.' }
