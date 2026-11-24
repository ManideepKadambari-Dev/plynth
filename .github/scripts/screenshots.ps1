param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$OutFolder
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = $env:GITHUB_WORKSPACE
$sizes = '1280x720', '1440x900', '1920x1080'
$scratch = Join-Path $env:RUNNER_TEMP 'plynth-screens'
$project = Join-Path $scratch 'projects/apartment-01'
$cli = Join-Path $Package 'plynth.com'
$app = Join-Path $Package 'plynth.exe'

$folders = @{}
foreach ($name in 'config', 'appdata', 'local', 'home', 'temp', 'private', 'blender', 'blender/config',
                  'blender/scripts', 'blender/extensions', 'blender/datafiles', 'xdg', 'projects') {
    $folders[$name] = Join-Path $scratch $name
    New-Item -ItemType Directory -Force $folders[$name] | Out-Null
}
New-Item -ItemType Directory -Force $OutFolder | Out-Null

$fake = Get-ChildItem (Join-Path $root 'out/build/msvc') -Recurse -File -Filter 'fake_blender.exe' |
    Where-Object { $_.FullName -match '[\\/]Debug[\\/]' } | Select-Object -First 1
if (-not $fake) { throw 'fake_blender.exe was not found in the Debug build, and a real Blender must never start' }

# Every setting and user folder points into the runner's temp folder, and Blender is always the fake one.
$scratchEnvironment = @{
    PLYNTH_CONFIG_DIR       = $folders['config']
    APPDATA                 = $folders['appdata']
    LOCALAPPDATA            = $folders['local']
    USERPROFILE             = $folders['home']
    HOME                    = $folders['home']
    TEMP                    = $folders['temp']
    TMP                     = $folders['temp']
    XDG_DATA_HOME           = $folders['xdg']
    PLYNTH_BLENDER_PRIVATE  = $folders['private']
    BLENDER_USER_RESOURCES  = $folders['blender']
    BLENDER_USER_CONFIG     = $folders['blender/config']
    BLENDER_USER_SCRIPTS    = $folders['blender/scripts']
    BLENDER_USER_EXTENSIONS = $folders['blender/extensions']
    BLENDER_USER_DATAFILES  = $folders['blender/datafiles']
    PLYNTH_BLENDER          = $fake.FullName
}

# Splits a line of options into words, keeping text in double quotes together.
function Split-Options([string]$text) {
    $words = @()
    foreach ($m in [regex]::Matches($text, '"([^"]*)"|(\S+)')) {
        $words += if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
    }
    return $words
}

# Runs Plynth with the scratch folders and waits for it, and stops it after the time limit.
function Invoke-Plynth([string]$program, [string[]]$arguments, [hashtable]$extra = @{}, [int]$seconds = 180) {
    $start = [System.Diagnostics.ProcessStartInfo]::new($program)
    foreach ($a in $arguments) { $start.ArgumentList.Add($a) }
    foreach ($k in $scratchEnvironment.Keys) { $start.Environment[$k] = $scratchEnvironment[$k] }
    foreach ($k in $extra.Keys) { $start.Environment[$k] = $extra[$k] }
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $scratch
    $process = [System.Diagnostics.Process]::Start($start)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($seconds * 1000)) {
        $process.Kill($true)
        return [pscustomobject]@{ Code = 'timed out'; Output = '' }
    }
    return [pscustomobject]@{ Code = $process.ExitCode; Output = $out.Result + $err.Result }
}

