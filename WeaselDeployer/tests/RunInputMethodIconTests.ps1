$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $root 'build/input-method-icon-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe = Join-Path $build 'icon-tests.exe'
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE `
  "/I$(Join-Path $root 'include')" "/Fe:$exe" "/Fo:$build/icon-tests.obj" `
  (Join-Path $PSScriptRoot 'InputMethodIconTests.cpp') /link Ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Input method icon tests failed to compile.' }
& $exe (Join-Path $root 'resource/input-method.ico') (Join-Path $build 'icon.dll')
if ($LASTEXITCODE -ne 0) { throw 'Input method icon tests failed.' }
