$ErrorActionPreference = 'Stop'
$source = Split-Path $PSScriptRoot -Parent
$build = Join-Path $env:TEMP ('weasel-model-tests-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $build | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-TestFile($name, $content) {
    [IO.File]::WriteAllText((Join-Path $build $name), $content, $utf8)
}

# Compile the actual manager implementation. Replace only the download artifact
# constants with a deterministic 4 KiB fixture; no transaction logic is copied.
$fixture = [byte[]]::new(4096)
for ($i = 0; $i -lt $fixture.Length; ++$i) { $fixture[$i] = 90 }
$sha = [Security.Cryptography.SHA256]::Create()
$digest = ([BitConverter]::ToString($sha.ComputeHash($fixture))).Replace('-', '').ToLowerInvariant()
$sha.Dispose()
$cpp = [IO.File]::ReadAllText((Join-Path $source 'WanxiangModelManager.cpp'))
$cpp = $cpp.Replace('9f80530f470033cfb6d4b44bb861b540f64100426f92dd0f87140883632a3d93', $digest)
$cpp = $cpp.Replace('Weasel Wanxiang LTS Model', ('Weasel model tests ' + [guid]::NewGuid()))
Write-TestFile 'WanxiangModelManager.cpp' $cpp
$header = [IO.File]::ReadAllText((Join-Path $source 'WanxiangModelManager.h'))
Write-TestFile 'WanxiangModelManager.h' ($header.Replace('420343852', '4096').Replace(' private:', ' public:'))
Write-TestFile 'stdafx.h' @'
#pragma once
#include <windows.h>
#include <iostream>
#define LOG(level) std::cerr
'@
Write-TestFile 'WeaselIPC.h' @'
#pragma once
namespace weasel {
class Client {
 public:
  bool Connect() { return false; }
  void StartMaintenance() {}
  void EndMaintenance() {}
};
}
'@
Write-TestFile 'WeaselUtility.h' @'
#pragma once
#include <filesystem>
#include <string>
extern std::filesystem::path test_user_directory;
inline std::filesystem::path WeaselUserDataPath() { return test_user_directory; }
inline std::wstring u8tow(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}
'@
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ModelTransactionTests.cpp') -Destination $build
Push-Location $build
try {
    & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I. WanxiangModelManager.cpp ModelTransactionTests.cpp /Fe:ModelTransactionTests.exe /link ole32.lib shell32.lib bcrypt.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Model transaction test build failed' }
    & .\ModelTransactionTests.exe (Join-Path $build 'fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Model transaction tests failed' }
} finally {
    Pop-Location
}
