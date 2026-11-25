param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string]$Version
)

# Builds the zip of exact third party sources: the Qt module archives from qt.io, every source archive vcpkg
# downloaded for this build and the port recipes it used. It holds no program. Run it only in a build that
# restored no vcpkg cache, so every source archive was really downloaded.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$list = Get-Content -Raw (Join-Path $PSScriptRoot 'sources.json') | ConvertFrom-Json
$work = Join-Path $env:RUNNER_TEMP 'plynth-sources'
$bundle = Join-Path $work "Plynth-$Version-third-party-sources"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force (Join-Path $bundle 'qt'), (Join-Path $bundle 'vcpkg/downloads'), (Join-Path $bundle 'vcpkg/ports') | Out-Null
$rows = [System.Collections.Generic.List[string]]::new()

# Adds one line to the index with the file's SHA-256 and where it came from.
function Add-Row([string]$file, [string]$from) {
    $relative = [System.IO.Path]::GetRelativePath($bundle, $file).Replace('\', '/')
    $rows.Add("$((Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant())  $relative  $from")
}

$qt = $list.qt
$md5 = @{}
$sums = Join-Path $work 'md5sums.txt'
try {
    Invoke-WebRequest -Uri "$($qt.archive)/md5sums.txt" -OutFile $sums
    foreach ($line in Get-Content $sums) { if ($line -match '^([0-9a-f]{32})\s+\*?(\S+)$') { $md5[$Matches[2]] = $Matches[1] } }
} catch {
    Write-Host "::warning::Qt's md5sums.txt could not be read, so the Qt archives are recorded by SHA-256 only"
}
foreach ($module in $qt.modules) {
    $name = "$module-everywhere-src-$($qt.version).tar.xz"
    $file = Join-Path $bundle "qt/$name"
    Invoke-WebRequest -Uri "$($qt.archive)/$name" -OutFile $file
    if ($md5.ContainsKey($name) -and (Get-FileHash -Algorithm MD5 $file).Hash.ToLowerInvariant() -ne $md5[$name]) {
        throw "$name does not match Qt's published checksum"
    }
    Add-Row $file "$($qt.archive)/$name"
}

$downloads = Join-Path $env:VCPKG_ROOT 'downloads'
Get-ChildItem $downloads -File | Where-Object { $_.Name -notlike '*.part' -and $_.Name -notlike '*.tmp' } | ForEach-Object {
    $target = Join-Path $bundle "vcpkg/downloads/$($_.Name)"
    Copy-Item $_.FullName $target
    Add-Row $target 'vcpkg download'
}

# The recipe vcpkg used for each installed port: the repo's own port first, then the version vcpkg checked out.
$info = Join-Path $root 'out/build/msvc/vcpkg_installed/vcpkg/info'
$ports = Get-ChildItem $info -Filter *.list | ForEach-Object { ($_.BaseName -split '_')[0] } | Sort-Object -Unique
foreach ($port in $ports) {
    $candidates = @((Join-Path $root "ports/$port")) +
        @(Get-ChildItem (Join-Path $env:VCPKG_ROOT "buildtrees/versioning_/versions/$port") -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName) +
        @((Join-Path $env:VCPKG_ROOT "ports/$port"))
    $recipe = $candidates | Where-Object { Test-Path (Join-Path $_ 'portfile.cmake') } | Select-Object -First 1
    if (-not $recipe) { throw "no port recipe found for $port" }
    $target = Join-Path $bundle "vcpkg/ports/$port"
    Copy-Item -Recurse $recipe $target
    Get-ChildItem -Recurse -File $target | ForEach-Object { Add-Row $_.FullName "port recipe of $port" }
}

@("Third party sources of Plynth $Version", 'SHA-256, path in this zip, origin', '') + $rows |
    Out-File -FilePath (Join-Path $bundle 'SOURCES.txt') -Encoding utf8NoBOM
New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "Plynth-$Version-third-party-sources.zip"
Push-Location $work
try { tar -a -c -f $zip (Split-Path $bundle -Leaf) } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "making the zip failed with code $LASTEXITCODE" }
"Sources zip: $([math]::Round((Get-Item $zip).Length / 1MB)) MB, $($rows.Count) files"
