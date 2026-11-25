param(
    [Parameter(Mandatory)][string]$Package
)

# Starts the staged programs from a copy in a folder with a space and a non-ASCII letter, with scratch
# settings, and checks that both launchers answer.
$ErrorActionPreference = 'Stop'
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("plynth smoke " + [char]0x00FC + " " + [guid]::NewGuid().ToString('N').Substring(0, 6))
$copy = Join-Path $scratch 'program'
New-Item -ItemType Directory -Force $copy, (Join-Path $scratch 'config') | Out-Null
Copy-Item -Recurse (Join-Path $Package '*') $copy
foreach ($name in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -like 'PLYNTH_*' } | ForEach-Object Name)) {
    Remove-Item "Env:$name"
}
$env:PLYNTH_CONFIG_DIR = Join-Path $scratch 'config'
$env:QML_DISABLE_DISK_CACHE = '1'
$version = (Get-Content -Raw (Join-Path $Package 'package.json') | ConvertFrom-Json).app_version
$failed = 0

# The update check is switched off before anything else starts, so no check can ever reach GitHub.
& (Join-Path $copy 'plynth.com') settings set --update-check false | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host '::warning::The smoke copy could not switch the update check off. It is not installed, so it makes no request anyway.' }
$global:LASTEXITCODE = 0

foreach ($launcher in (Join-Path $copy 'plynth.com'), (Join-Path $copy 'cli\plynth.com')) {
    $text = & $launcher version 2>&1 | Out-String
    $answer = try { $text | ConvertFrom-Json } catch { $null }
    if ($LASTEXITCODE -ne 0 -or -not $answer -or $answer.result.version -ne $version) {
        Write-Host "::error::$launcher version did not answer $version`: $text"
        $failed++
    }
    & $launcher help | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "::error::$launcher help ended with code $LASTEXITCODE"; $failed++ }
}
$global:LASTEXITCODE = 0
Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
if ($failed) { exit 1 }
"Both launchers answered $version from a copy in $copy"
