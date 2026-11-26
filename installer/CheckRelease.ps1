param(
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$Files,
    [Parameter(Mandatory)][string]$Stage
)

# Checks a tag and its release files before the draft release is made. Every problem prints one line.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'PackageRules.ps1')
$problems = [System.Collections.Generic.List[string]]::new()

# Records one problem, shown as an error line on GitHub.
function Add-Problem([string]$text) {
    $problems.Add($text)
    Write-Host "::error::$text"
}

$version = Get-ProjectVersion $root
if ($Tag -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-[0-9A-Za-z][0-9A-Za-z.-]*)?$') {
    Add-Problem "the tag $Tag is not v<major>.<minor>.<patch> with an optional label"
} elseif ("$($Matches[1]).$($Matches[2]).$($Matches[3])" -ne $version) {
    Add-Problem "the tag $Tag does not match the project version $version"
}

$commit = (git -C $root rev-parse HEAD).Trim()
git -C $root fetch --no-tags --quiet origin main
git -C $root merge-base --is-ancestor $commit FETCH_HEAD
if ($LASTEXITCODE -ne 0) { Add-Problem "the tagged commit $commit is not on main" }
$global:LASTEXITCODE = 0

$package = Get-Content -Raw (Join-Path $Stage 'package.json') | ConvertFrom-Json
if ($package.trial) { Add-Problem 'the stage is a trial' }
if ($package.commit -ne $commit) { Add-Problem 'the stage was made from another commit' }

$notes = Join-Path $root "installer/text/release-notes-$version.md"
if (-not (Test-Path $notes)) { Add-Problem "the release notes installer/text/release-notes-$version.md are missing" }

# A release trusts the main and the spare key, and a third only while keys are changed.
$keys = @(Get-TrustedKeys $root)
$testKeys = @(Get-TestKeys $root)
if ($keys.Count -lt 2 -or $keys.Count -gt 3) { Add-Problem "core/src/update/trusted-keys.txt holds $($keys.Count) keys, a release needs two or three" }
$ids = @()
foreach ($key in $keys) {
    if (-not (Test-PublicKeyLine $key)) { Add-Problem "a line of trusted-keys.txt is not a minisign public key: $key"; continue }
    if ($testKeys -contains $key) { Add-Problem "trusted-keys.txt holds a test key: $key" }
    $ids += [Convert]::ToHexString([Convert]::FromBase64String($key), 2, 8)
}
if (@($keys | Sort-Object -Unique).Count -ne $keys.Count -or @($ids | Sort-Object -Unique).Count -ne $ids.Count) {
    Add-Problem 'trusted-keys.txt holds the same key or key id twice'
}

$setup = Join-Path $Files 'PlynthSetup.exe'
if (Test-Path $setup) {
    # Inno Setup pads these fields with spaces or nulls, so they are compared trimmed.
    $info = (Get-Item $setup).VersionInfo
    $name = "$($info.ProductName)" -replace '^[\s\x00]+|[\s\x00]+$', ''
    $shown = "$($info.ProductVersion)" -replace '^[\s\x00]+|[\s\x00]+$', ''
    if ($shown -ne $version -or $name -ne 'Plynth') {
        Add-Problem "PlynthSetup.exe says $name $shown, the release is Plynth $version"
    }
}

$expected = @('PlynthSetup.exe', "Plynth-$version-windows-x64.zip", "Plynth-$version-third-party-sources.zip", 'SHA256SUMS.txt')
$present = @(Get-ChildItem -File $Files | ForEach-Object Name)
foreach ($name in $expected) { if ($present -notcontains $name) { Add-Problem "release file missing: $name" } }
foreach ($name in $present) { if ($expected -notcontains $name) { Add-Problem "file that is not a release file: $name" } }
$sums = Join-Path $Files 'SHA256SUMS.txt'
if (Test-Path $sums) {
    $seen = @()
    foreach ($line in Get-Content $sums) {
        if ($line -notmatch '^([0-9a-f]{64})  (\S+)$') { Add-Problem "a line of SHA256SUMS.txt has the wrong form: $line"; continue }
        $seen += $Matches[2]
        $file = Join-Path $Files $Matches[2]
        if (-not (Test-Path $file)) { Add-Problem "SHA256SUMS.txt names a missing file: $($Matches[2])"; continue }
        if ((Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant() -ne $Matches[1]) { Add-Problem "the sum of $($Matches[2]) is wrong" }
    }
    foreach ($name in $expected) { if ($name -ne 'SHA256SUMS.txt' -and $seen -notcontains $name) { Add-Problem "SHA256SUMS.txt lacks $name" } }
}

if ($problems.Count) { exit 1 }
"Release check passed for $Tag at $commit"
