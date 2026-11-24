param(
    [Parameter(Mandatory)][string]$Package
)

# Runs the hidden [.graphics] cases of the Debug CLI tests against the packaged app, with software 3D
# rendering and every setting and user folder in the runner's temp folder. A branch without them passes.
$ErrorActionPreference = 'Stop'
$root = $env:GITHUB_WORKSPACE

# Writes a line to the job summary.
function Add-Summary([string]$text) {
    $text | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}

$program = Get-ChildItem (Join-Path $root 'out/build/msvc') -Recurse -File -Filter 'plynth_cli_tests.exe' |
    Where-Object { $_.FullName -match '[\\/]Debug[\\/]' } | Select-Object -First 1
if (-not $program) { throw 'plynth_cli_tests.exe was not found in the Debug build' }
$app = Join-Path $Package 'plynth.exe'
if (-not (Test-Path $app)) { throw "plynth.exe is missing in $Package" }

$listed = @(& $program.FullName '[.graphics]' --list-tests --verbosity quiet | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$global:LASTEXITCODE = 0
if ($listed.Count -eq 0) {
    Add-Summary '### Graphics tests'
    Add-Summary ''
    Add-Summary 'This branch has no [.graphics] cases, so none ran.'
    Add-Summary ''
    exit 0
}

$scratch = Join-Path $env:RUNNER_TEMP 'plynth-graphics'
$folders = @{}
foreach ($name in 'config', 'appdata', 'local', 'home', 'temp', 'private', 'blender', 'blender/config',
                  'blender/scripts', 'blender/extensions', 'blender/datafiles', 'xdg') {
    $folders[$name] = Join-Path $scratch $name
    New-Item -ItemType Directory -Force $folders[$name] | Out-Null
}
$fake = Get-ChildItem (Join-Path $root 'out/build/msvc') -Recurse -File -Filter 'fake_blender.exe' |
    Where-Object { $_.FullName -match '[\\/]Debug[\\/]' } | Select-Object -First 1
if (-not $fake) { throw 'fake_blender.exe was not found in the Debug build, and a real Blender must never start' }

$env:PLYNTH_RENDER_EXE = $app
$env:QT_QPA_PLATFORM = 'windows'
$env:QSG_RHI_BACKEND = 'd3d11'
$env:QSG_RHI_PREFER_SOFTWARE_RENDERER = '1'
$env:PLYNTH_CONFIG_DIR = $folders['config']
$env:APPDATA = $folders['appdata']
$env:LOCALAPPDATA = $folders['local']
$env:USERPROFILE = $folders['home']
$env:HOME = $folders['home']
$env:TEMP = $folders['temp']
$env:TMP = $folders['temp']
$env:XDG_DATA_HOME = $folders['xdg']
$env:PLYNTH_BLENDER_PRIVATE = $folders['private']
$env:BLENDER_USER_RESOURCES = $folders['blender']
$env:BLENDER_USER_CONFIG = $folders['blender/config']
$env:BLENDER_USER_SCRIPTS = $folders['blender/scripts']
$env:BLENDER_USER_EXTENSIONS = $folders['blender/extensions']
$env:BLENDER_USER_DATAFILES = $folders['blender/datafiles']
$env:PLYNTH_BLENDER = $fake.FullName

$log = Join-Path $root 'test-logs/graphics-tests.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
& $program.FullName '[.graphics]' --reporter console 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE

Add-Summary '### Graphics tests'
Add-Summary ''
Add-Summary "$($listed.Count) [.graphics] cases ran against the packaged plynth.exe with software D3D11. Exit code $code."
Add-Summary 'The full output is graphics-tests.log in the test-logs artifact.'
Add-Summary ''
exit $code
