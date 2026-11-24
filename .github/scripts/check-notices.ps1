param(
    [Parameter(Mandatory)][string]$Stage,
    [string]$Licenses
)

# Checks that every DLL and font file in a program folder has a row in THIRD_PARTY_NOTICES.md and that every
# license text a row links to is there. Exits with 1 and lists the problems when anything is missing.

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Stage -PathType Container)) { throw "The folder $Stage does not exist." }
$stageFull = (Resolve-Path -LiteralPath $Stage).Path
if (-not $Licenses) {
    $inStage = Join-Path $stageFull 'licenses'
    $Licenses = if (Test-Path (Join-Path $inStage 'THIRD_PARTY_NOTICES.md')) { $inStage } else { Join-Path $PSScriptRoot '../../licenses' }
}
$notices = Join-Path $Licenses 'THIRD_PARTY_NOTICES.md'
if (-not (Test-Path -LiteralPath $notices -PathType Leaf)) { throw "The file $notices does not exist." }

$problems = [System.Collections.Generic.List[string]]::new()
$patterns = [System.Collections.Generic.List[string]]::new()
$section = ''
$rows = 0
foreach ($line in Get-Content -LiteralPath $notices -Encoding utf8) {
    if ($line -match '^## (.+)$') { $section = $Matches[1].Trim(); continue }
    if ($line -notmatch '^\|' -or $line -match '^\|\s*-') { continue }
    $cells = @([regex]::Split($line.Trim().Trim('|'), '(?<!\\)\|') | ForEach-Object { $_.Trim() })
    if ($cells[0] -in @('Component', 'Part')) { continue }
    if ($section -eq 'Shipped components') {
        foreach ($m in [regex]::Matches($cells[3], '`([^`]+)`')) { $patterns.Add($m.Groups[1].Value) }
    } elseif ($section -ne 'Parts of Qt') {
        continue
    }
    $rows++
    $links = [regex]::Matches($cells[5], '\]\(([^)]+)\)')
    if ($links.Count -eq 0) { $problems.Add("The row for $($cells[0]) links no license text.") }
    foreach ($link in $links) {
        $target = $link.Groups[1].Value
        if ($target -match '^https://') { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $Licenses $target) -PathType Leaf)) {
            $problems.Add("The row for $($cells[0]) links $target, which is missing.")
        }
    }
}
if ($rows -eq 0) { $problems.Add('THIRD_PARTY_NOTICES.md has no rows.') }

$shipped = Get-ChildItem -LiteralPath $stageFull -Recurse -File -Include *.dll, *.ttf, *.otf
foreach ($file in $shipped) {
    $relative = $file.FullName.Substring($stageFull.Length).TrimStart('\', '/') -replace '\\', '/'
    $covered = $false
    foreach ($pattern in $patterns) {
        if ($relative -like $pattern) { $covered = $true; break }
    }
    if (-not $covered) { $problems.Add("The shipped file $relative has no row.") }
}

if ((Test-Path (Join-Path $stageFull 'samples')) -and -not (Test-Path (Join-Path $stageFull 'samples/LICENSE'))) {
    $problems.Add('The samples folder has no LICENSE file.')
}

$summary = @('### Third party notices', '')
if ($problems.Count -eq 0) {
    $summary += "Every one of the $($shipped.Count) shipped DLL and font files has a row, and every linked license text is there."
} else {
    $summary += "$($problems.Count) problems:"
    $summary += ''
    $summary += $problems | ForEach-Object { "- $_" }
}
$summary += ''
if ($env:GITHUB_STEP_SUMMARY) { $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8 }
$summary | ForEach-Object { Write-Host $_ }
if ($problems.Count -gt 0) { exit 1 }
