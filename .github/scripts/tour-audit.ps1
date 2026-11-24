param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$OutFolder
)

# Runs the program offscreen in its tour audit mode on a scratch project and fails when a control on any
# screen has a tour id with no entry in the tour text. Blender is a path that does not exist, so nothing can
# start it, and every setting and user folder points into the runner's temp folder.

$ErrorActionPreference = 'Stop'
$scratch = Join-Path $env:RUNNER_TEMP 'plynth-tour-audit'
$project = Join-Path $scratch 'projects/apartment-01'
$cli = Join-Path $Package 'plynth.com'
$app = Join-Path $Package 'plynth.exe'
$folders = @{}
foreach ($name in 'config', 'appdata', 'local', 'home', 'temp', 'private', 'blender', 'xdg', 'projects') {
    $folders[$name] = Join-Path $scratch $name
    New-Item -ItemType Directory -Force $folders[$name] | Out-Null
}
New-Item -ItemType Directory -Force $OutFolder | Out-Null

$environment = @{
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
    BLENDER_USER_CONFIG     = $folders['blender']
    BLENDER_USER_SCRIPTS    = $folders['blender']
    BLENDER_USER_EXTENSIONS = $folders['blender']
    BLENDER_USER_DATAFILES  = $folders['blender']
    PLYNTH_BLENDER          = Join-Path $scratch 'no-blender.exe'
    QT_QPA_PLATFORM         = 'offscreen'
}

# Runs a program with the scratch environment and returns its exit code and output.
function Invoke-Scratch([string]$program, [string[]]$arguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new($program)
    foreach ($a in $arguments) { $start.ArgumentList.Add($a) }
    foreach ($k in $environment.Keys) { $start.Environment[$k] = $environment[$k] }
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $scratch
    $process = [System.Diagnostics.Process]::Start($start)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(180000)) {
        $process.Kill($true)
        return [pscustomobject]@{ Code = 'timed out'; Output = '' }
    }
    return [pscustomobject]@{ Code = $process.ExitCode; Output = $out.Result + $err.Result }
}

$plan = Join-Path $Package 'samples/plans/apartment-01.png'
foreach ($step in @(@('project', 'new', '--project', $project, '--image', $plan),
                    @('scale', 'set', '--project', $project, '--pixels-per-metre', '100'))) {
    $r = Invoke-Scratch $cli $step
    if ($r.Code -ne 0) { throw "plynth $($step[0]) $($step[1]) ended with $($r.Code): $($r.Output)" }
}

$result = Join-Path $OutFolder 'tour-audit.json'
$run = Invoke-Scratch $app @($project, '--tour-audit', $result, '--size', '1440x900')
$summary = @('### Tour entry check', '')
if (Test-Path $result) {
    $audit = Get-Content $result -Raw | ConvertFrom-Json
    $summary += "$(@($audit.controls).Count) control ids found, $(@($audit.without_entry).Count) without an entry, " +
                "$($audit.without_id) controls without an id, $(@($audit.unused_entries).Count) entries no screen showed."
    if (@($audit.without_entry).Count -gt 0) {
        $summary += ''
        $summary += 'Ids without an entry in app/resources/tour.json:'
        $summary += ''
        $summary += @($audit.without_entry) | ForEach-Object { "- ``$_``" }
    }
} else {
    $summary += "The audit wrote no result. Exit code $($run.Code). $($run.Output)"
}
$summary += ''
if ($env:GITHUB_STEP_SUMMARY) { $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8 }
$summary | ForEach-Object { Write-Host $_ }
if ($run.Code -ne 0) { exit 1 }
