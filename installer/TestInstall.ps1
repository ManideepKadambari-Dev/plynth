param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$Stage,
    [string]$NoRuntimeSetup = ''
)

# Installs, checks, installs again and uninstalls Plynth on a throwaway GitHub runner. It changes the user's
# command line path and Start menu, so it refuses to run anywhere else.
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

# Runs one installer or uninstaller silently and waits for every process it starts.
function Invoke-Setup([string]$file, [string[]]$arguments) {
    $p = Start-Process -FilePath $file -ArgumentList $arguments -PassThru -Wait
    $deadline = (Get-Date).AddMinutes(5)
    while ((Get-Process | Where-Object { $_.ProcessName -like 'unins*' -or $_.ProcessName -like '_iu*' -or $_.ProcessName -eq 'PlynthSetup' -or $_.ProcessName -like 'is-*' }) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
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
} finally {
    Copy-Item (Join-Path $scratch '*.log') $env:RUNNER_TEMP -ErrorAction SilentlyContinue
}

$hash = (Get-FileHash -Algorithm SHA256 $Setup).Hash.ToLowerInvariant()
Write-Host "::notice title=PlynthSetup.exe SHA-256::$hash"
@('### Install test', '', "PlynthSetup.exe SHA-256: ``$hash``", '',
  $(if ($failures.Count) { "$($failures.Count) checks failed." } else { 'Every install check passed.' }), '') |
    Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
if ($failures.Count) { exit 1 }
