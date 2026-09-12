$ErrorActionPreference = 'Stop'
$taskSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Composition.cpp') -Raw
$taskAttributes = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\DisplayAttribute.cpp') -Raw
function Get-Section([string]$Text, [string]$Start, [string]$End) {
    $first = $Text.IndexOf($Start, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Missing source marker: $Start" }
    $last = $Text.IndexOf($End, $first + $Start.Length, [StringComparison]::Ordinal)
    if ($last -lt 0) { throw "Missing source marker: $End" }
    return $Text.Substring($first, $last - $first)
}
# Compile the actual production routines against a controlled host, rather
# than a second implementation of the lifecycle or display-attribute cleanup.
$taskParts = @(
    (Get-Section $taskSource '/* End Composition */' '/* Get Text Extent */'),
    (Get-Section $taskSource 'void WeaselTSF::_FinalizeComposition()' 'void WeaselTSF::_SetComposition('),
    (Get-Section $taskSource 'BOOL WeaselTSF::_IsComposing()' 'BOOL WeaselTSF::_IsCurrentComposition('),
    $taskSource.Substring($taskSource.IndexOf('BOOL WeaselTSF::_IsCurrentComposition(', [StringComparison]::Ordinal)),
    (Get-Section $taskAttributes 'void WeaselTSF::_ClearCompositionDisplayAttributes(' 'BOOL WeaselTSF::_SetCompositionDisplayAttributes(')
)
$taskDirectory = Join-Path $PSScriptRoot 'obj'
[void][IO.Directory]::CreateDirectory($taskDirectory)
[IO.File]::WriteAllText((Join-Path $taskDirectory 'composition-under-test.inc'), ($taskParts -join "`r`n"), (New-Object Text.UTF8Encoding($false)))
Write-Output 'Prepared production composition routines for regression tests'

$taskCandidate = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\CandidateList.cpp') -Raw
$taskCandidateParts = @(
    (Get-Section $taskCandidate 'void CCandidateList::Destroy()' 'UIStyle& CCandidateList::style()'),
    (Get-Section $taskCandidate 'void CCandidateList::StartUI()' 'com_ptr<ITfContext> CCandidateList::GetContextDocument()'),
    (Get-Section $taskSource 'STDMETHODIMP WeaselTSF::OnCompositionTerminated(' 'void WeaselTSF::_SetComposition('),
    (Get-Section $taskSource 'BOOL WeaselTSF::_IsComposing()' 'BOOL WeaselTSF::_IsCurrentComposition('),
    $taskSource.Substring($taskSource.IndexOf('BOOL WeaselTSF::_IsCurrentComposition(', [StringComparison]::Ordinal))
)
[IO.File]::WriteAllText((Join-Path $taskDirectory 'candidate-under-test.inc'), ($taskCandidateParts -join "`r`n"), (New-Object Text.UTF8Encoding($false)))
# Keep a negative control from the shipped CI83 source. Rename only its class
# qualifier so its unchanged StartUI/Destroy run alongside the corrected ones.
$taskBaseline = & git -C (Join-Path $PSScriptRoot '..\..') show '093117dbf670aad2081a5cb8b7aa62c0e5228017:WeaselTSF/CandidateList.cpp'
if ($LASTEXITCODE -ne 0) { throw 'Cannot load pinned CI83 lifecycle baseline' }
$taskBaseline = $taskBaseline -join "`n"
$taskLegacyParts = @(
    (Get-Section $taskBaseline 'void CCandidateList::Destroy()' 'void CCandidateList::DestroyAll()'),
    (Get-Section $taskBaseline 'void CCandidateList::StartUI()' 'void CCandidateList::EndUI()')
)
[IO.File]::WriteAllText((Join-Path $taskDirectory 'candidate-ci83-baseline.inc'), (($taskLegacyParts -join "`r`n").Replace('CCandidateList::', 'CLegacyCandidateList::')), (New-Object Text.UTF8Encoding($false)))
Write-Output 'Prepared production UI lifecycle routines and pinned CI83 negative control'