# Says whether the middle of a picture is nearly all one color, which means the 3D view drew nothing.
function Test-Blank([string]$file) {
    if (-not (Test-Path $file)) { return $true }
    $bitmap = [System.Drawing.Bitmap]::new($file)
    try {
        $counts = @{}
        $total = 0
        for ($y = [int]($bitmap.Height * 0.25); $y -lt [int]($bitmap.Height * 0.75); $y += 8) {
            for ($x = [int]($bitmap.Width * 0.3); $x -lt [int]($bitmap.Width * 0.7); $x += 8) {
                $c = $bitmap.GetPixel($x, $y).ToArgb()
                $counts[$c] = 1 + [int]$counts[$c]
                $total++
            }
        }
        $top = ($counts.Values | Measure-Object -Maximum).Maximum
        return ($top / [math]::Max(1, $total)) -gt 0.98
    } finally {
        $bitmap.Dispose()
    }
}

$plan = Join-Path $Package 'samples/plans/apartment-01.png'
$steps = @(
    @('project', 'new', '--project', $project, '--image', $plan),
    @('scale', 'set', '--project', $project, '--pixels-per-metre', '100'),
    @('detect', '--project', $project, '--apply')
)
foreach ($s in $steps) {
    $r = Invoke-Plynth $cli $s
    if ($r.Code -ne 0) { throw "plynth $($s[0]) $($s[1]) ended with $($r.Code): $($r.Output)" }
}

# A library in the scratch folder, so the catalog windows show a catalog. The samples are imported through the
# fake Blender; an item it cannot import is only a warning, since the pictures still show the windows.
$library = Join-Path $scratch 'library'
$r = Invoke-Plynth $cli @('library', 'init', $library, '--use')
if ($r.Code -ne 0) { throw "plynth library init ended with $($r.Code): $($r.Output)" }
$r = Invoke-Plynth $cli @('samples', 'add', '--library', $library) @{} 600
if ($r.Code -ne 0) { Write-Host "::warning::plynth samples add ended with $($r.Code): $($r.Output)" }

$screens = @()
foreach ($line in Get-Content (Join-Path $root '.github/screens.txt')) {
    if (-not $line.Trim()) { continue }
    $at = $line.IndexOf(':')
    $screens += [pscustomobject]@{ Name = $line.Substring(0, $at).Trim(); Options = $line.Substring($at + 1).Trim() }
}
# The extra options come from the environment and are only ever split into words, never run as a script.
if ($env:APP_OPTIONS) { $screens += [pscustomobject]@{ Name = 'extra'; Options = "{project} $env:APP_OPTIONS" } }

$offscreen = @{ QT_QPA_PLATFORM = 'offscreen' }
$software = @{ QT_QPA_PLATFORM = 'windows'; QSG_RHI_BACKEND = 'd3d11'; QSG_RHI_PREFER_SOFTWARE_RENDERER = '1' }
$rows = @()
$failed = 0
foreach ($screen in $screens) {
    $words = @(Split-Options $screen.Options | ForEach-Object { if ($_ -eq '{project}') { $project } else { $_ } })
    $threeD = $screen.Options -match '--view\s+3d'
    foreach ($size in $sizes) {
        $file = Join-Path $OutFolder "$($screen.Name)-$size.png"
        $arguments = $words + @('--screenshot', $file, '--size', $size)
        $r = Invoke-Plynth $app $arguments $offscreen
        $mode = 'offscreen'
        if ($threeD -and ($r.Code -ne 0 -or (Test-Blank $file))) {
            $r = Invoke-Plynth $app $arguments $software
            $mode = 'windows platform with software D3D11, offscreen drew nothing'
            if ($r.Code -eq 0 -and (Test-Blank $file)) { $mode += '; still blank' }
        }
        if ($r.Code -ne 0) { $failed++ }
        $rows += "| $($screen.Name) | $size | $($r.Code) | $mode |"
    }
}

$summary = @('### Screenshots', '',
    'Exit codes: 0 saved, 1 could not save, 2 bad options, 3 the plan image failed, 4 the terminal check failed.',
    'Blender in these runs is the fake test program only.', '',
    '| Screen | Size | Exit | Rendering |', '|---|---|---|---|') + $rows + @('')
$summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
if ($failed -gt 0) { throw "$failed screenshots failed" }
