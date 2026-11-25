param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$Stage,
    [string]$NoRuntimeSetup = '',
    [string]$NewerSetup = '',
    [string]$NewerStage = '',
    [string]$NewerVersion = '',
    [string]$NewerNoRuntimeSetup = '',
    [string]$LowerSetup = ''
)

# Installs, checks, installs again and uninstalls Plynth on a throwaway GitHub runner. With the newer test
# installers it also checks update mode. It changes the user's command line path and Start menu, so it
# refuses to run anywhere else.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'TestInstall.ps1 runs only on a GitHub hosted runner'
}
$app = Join-Path $env:LOCALAPPDATA 'Programs\Plynth'
$cli = Join-Path $app 'cli'
$scratch = Join-Path $env:RUNNER_TEMP 'plynth-install-test'
New-Item -ItemType Directory -Force $scratch, (Join-Path $scratch 'config') | Out-Null
$env:PLYNTH_CONFIG_DIR = Join-Path $scratch 'config'
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$package = Get-Content -Raw (Join-Path $Stage 'package.json') | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()

# Records one failed check, shown as an error line on GitHub.
function Add-Failure([string]$text) {
    $failures.Add($text)
    Write-Host "::error::$text"
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

# Reads the user's Path value as stored, without expanding variables.
function Get-UserPath {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
    try { return [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
    finally { $key.Close() }
}

# Says how often the cli folder is on the user's path.
function Get-CliCount {
    return @((Get-UserPath) -split ';' | Where-Object { $_.Trim().TrimEnd('\') -ieq $cli }).Count
}

# Runs one Plynth command line and returns its parsed answer, or null.
function Invoke-Cli([string]$launcher, [string[]]$arguments) {
    $text = & $launcher @arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    $global:LASTEXITCODE = 0
    try { return [pscustomobject]@{ Code = $code; Answer = ($text | ConvertFrom-Json) } }
    catch { return [pscustomobject]@{ Code = $code; Answer = $null } }
}

# The path starts long and with a variable entry, both of which must survive.
$originalKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
$originalPath = $originalKey.GetValue('Path', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$long = (1..60 | ForEach-Object { "C:\plynth-install-test\entry-number-$_" }) -join ';'
$seeded = "$long;%USERPROFILE%\plynth-variable-entry"
if ($originalPath) { $seeded = "$originalPath;$seeded" }
$originalKey.SetValue('Path', $seeded, [Microsoft.Win32.RegistryValueKind]::ExpandString)
$originalKey.Close()

$userData = @((Join-Path $env:APPDATA 'Plynth\keep.txt'), (Join-Path $env:LOCALAPPDATA 'Plynth\keep.txt'),
              (Join-Path $env:USERPROFILE 'Documents\Plynth\keep.txt'))
foreach ($f in $userData) { New-Item -ItemType Directory -Force (Split-Path $f) | Out-Null; 'keep' | Out-File $f }

try {
    $log = Join-Path $scratch 'install-1.log'
    $code = Invoke-Setup $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log")
    if ($code -ne 0) { Add-Failure "the first install ended with code $code" }

    foreach ($entry in $package.files) {
        $file = Join-Path $app $entry.path
        if (-not (Test-Path -LiteralPath $file)) { Add-Failure "not installed: $($entry.path)"; continue }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant() -ne $entry.sha256) {
            Add-Failure "installed file differs from the stage: $($entry.path)"
        }
    }
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Plynth.lnk'
    if (-not (Test-Path $menu)) { Add-Failure 'no Start menu entry' }

    foreach ($launcher in (Join-Path $app 'plynth.com'), (Join-Path $cli 'plynth.com')) {
        $r = Invoke-Cli $launcher @('version')
        if ($r.Code -ne 0 -or -not $r.Answer -or $r.Answer.result.version -ne $package.app_version) {
            Add-Failure "$launcher version did not answer $($package.app_version)"
        }
        $r = Invoke-Cli $launcher @('help')
        if ($r.Code -ne 0) { Add-Failure "$launcher help ended with code $($r.Code)" }
    }

    $count = Get-CliCount
    if ($count -ne 1) { Add-Failure "the cli folder is on the path $count times after the first install" }
    $path = Get-UserPath
    if ($path -notlike "*$long*" -or $path -notlike '*%USERPROFILE%\plynth-variable-entry*') {
        Add-Failure 'the install changed other entries of the path'
    }

    $shot = Join-Path $scratch 'home.png'
    $started = $false
    foreach ($try in 1..3) {
        $p = Start-Process -FilePath (Join-Path $app 'plynth.exe') -ArgumentList @('--home', '--screenshot', $shot, '--size', '1280x720') -PassThru
        if ($p.WaitForExit(120000) -and $p.ExitCode -eq 0 -and (Test-Path $shot)) { $started = $true; break }
        if (-not $p.HasExited) { $p.Kill($true) }
    }
    if (-not $started) { Write-Host '::warning::The installed app did not save a home screenshot with the Windows platform in three tries' }

    $stale = Join-Path $app 'blender\stale_script.py'
    'stale' | Out-File $stale
    $log = Join-Path $scratch 'install-2.log'
    $code = Invoke-Setup $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log")
    if ($code -ne 0) { Add-Failure "the second install ended with code $code" }
    if (Test-Path $stale) { Add-Failure 'a stale script in blender survived the second install' }
    $count = Get-CliCount
    if ($count -ne 1) { Add-Failure "the cli folder is on the path $count times after the second install" }
    if ((Get-Content -Raw (Join-Path $scratch 'install-2.log')) -match 'runtime is missing') {
        Add-Failure 'the runtime check says the runtime is missing on a runner that has it'
    }

    $uninstaller = Join-Path $app 'unins000.exe'
    $code = Invoke-Setup $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ($code -ne 0) { Add-Failure "the uninstall ended with code $code" }
    if (Test-Path (Join-Path $app 'plynth.exe')) { Add-Failure 'plynth.exe is still there after the uninstall' }
    if (Test-Path $menu) { Add-Failure 'the Start menu entry is still there after the uninstall' }
    if ((Get-CliCount) -ne 0) { Add-Failure 'the cli folder is still on the path after the uninstall' }
    $path = Get-UserPath
    if ($path -notlike "*$long*" -or $path -notlike '*%USERPROFILE%\plynth-variable-entry*') {
        Add-Failure 'the uninstall changed other entries of the path'
    }
    foreach ($f in $userData) { if (-not (Test-Path $f)) { Add-Failure "the uninstall removed user data: $f" } }

    if ($NoRuntimeSetup) {
        $log = Join-Path $scratch 'install-no-runtime.log'
        $code = Invoke-Setup $NoRuntimeSetup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log")
        if ($code -ne 0) { Add-Failure "the install with a missing runtime ended with code $code" }
        if ((Get-Content -Raw $log) -notmatch 'runtime is missing') { Add-Failure 'a missing runtime was not logged' }
        if (-not (Test-Path (Join-Path $app 'plynth.exe'))) { Add-Failure 'the install with a missing runtime did not finish' }
        Invoke-Setup (Join-Path $app 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') | Out-Null
    }

    if ($NewerSetup) {
        $newerPackage = Get-Content -Raw (Join-Path $NewerStage 'package.json') | ConvertFrom-Json
        $updateKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8473EE0D-F68A-4C90-BB1C-1D2F3A293B12}_is1'
        $script:step = 0

        # Runs one installer in update mode, the way Plynth starts it, and returns its exit code.
        function Invoke-Update([string]$file, [switch]$Reopen) {
            $script:step++
            $arguments = @('/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOCLOSEAPPLICATIONS',
                           '/NORESTARTAPPLICATIONS', '/PLYNTHUPDATE=1', "/LOG=$scratch\update-$($script:step).log")
            if ($Reopen) { $arguments += '/PLYNTHREOPEN=1' }
            return Invoke-Setup $file $arguments
        }

        # The text of the log of the last update run.
        function Get-UpdateLog { return Get-Content -Raw (Join-Path $scratch "update-$($script:step).log") -ErrorAction SilentlyContinue }

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

        # Waits for every setup and uninstall process to end, five minutes at most.
        function Wait-SetupProcesses {
            $deadline = (Get-Date).AddMinutes(5)
            while ((Get-Process | Where-Object { $_.ProcessName -like 'unins*' -or $_.ProcessName -like '_iu*' -or $_.ProcessName -eq 'PlynthSetup' -or $_.ProcessName -like 'is-*' }) -and (Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 500
            }
        }

        # Says whether a Plynth app from the install folder runs now.
        function Test-AppRunning {
            return [bool](Get-Process -Name plynth -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($app, [System.StringComparison]::OrdinalIgnoreCase) })
        }

        $code = Invoke-Update $NewerSetup
        if ($code -eq 0 -or (Test-Path (Join-Path $app 'plynth.exe'))) { Add-Failure 'update mode installed Plynth where none was installed' }

        $code = Invoke-Setup $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$scratch\install-before-update.log")
        if ($code -ne 0) { Add-Failure "the install before the update ended with code $code" }
        # The installed copy is a release, so its update check is switched off before any start.
        $quiet = Invoke-Cli (Join-Path $app 'plynth.com') @('settings', 'set', '--update-check', 'false')
        $userBefore = @{}
        foreach ($f in $userData) { $userBefore[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash }
        $configBefore = Get-Snapshot $env:PLYNTH_CONFIG_DIR
        $before = Get-Snapshot $app

        $refusals = [ordered]@{ 'a lower version' = $LowerSetup; 'the same version' = $Setup; 'a runtime that is too old' = $NewerNoRuntimeSetup }
        foreach ($name in $refusals.Keys) {
            if (-not $refusals[$name]) { continue }
            $code = Invoke-Update $refusals[$name]
            if ($code -eq 0) { Add-Failure "update mode accepted $name" }
            if (-not (Test-SameSnapshot $before (Get-Snapshot $app))) { Add-Failure "update mode with $name changed installed files" }
        }
        if ($NewerNoRuntimeSetup -and (Get-UpdateLog) -notmatch 'Redistributable') { Add-Failure 'a runtime that is too old was not named in the log' }

        foreach ($held in 'plynth.exe', 'plynth.com', 'Qt6Core.dll', 'blender\plynth_worker.py') {
            $stream = [System.IO.File]::Open((Join-Path $app $held), 'Open', 'Read', 'Read')
            try { $code = Invoke-Update $NewerSetup } finally { $stream.Dispose() }
            if ($code -eq 0) { Add-Failure "update mode ran while $held was held open" }
            if ((Get-UpdateLog) -notmatch 'is in use') { Add-Failure "the log does not say that $held is in use" }
            if (-not (Test-SameSnapshot $before (Get-Snapshot $app))) { Add-Failure "a held $held left changed files behind" }
        }

        $running = [System.Threading.Mutex]::new($false, 'PlynthRunning')
        try { $code = Invoke-Update $NewerSetup } finally { $running.Dispose() }
        if ($code -eq 0) { Add-Failure 'update mode ran while Plynth was marked as running' }
        if ((Get-UpdateLog) -notmatch 'still running') { Add-Failure 'the log does not say that Plynth is still running' }
        if (-not (Test-SameSnapshot $before (Get-Snapshot $app))) { Add-Failure 'a running Plynth left changed files behind' }

        # The real update: Plynth is marked running at first and closes a few seconds later, and the installer
        # file is held with read sharing only, as Plynth holds a verified download.
        $running = [System.Threading.Mutex]::new($false, 'PlynthRunning')
        $held = [System.IO.File]::Open($NewerSetup, 'Open', 'Read', 'Read')
        try {
            $script:step++
            $log = Join-Path $scratch "update-$($script:step).log"
            $p = Start-Process -FilePath $NewerSetup -PassThru -ArgumentList @('/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                '/CURRENTUSER', '/NOCLOSEAPPLICATIONS', '/NORESTARTAPPLICATIONS', '/PLYNTHUPDATE=1', "/LOG=$log")
            # Reading the handle once keeps the exit code readable after the process ends.
            $null = $p.Handle
            Start-Sleep -Seconds 3
            $replaced = $true
            try { [System.IO.File]::OpenWrite($NewerSetup).Dispose() } catch { $replaced = $false }
            if ($replaced) { Add-Failure 'the installer file could be opened for writing while it ran' }
            $running.Dispose()
            $p.WaitForExit()
            Wait-SetupProcesses
            $code = $p.ExitCode
        } finally {
            $running.Dispose()
            $held.Dispose()
        }
        if ($code -ne 0) { Add-Failure "the update ended with code $code" }
        foreach ($entry in $newerPackage.files) {
            $file = Join-Path $app $entry.path
            if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant() -ne $entry.sha256) {
                Add-Failure "not replaced by the update: $($entry.path)"
            }
        }
        if ((Get-ItemProperty -Path $updateKey -ErrorAction SilentlyContinue).DisplayVersion -ne $NewerVersion) { Add-Failure "Windows does not list Plynth $NewerVersion after the update" }
        if (Test-AppRunning) { Add-Failure 'Plynth was started again without being asked' }
        foreach ($launcher in (Join-Path $app 'plynth.com'), (Join-Path $cli 'plynth.com')) {
            $r = Invoke-Cli $launcher @('version')
            if ($r.Code -ne 0 -or -not $r.Answer) { Add-Failure "$launcher does not run after the update" }
        }
        if ((Get-CliCount) -ne 1) { Add-Failure 'the cli folder is not on the path exactly once after the update' }
        foreach ($f in $userData) {
            if (-not (Test-Path $f) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash -ne $userBefore[$f]) { Add-Failure "the update changed user data: $f" }
        }
        $configAfter = Get-Snapshot $env:PLYNTH_CONFIG_DIR
        foreach ($key in $configBefore.Keys) {
            if (-not $configAfter.ContainsKey($key) -or $configAfter[$key] -ne $configBefore[$key]) { Add-Failure "the update changed a settings file: $key" }
        }

        # Starting again after the update, only when asked, and only when the copy cannot reach GitHub.
        if ($quiet.Code -eq 0) {
            $code = Invoke-Setup $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$scratch\install-before-reopen.log")
            if ($code -ne 0) { Add-Failure "the install before the reopen check ended with code $code" }
            $code = Invoke-Update $NewerSetup -Reopen
            if ($code -ne 0) { Add-Failure "the update that reopens Plynth ended with code $code" }
            $deadline = (Get-Date).AddSeconds(30)
            while (-not (Test-AppRunning) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
            if (-not (Test-AppRunning)) { Add-Failure 'Plynth was not started again after an update that asked for it' }
            Get-Process -Name plynth -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($app, [System.StringComparison]::OrdinalIgnoreCase) } |
                ForEach-Object { $_.Kill($true); $_.WaitForExit(10000) | Out-Null }
        } else {
            Write-Host '::warning::The update check could not be switched off, so starting Plynth again after an update was not checked'
        }

        $code = Invoke-Setup (Join-Path $app 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        if ($code -ne 0) { Add-Failure "the uninstall after the update checks ended with code $code" }
        foreach ($f in $userData) { if (-not (Test-Path $f)) { Add-Failure "the uninstall removed user data: $f" } }
    }
} finally {
    Copy-Item (Join-Path $scratch '*.log') $env:RUNNER_TEMP -ErrorAction SilentlyContinue
}

$hash = (Get-FileHash -Algorithm SHA256 $Setup).Hash.ToLowerInvariant()
Write-Host "::notice title=PlynthSetup.exe SHA-256::$hash"
@('### Install test', '', "PlynthSetup.exe SHA-256: ``$hash``", '',
  $(if ($failures.Count) { "$($failures.Count) checks failed." } else { 'Every install check passed.' }), '') |
    Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
if ($failures.Count) { exit 1 }
