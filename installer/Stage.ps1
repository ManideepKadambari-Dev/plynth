param(
    [Parameter(Mandatory)][string]$Programs,
    [Parameter(Mandatory)][string]$Qt,
    [Parameter(Mandatory)][string]$Out,
    [switch]$Trial
)

# Builds the folder the installer installs: the programs, a fresh windeployqt run, the libraries and the
# committed data files. Nothing is copied that is not in the repository or named here.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'PackageRules.ps1')

if (Test-Path $Out) {
    if (Get-ChildItem -Force $Out | Select-Object -First 1) { throw "the stage folder $Out is not empty" }
} else {
    New-Item -ItemType Directory -Force $Out | Out-Null
}
$Out = (Resolve-Path $Out).Path

# Copies one committed file to the same relative path in the stage.
function Copy-Tracked([string]$relative, [string]$targetRelative = $relative) {
    $target = Join-Path $Out $targetRelative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $relative) $target
}

foreach ($name in 'plynth.exe', 'plynth.com') {
    $file = Join-Path $Programs $name
    if (-not (Test-Path $file)) { throw "$name is missing in $Programs" }
    Copy-Item $file $Out
}
New-Item -ItemType Directory -Force (Join-Path $Out 'cli') | Out-Null
Copy-Item (Join-Path $Programs 'plynth.com') (Join-Path $Out 'cli/plynth.com')

$deploy = Join-Path $Qt 'bin/windeployqt.exe'
if (-not (Test-Path $deploy)) { throw "windeployqt was not found in $Qt" }
& $deploy --release --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler `
    --no-opengl-sw --qmldir (Join-Path $root 'app/qml') --dir $Out (Join-Path $Out 'plynth.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with code $LASTEXITCODE" }
$global:LASTEXITCODE = 0

# The libraries vcpkg placed beside the built program. Qt comes from windeployqt above.
Get-ChildItem $Programs -File -Filter *.dll | Where-Object { $_.Name -notlike 'Qt6*' } |
    Where-Object { -not (Test-Never $_.Name) } |
    Where-Object { -not (Test-Path (Join-Path $Out $_.Name)) } |
    Copy-Item -Destination $Out

# windeployqt may still bring files the package must never hold.
Get-ChildItem -Recurse -File $Out | Where-Object { Test-Never $_.Name } | Remove-Item -Force
foreach ($folder in 'qmltooling', 'translations') {
    Get-ChildItem -Recurse -Directory $Out -Filter $folder | Remove-Item -Recurse -Force
}

$tracked = @(git -C $root ls-files)
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }
if (-not $Trial) {
    $dirty = @(git -C $root status --porcelain --untracked-files=no -- $script:DataRoots)
    if ($dirty) { throw "data files differ from the commit: $($dirty -join ', ')" }
}
foreach ($file in $tracked) {
    if (Test-DataFile $file) { Copy-Tracked $file }
}
foreach ($pair in $script:RenamedFiles.GetEnumerator()) {
    if ($tracked -contains $pair.Key) { Copy-Tracked $pair.Key $pair.Value }
}

foreach ($required in $script:RequiredFiles) {
    if (-not (Test-Path (Join-Path $Out $required))) {
        if ($Trial -and $script:LaterFiles -contains $required) { continue }
        throw "a required file is missing from the stage: $required"
    }
}

$version = Get-ProjectVersion $root
$commit = (git -C $root rev-parse HEAD).Trim()
$runtime = if ($env:VCToolsVersion) { ($env:VCToolsVersion -split '\.')[0..1] -join '.' } else { throw 'VCToolsVersion is not set' }
$files = Get-ChildItem -Recurse -File $Out | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($Out, $_.FullName).Replace('\', '/')
    [ordered]@{ path = $relative; sha256 = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant() }
} | Sort-Object { $_.path } -Culture ([cultureinfo]::InvariantCulture)
$package = [ordered]@{
    format = 'plynth-package'; version = 1; app_version = $version; commit = $commit; platform = 'windows-x64'
    qt = (Split-Path (Split-Path $Qt) -Leaf); runtime_minimum = $runtime; trial = [bool]$Trial; files = @($files)
}
$package | ConvertTo-Json -Depth 5 | Out-File -FilePath (Join-Path $Out 'package.json') -Encoding utf8NoBOM
"Staged $(@($files).Count) files of Plynth $version ($(if ($Trial) { 'trial' } else { 'release' })) in $Out"
