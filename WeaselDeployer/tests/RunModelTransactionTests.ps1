param([switch]$NetworkProbe)

$ErrorActionPreference = 'Stop'
$source = Split-Path $PSScriptRoot -Parent
$build = Join-Path $env:TEMP ('weasel-model-tests-' + [guid]::NewGuid())
$registryRoot = 'Software\Rime\Weasel\Tests\PackageUpdates-' + [guid]::NewGuid()
$registryLiteral = $registryRoot.Replace('\', '\\')
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
$cpp = $cpp.Replace('Software\\Rime\\Weasel\\PackageUpdates', $registryLiteral)
$cpp = $cpp.Replace('Weasel Wanxiang LTS Grammar Model',
    ('Weasel grammar model tests ' + [guid]::NewGuid()))
Write-TestFile 'WanxiangModelManager.cpp' $cpp
$header = [IO.File]::ReadAllText((Join-Path $source 'WanxiangModelManager.h'))
$header = $header.Replace('420343852', '4096')
$header = $header.Replace('9f80530f470033cfb6d4b44bb861b540f64100426f92dd0f87140883632a3d93', $digest)
Write-TestFile 'WanxiangModelManager.h' ($header.Replace(' private:', ' public:'))
$updateHeader = [IO.File]::ReadAllText((Join-Path $source 'WanxiangUpdateManager.h'))
Write-TestFile 'WanxiangUpdateManager.h' ($updateHeader.Replace(' private:', ' public:'))
$updateCpp = [IO.File]::ReadAllText((Join-Path $source 'WanxiangUpdateManager.cpp'))
$updateCpp = $updateCpp.Replace('Software\\Rime\\Weasel\\PackageUpdates', $registryLiteral)
Write-TestFile 'WanxiangUpdateManager.cpp' $updateCpp
$schemeHeader = [IO.File]::ReadAllText((Join-Path $source 'WanxiangSchemeManager.h'))
Write-TestFile 'WanxiangSchemeManager.h' ($schemeHeader.Replace(' private:', ' public:'))
$schemeCpp = [IO.File]::ReadAllText((Join-Path $source 'WanxiangSchemeManager.cpp'))
$schemeCpp = $schemeCpp.Replace('Software\\Rime\\Weasel\\PackageUpdates', $registryLiteral)
Write-TestFile 'WanxiangSchemeManager.cpp' $schemeCpp
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UpdateManagerTests.cpp') -Destination $build
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SchemeTransactionTests.cpp') -Destination $build
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SchemeDownloadProbe.cpp') -Destination $build
Write-TestFile 'stdafx.h' @'
#pragma once
#include <windows.h>
#include <atlbase.h>
#include <iostream>
#define LOG(level) std::cerr
'@
Write-TestFile 'WeaselIPC.h' @'
#pragma once
namespace weasel {
inline int maintenance_start_count = 0;
inline int maintenance_end_count = 0;
class Client {
 public:
  bool Connect() { return true; }
  void StartMaintenance() { ++maintenance_start_count; }
  void EndMaintenance() { ++maintenance_end_count; }
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
inline std::string wtou8(const std::wstring& text) {
  return std::string(text.begin(), text.end());
}
'@
$modelTests = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ModelTransactionTests.cpp'))
$modelTests = "#include <WeaselIPC.h>`r`n" + $modelTests
$freshInstall = 'Require(manager.CompleteAndInstall(&error), "fresh model install failed");'
$freshInstallCheck = @'
const int maintenance_before_fresh_install = weasel::maintenance_start_count;
      Require(manager.CompleteAndInstall(&error), "fresh model install failed");
      Require(weasel::maintenance_start_count == maintenance_before_fresh_install,
              "fresh model install entered maintenance");
'@
if (-not $modelTests.Contains($freshInstall)) { throw 'Fresh install test insertion point was not found' }
$modelTests = $modelTests.Replace($freshInstall, $freshInstallCheck.Trim())
$replacement = 'Require(manager.CompleteAndInstall(&error), "replacement failed");'
$replacementCheck = @'
const int maintenance_before_replacement = weasel::maintenance_start_count;
      Require(manager.CompleteAndInstall(&error), "replacement failed");
      Require(weasel::maintenance_start_count == maintenance_before_replacement,
              "model replacement entered maintenance before Apply");
'@
if (-not $modelTests.Contains($replacement)) { throw 'Replacement test insertion point was not found' }
$modelTests = $modelTests.Replace($replacement, $replacementCheck.Trim())
Write-TestFile 'ModelTransactionTests.cpp' $modelTests
Push-Location $build
try {
    & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I. WanxiangModelManager.cpp ModelTransactionTests.cpp /Fe:ModelTransactionTests.exe /link ole32.lib shell32.lib bcrypt.lib advapi32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Model transaction test build failed' }
    # GitHub's workspace is on D: and LocalAppData is on C:, exercising the
    # same cross-volume copy/rename path used by a custom Rime user folder.
    $fixtureRoot = if ($env:GITHUB_WORKSPACE) {
        Join-Path $env:GITHUB_WORKSPACE ('model-test-fixtures-' + [guid]::NewGuid())
    } else { Join-Path $build 'fixtures' }
    $cacheRoot = if ($env:GITHUB_WORKSPACE) {
        Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ('weasel-model-test-cache-' + [guid]::NewGuid())
    } else { Join-Path $build 'cache' }
    & .\ModelTransactionTests.exe $fixtureRoot $cacheRoot
    if ($LASTEXITCODE -ne 0) { throw "Model transaction tests failed (exit $LASTEXITCODE)" }
    & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I. WanxiangSchemeManager.cpp SchemeTransactionTests.cpp /Fe:SchemeTransactionTests.exe /link advapi32.lib bcrypt.lib ole32.lib shell32.lib uuid.lib winhttp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Scheme transaction test build failed' }
    & .\SchemeTransactionTests.exe (Join-Path $fixtureRoot 'scheme')
    if ($LASTEXITCODE -ne 0) { throw "Scheme transaction tests failed (exit $LASTEXITCODE)" }
    if ($NetworkProbe) {
        & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I. WanxiangSchemeManager.cpp SchemeDownloadProbe.cpp /Fe:SchemeDownloadProbe.exe /link advapi32.lib bcrypt.lib ole32.lib shell32.lib uuid.lib winhttp.lib
        if ($LASTEXITCODE -ne 0) { throw 'Scheme download probe build failed' }
        $releases = Invoke-RestMethod -Uri 'https://cnb.cool/amzxyz/rime-wanxiang/-/releases' -Headers @{ Accept = 'application/json' }
        $formal = $releases | Where-Object { -not $_.draft -and -not $_.prerelease -and $_.tag_name -match '^v?\d+\.\d+\.\d+$' } | Sort-Object { [version]($_.tag_name.TrimStart('v', 'V')) } -Descending | Select-Object -First 1
        $asset = $formal.assets | Where-Object name -eq 'rime-wanxiang-lite.zip' | Select-Object -First 1
        if (-not $formal -or -not $asset -or $asset.hash_algo -ne 'sha256') { throw 'CNB scheme metadata was incomplete' }
        $downloadUrl = 'https://cnb.cool/amzxyz/rime-wanxiang/-/releases/download/' + $formal.tag_name + '/rime-wanxiang-lite.zip'
        & .\SchemeDownloadProbe.exe (Join-Path $fixtureRoot 'scheme-download') $formal.tag_name $downloadUrl $asset.hash_value ([string]$asset.size)
        if ($LASTEXITCODE -ne 0) { throw "Scheme download probe failed (exit $LASTEXITCODE)" }
    }
    & cl.exe /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I. WanxiangUpdateManager.cpp UpdateManagerTests.cpp /Fe:UpdateManagerTests.exe /link advapi32.lib winhttp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Update manager test build failed' }
    if ($NetworkProbe) {
        & .\UpdateManagerTests.exe --network
    } else {
        & .\UpdateManagerTests.exe
    }
    if ($LASTEXITCODE -ne 0) { throw "Update manager tests failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
    Remove-Item -LiteralPath ('Registry::HKEY_CURRENT_USER\' + $registryRoot) `
        -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $build -Recurse -Force -ErrorAction SilentlyContinue
}
