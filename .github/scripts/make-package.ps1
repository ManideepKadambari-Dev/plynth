param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Config,
    [Parameter(Mandatory)][string]$Destination,
    [Parameter(Mandatory)][string]$LogFolder
)

$ErrorActionPreference = 'Stop'
$root = $env:GITHUB_WORKSPACE
$built = Join-Path $root "out/build/msvc/app/$Config"
if (-not (Test-Path (Join-Path $built 'plynth.exe'))) { throw "plynth.exe was not built in $built" }
if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
New-Item -ItemType Directory -Force $Destination | Out-Null

# The program, its console launcher and the Qt and library files the build placed beside them. The DirectX
# shader compiler is left out as in the installer: the app draws with Direct3D 11, which does not use it.
robocopy $built $Destination /E /XF *.pdb *.ilk *.exp *.lib *_tests.exe dxil.dll dxcompiler.dll /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { throw "copying the program failed with code $LASTEXITCODE" }
$global:LASTEXITCODE = 0

# Only committed files go in, so nothing that is not in the repository can reach the copy.
$tracked = git -C $root ls-files -- blender samples docs schema licenses LICENSE AUTHORS README.md
foreach ($file in $tracked) {
    if ($file -like 'blender/tests/*' -or $file -like 'blender/experiments/*' -or $file -eq 'blender/CMakeLists.txt') { continue }
    if ($file -like 'docs/architecture/*') { continue }
    $target = Join-Path $Destination $file
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item (Join-Path $root $file) $target
}

# The Unreal editor script ships as unreal/ next to the program, without its test project.
$unrealFiles = git -C $root ls-files -- engines/unreal
foreach ($file in $unrealFiles) {
    if ($file -like 'engines/unreal/tests/*') { continue }
    $target = Join-Path $Destination ('unreal/' + $file.Substring('engines/unreal/'.Length))
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item (Join-Path $root $file) $target
}

# A Release copy carries the redistributable C++ runtime so it runs without Visual Studio.
if ($Config -eq 'Release' -and $env:VCToolsRedistDir) {
    $crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
    if ($crt) {
        Get-ChildItem $crt.FullName -Filter *.dll | Where-Object { -not (Test-Path (Join-Path $Destination $_.Name)) } |
            Copy-Item -Destination $Destination
    }
}

$unwanted = Get-ChildItem -Recurse -File $Destination -Include *.zip, *.7z, *.blend, *.blend1, *.pem, *.key, *.env, *.pdb
if ($unwanted) { throw "the copy holds files it must not: $($unwanted.FullName -join ', ')" }

$results = Get-ChildItem $LogFolder -Filter 'result-*.txt' -ErrorAction SilentlyContinue | Sort-Object Name
$info = @(
    "Plynth $Config build",
    "Commit: $env:GITHUB_SHA",
    "Branch or tag: $env:GITHUB_REF_NAME",
    "Built: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC",
    "Run: $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID",
    ''
)
if ($Config -eq 'Debug') {
    $info += 'A Debug build needs the Visual Studio debug runtime, so it runs only on a PC with Visual Studio installed.'
    $info += ''
}
foreach ($r in $results) { $info += Get-Content $r.FullName }
if (-not $results) { $info += 'No test results were recorded for this build.' }
$info | Out-File -FilePath (Join-Path $Destination 'BUILD-INFO.txt') -Encoding utf8

$bytes = (Get-ChildItem -Recurse -File $Destination | Measure-Object -Sum Length).Sum
$size = '{0:N0} MB' -f ($bytes / 1MB)
"Package: $Config, $size before compression." | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
"" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
