param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$NewerSetup,
    [Parameter(Mandatory)][string]$NewerStage,
    [Parameter(Mandatory)][string]$NewerVersion,
    [Parameter(Mandatory)][string]$LowerVersion,
    [string]$Driver = ''
)

# Installs this build, then updates it through core's own check, download, signature check and installer
# start, against a fake newer release served on 127.0.0.1 and signed with a key made for this run. No GitHub
# release is used. It changes the user's install and path, so it refuses to run anywhere but a GitHub runner.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'TestUpdate.ps1 runs only on a GitHub hosted runner'
}
$app = Join-Path $env:LOCALAPPDATA 'Programs\Plynth'
$scratch = Join-Path $env:RUNNER_TEMP 'plynth-update-test'
New-Item -ItemType Directory -Force $scratch, (Join-Path $scratch 'config') | Out-Null
$env:PLYNTH_CONFIG_DIR = Join-Path $scratch 'config'
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$failures = [System.Collections.Generic.List[string]]::new()

# Records one failed check, shown as an error line on GitHub.
function Add-Failure([string]$text) {
    $failures.Add($text)
    Write-Host "::error::$text"
}

# Writes a note to the run summary.
function Add-Summary([string[]]$lines) {
    if ($env:GITHUB_STEP_SUMMARY) { @('### Update test', '') + $lines + @('') | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8 }
    $lines | ForEach-Object { Write-Host $_ }
}

