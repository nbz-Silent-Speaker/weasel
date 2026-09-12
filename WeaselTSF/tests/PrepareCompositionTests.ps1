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
Write-Output 'Prepared production UI lifecycle routines for recovery tests'
