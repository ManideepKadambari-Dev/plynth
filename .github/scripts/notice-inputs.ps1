param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$OutFolder,
    [string]$Build = (Join-Path $env:GITHUB_WORKSPACE 'out/build/msvc')
)

# Gathers what the third party notices are written from: the copyright files vcpkg installed, Qt's bill of
# materials, the font licenses and the list of files in the program folder. It only reads files that are
# already on the runner and downloads nothing.

$ErrorActionPreference = 'Stop'
if (Test-Path $OutFolder) { Remove-Item -Recurse -Force $OutFolder }
New-Item -ItemType Directory -Force $OutFolder | Out-Null

$share = Get-ChildItem -Path (Join-Path $Build 'vcpkg_installed') -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'share' } | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($share) {
    foreach ($port in Get-ChildItem -LiteralPath $share -Directory) {
        $copyright = Join-Path $port.FullName 'copyright'
        if (-not (Test-Path -LiteralPath $copyright)) { continue }
        $target = Join-Path $OutFolder "vcpkg/$($port.Name)"
        New-Item -ItemType Directory -Force $target | Out-Null
        Copy-Item -LiteralPath $copyright $target
        $spdx = Join-Path $port.FullName 'vcpkg.spdx.json'
        if (Test-Path -LiteralPath $spdx) { Copy-Item -LiteralPath $spdx $target }
    }
} else {
    'No vcpkg_installed share folder was found.' | Out-File (Join-Path $OutFolder 'missing.txt') -Append -Encoding utf8
}

if ($env:QT_ROOT_DIR) {
    $sbom = Join-Path $env:QT_ROOT_DIR 'sbom'
    if (Test-Path $sbom) {
        Copy-Item -Recurse $sbom (Join-Path $OutFolder 'qt-sbom')
    } else {
        "Qt has no sbom folder at $sbom." | Out-File (Join-Path $OutFolder 'missing.txt') -Append -Encoding utf8
    }
}

$fonts = Join-Path $OutFolder 'fonts'
New-Item -ItemType Directory -Force $fonts | Out-Null
Get-ChildItem (Join-Path $env:GITHUB_WORKSPACE 'app/resources/fonts') -Filter '*OFL*' | Copy-Item -Destination $fonts

$packageFull = (Resolve-Path $Package).Path
$lines = foreach ($file in Get-ChildItem -LiteralPath $packageFull -Recurse -File | Sort-Object FullName) {
    $relative = $file.FullName.Substring($packageFull.Length).TrimStart('\', '/') -replace '\\', '/'
    $version = if ($file.Extension -in '.dll', '.exe') { $file.VersionInfo.FileVersion } else { '' }
    "$relative`t$($file.Length)`t$version"
}
$lines | Out-File (Join-Path $OutFolder 'package-files.txt') -Encoding utf8

$ports = if ($share) { (Get-ChildItem -LiteralPath (Join-Path $OutFolder 'vcpkg') -Directory -ErrorAction SilentlyContinue).Count } else { 0 }
"Notice inputs: $ports vcpkg copyright files and $($lines.Count) package files listed." |
    Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