# Waits for every setup and uninstall process to end, five minutes at most.
function Wait-SetupProcesses {
    $deadline = (Get-Date).AddMinutes(5)
    while ((Get-Process | Where-Object { $_.ProcessName -like 'unins*' -or $_.ProcessName -like '_iu*' -or $_.ProcessName -eq 'PlynthSetup' -or $_.ProcessName -like 'is-*' }) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
}

# Runs one installer or uninstaller silently and waits for it and its setup helpers, not for a Plynth it starts.
function Invoke-Setup([string]$file, [string[]]$arguments) {
    # Not -Wait: in PowerShell 7 that also waits for every program the setup starts, such as a reopened Plynth.
    # The uninstaller hands its work to a copy of itself and ends at once, so the helpers it starts are
    # followed by parent id until all of them have ended.
    $started = (Get-Date).AddSeconds(-2)
    $p = Start-Process -FilePath $file -ArgumentList $arguments -PassThru
    $null = $p.Handle
    $family = [System.Collections.Generic.HashSet[int]]::new()
    $null = $family.Add($p.Id)
    $deadline = (Get-Date).AddMinutes(10)
    while ($true) {
        $all = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, Name, CreationDate
        do {
            $grew = $false
            foreach ($q in $all) {
                if ($family.Contains([int]$q.ProcessId) -or -not $family.Contains([int]$q.ParentProcessId)) { continue }
                if ($q.CreationDate -lt $started -or $q.Name -like 'plynth.*') { continue }
                $null = $family.Add([int]$q.ProcessId)
                $grew = $true
            }
        } while ($grew)
        $alive = @($all | Where-Object { $family.Contains([int]$_.ProcessId) -and $_.Name -notlike 'plynth.*' })
        if ($alive.Count -eq 0) { break }
        if ((Get-Date) -gt $deadline) {
            foreach ($q in $alive) { Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue }
            Add-Failure "$([System.IO.Path]::GetFileName($file)) did not end within ten minutes"
            return -1
        }
        Start-Sleep -Milliseconds 300
    }
    $names = (Get-Date).AddMinutes(5)
    while ((Get-Process | Where-Object { $_.ProcessName -like 'unins*' -or $_.ProcessName -like '_iu*' -or $_.ProcessName -eq 'PlynthSetup' -or $_.ProcessName -like 'is-*' }) -and (Get-Date) -lt $names) {
        Start-Sleep -Milliseconds 500
    }
    # A setup that is still letting go of its mutex would make the next one refuse to run.
    $free = (Get-Date).AddSeconds(30)
    $mutex = $null
    while ([System.Threading.Mutex]::TryOpenExisting('PlynthSetup', [ref]$mutex) -and (Get-Date) -lt $free) {
        $mutex.Dispose()
        Start-Sleep -Milliseconds 300
    }
    if ($mutex) { $mutex.Dispose() }
    $p.WaitForExit()
    return $p.ExitCode
}

# Runs one Plynth command line and returns its exit code and parsed answer.
function Invoke-Cli([string]$launcher, [string[]]$arguments) {
    $text = & $launcher @arguments 2>$null | Out-String
    $code = $LASTEXITCODE
    $global:LASTEXITCODE = 0
    try { return [pscustomobject]@{ Code = $code; Answer = ($text | ConvertFrom-Json) } }
    catch { return [pscustomobject]@{ Code = $code; Answer = $null } }
}

# The SHA-256 of every file below a folder, by relative path.
function Get-Snapshot([string]$folder) {
    $map = @{}
    if (-not (Test-Path $folder)) { return $map }
    foreach ($f in Get-ChildItem -Recurse -File -Force $folder) {
        $map[[System.IO.Path]::GetRelativePath($folder, $f.FullName)] = (Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash
    }
    return $map
}

# Says whether two snapshots hold the same files with the same contents.
function Test-SameSnapshot([hashtable]$a, [hashtable]$b) {
    if ($a.Count -ne $b.Count) { return $false }
    foreach ($key in $a.Keys) { if (-not $b.ContainsKey($key) -or $b[$key] -ne $a[$key]) { return $false } }
    return $true
}

# Runs the driver for one case, five minutes at most, and returns its answer. Its output goes to files, so an
# installer it starts cannot keep a pipe open.
function Invoke-Driver([string]$case) {
    Write-Host "Running the driver, case $case"
    $out = Join-Path $scratch "driver-$case.out"
    $err = Join-Path $scratch "driver-$case.err"
    $p = Start-Process -FilePath $Driver -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @('--case', $case, '--app', "`"$app`"", '--installer', "`"$NewerSetup`"", '--current', $current,
                        '--newer', $NewerVersion, '--lower', $LowerVersion)
    $null = $p.Handle
    if (-not $p.WaitForExit(300000)) {
        $p.Kill($true)
        Add-Failure "the driver did not end within five minutes for case $case"
    }
    $text = (Get-Content -Raw $out -ErrorAction SilentlyContinue) + (Get-Content -Raw $err -ErrorAction SilentlyContinue)
    $code = if ($p.HasExited) { $p.ExitCode } else { -1 }
    Write-Host "Driver, case $case, exit $code`: $text"
    $line = ($text -split "`r?`n" | Where-Object { $_.TrimStart().StartsWith('{') } | Select-Object -Last 1)
    try { $answer = $line | ConvertFrom-Json } catch { $answer = $null }
    return [pscustomobject]@{ Code = $code; Answer = $answer }
}

if (-not $Driver -or -not (Test-Path $Driver)) {
    Add-Summary @('Skipped: the update driver was not built, because core has no update code in this branch yet.')
    exit 0
}

$current = (Get-Item $Setup).VersionInfo.ProductVersion
$newerPackage = Get-Content -Raw (Join-Path $NewerStage 'package.json') | ConvertFrom-Json
$updateKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8473EE0D-F68A-4C90-BB1C-1D2F3A293B12}_is1'

$userData = @((Join-Path $env:APPDATA 'Plynth\update-keep.txt'), (Join-Path $env:LOCALAPPDATA 'Plynth\update-keep.txt'),
              (Join-Path $env:USERPROFILE 'Documents\Plynth\update-keep.txt'),
              (Join-Path $scratch 'projects\Demo house\project.json'), (Join-Path $scratch 'library\Library.plynthlib'))
foreach ($f in $userData) { New-Item -ItemType Directory -Force (Split-Path $f) | Out-Null; "keep $(Split-Path $f -Leaf)" | Out-File $f }

try {
    Write-Host "Installing $current"
    $code = Invoke-Setup $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$scratch\install.log")
    if ($code -ne 0) { throw "the install of $current ended with code $code" }
    $launcher = Join-Path $app 'plynth.com'
    Write-Host 'Asking the installed copy for update --status'
    $status = Invoke-Cli $launcher @('update', '--status')
    if ($status.Code -ne 0 -or -not $status.Answer -or -not $status.Answer.result) {
        Add-Summary @('Skipped: the installed plynth has no working "update --status" yet, so the update was not tried.')
        exit 0
    }
    # Only a release counts as installed. A branch run installs a trial, which must say it is not installed.
    $trial = (Get-Content -Raw (Join-Path $app 'package.json') | ConvertFrom-Json).trial
    $expected = if ($trial) { 'not_installed' } else { 'installed' }
    if ($status.Answer.result.install -ne $expected) {
        Add-Failure "the installed copy says $($status.Answer.result.install), and a $(if ($trial) { 'trial' } else { 'release' }) must say $expected"
    }
    Write-Host 'Switching the update check off'
    $setting = Invoke-Cli $launcher @('settings', 'set', '--update-check', 'false')
    if ($setting.Code -ne 0) { Add-Failure 'the installed copy could not switch the update check off' }

    $userBefore = @{}
    foreach ($f in $userData) { $userBefore[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash }
    $configBefore = Get-Snapshot $env:PLYNTH_CONFIG_DIR
    $installed = Get-Snapshot $app

    # Each of these must start nothing and change nothing.
    foreach ($case in 'changed-installer', 'changed-comment', 'downgrade', 'replaced-file') {
        $r = Invoke-Driver $case
        if ($r.Code -ne 0 -or -not $r.Answer -or -not $r.Answer.passed) { Add-Failure "case $case did not behave as it must" }
        if ($r.Answer -and $r.Answer.started) { Add-Failure "case $case started the installer" }
        Start-Sleep -Seconds 3
        Wait-SetupProcesses
        if (-not (Test-SameSnapshot $installed (Get-Snapshot $app))) { Add-Failure "case $case changed the installed files" }
    }

    $r = Invoke-Driver 'good'
    if ($r.Code -ne 0 -or -not $r.Answer -or -not $r.Answer.started) {
        Add-Failure 'the signed newer release was not installed'
    } else {
        Write-Host 'Waiting for the started installer'
        Start-Sleep -Seconds 3
        Wait-SetupProcesses
        Write-Host 'Checking the updated files'
        foreach ($entry in $newerPackage.files) {
            $file = Join-Path $app $entry.path
            if (-not (Test-Path -LiteralPath $file)) { Add-Failure "missing after the update: $($entry.path)"; continue }
            if ((Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant() -ne $entry.sha256) {
                Add-Failure "not replaced by the update: $($entry.path)"
            }
        }
        $shown = (Get-ItemProperty -Path $updateKey -ErrorAction SilentlyContinue).DisplayVersion
        if ($shown -ne $NewerVersion) { Add-Failure "Windows lists Plynth $shown after the update, not $NewerVersion" }
        foreach ($l in (Join-Path $app 'plynth.com'), (Join-Path $app 'cli\plynth.com')) {
            $v = Invoke-Cli $l @('version')
            if ($v.Code -ne 0 -or -not $v.Answer) { Add-Failure "$l does not run after the update" }
        }
        $after = Invoke-Cli $launcher @('update', '--status')
        Write-Host "update --status after the update: $($after.Answer | ConvertTo-Json -Depth 6 -Compress)"
        if (-not $after.Answer -or -not $after.Answer.result.last -or $after.Answer.result.last.to -ne $NewerVersion) {
            Add-Failure 'update --status does not name the update that was started'
        }
        foreach ($f in $userData) {
            if (-not (Test-Path $f) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash -ne $userBefore[$f]) { Add-Failure "user data changed: $f" }
        }
        $configAfter = Get-Snapshot $env:PLYNTH_CONFIG_DIR
        foreach ($key in $configBefore.Keys) {
            if ($key -like 'updates*' -or $key -eq 'update-state.json') { continue }
            if (-not $configAfter.ContainsKey($key) -or $configAfter[$key] -ne $configBefore[$key]) { Add-Failure "a settings file changed: $key" }
        }
    }
} finally {
    if (Test-Path (Join-Path $app 'unins000.exe')) {
        Write-Host 'Uninstalling'
        Invoke-Setup (Join-Path $app 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') | Out-Null
    }
    Copy-Item (Join-Path $scratch '*.log') $env:RUNNER_TEMP -ErrorAction SilentlyContinue
    Get-ChildItem -Recurse -File -Filter '*.log' $env:PLYNTH_CONFIG_DIR -ErrorAction SilentlyContinue | Copy-Item -Destination $env:RUNNER_TEMP -ErrorAction SilentlyContinue
}

if ($failures.Count) {
    Add-Summary @("$($failures.Count) update checks failed.")
    exit 1
}
Add-Summary @("Updated $current to $NewerVersion through the signed local release. Changed installer, changed comment, downgrade and replaced file each started nothing. User data kept.")
