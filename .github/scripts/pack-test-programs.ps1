param(
    [Parameter(Mandatory)][string]$Destination
)

$ErrorActionPreference = 'Stop'
$root = $env:GITHUB_WORKSPACE
$names = 'plynth_core_tests.exe', 'fake_store_helper.exe', 'plynth_integrity_probe.exe', 'fake_blender.exe',
         'plynth_echo_lines.exe', 'plynth_leaky_blender.exe'
if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
New-Item -ItemType Directory -Force $Destination | Out-Null

# Copies one file to the same path inside the destination as it has inside the checkout.
function Copy-Mirrored([string]$full) {
    $relative = [System.IO.Path]::GetRelativePath($root, $full)
    $target = Join-Path $Destination $relative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $full $target
}

$programs = @(Get-ChildItem (Join-Path $root 'out/build/msvc') -Recurse -File -Include $names |
    Where-Object { $_.FullName -match '[\\/]Debug[\\/]' })
if (-not ($programs | Where-Object Name -eq 'plynth_core_tests.exe')) { throw 'plynth_core_tests.exe was not built' }
foreach ($p in $programs) { Copy-Mirrored $p.FullName }

# The libraries vcpkg placed beside each program. The core tests use no Qt.
foreach ($folder in ($programs | ForEach-Object { $_.DirectoryName } | Sort-Object -Unique)) {
    Get-ChildItem $folder -File -Filter *.dll | ForEach-Object { Copy-Mirrored $_.FullName }
}

# Only committed files, so nothing that is not in the repository can reach the download.
foreach ($file in (git -C $root ls-files -- tests/core/data blender/tests schema samples)) {
    Copy-Mirrored (Join-Path $root $file)
}

$found = ($programs | ForEach-Object { [System.IO.Path]::GetRelativePath($root, $_.FullName) }) -join "`n  "
@(
    "Built from commit $env:GITHUB_SHA on $env:GITHUB_REF_NAME, Debug.",
    "The test programs hold paths of the build machine. Its checkout was: $root",
    'Unpack this folder to that same path, or the tests will not find their data, scripts and helpers.',
    'The Visual Studio debug runtime must be installed: these are Debug programs.',
    '',
    "Programs:`n  $found"
) | Out-File -FilePath (Join-Path $Destination 'PATHS.txt') -Encoding utf8

"Test programs packed from $root. Programs: $(($programs | ForEach-Object Name) -join ', ')." |
    Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
"" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
