param(
    [Parameter(Mandatory)][string]$Stage,
    [switch]$Release
)

# Checks a staged folder before it becomes an installer. Every problem prints one line, then it exits 1.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'PackageRules.ps1')
$Stage = (Resolve-Path $Stage).Path
$problems = [System.Collections.Generic.List[string]]::new()

# Records one problem, shown as an error line on GitHub.
function Add-Problem([string]$text) {
    $problems.Add($text)
    Write-Host "::error::$text"
}

$packageFile = Join-Path $Stage 'package.json'
if (-not (Test-Path $packageFile)) { throw 'package.json is missing' }
$package = Get-Content -Raw $packageFile | ConvertFrom-Json
if ($package.format -ne 'plynth-package' -or $package.version -ne 1) { Add-Problem 'package.json has the wrong format' }
if ($Release -and $package.trial) { Add-Problem 'a trial stage cannot be a release' }
if ($package.app_version -ne (Get-ProjectVersion $root)) { Add-Problem "package.json says $($package.app_version), the project says $(Get-ProjectVersion $root)" }

foreach ($required in $script:RequiredFiles) {
    if (Test-Path (Join-Path $Stage $required)) { continue }
    if ($package.trial -and $script:LaterFiles -contains $required) { continue }
    Add-Problem "required file missing: $required"
}

$listed = @{}
foreach ($entry in $package.files) { $listed[$entry.path] = $entry.sha256 }
$present = @{}
foreach ($file in Get-ChildItem -Recurse -Force $Stage) {
    $relative = [System.IO.Path]::GetRelativePath($Stage, $file.FullName).Replace('\', '/')
    if ($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { Add-Problem "link in the package: $relative" }
    if ($file.PSIsContainer) { continue }
    if ($relative -eq 'package.json') { continue }
    $present[$relative] = $true
    if ($relative.Length -gt 140) { Add-Problem "path longer than 140 characters: $relative" }
    if (Test-Never $file.Name) { Add-Problem "file that must never ship: $relative" }
    if (-not $listed.ContainsKey($relative)) { Add-Problem "file not in package.json: $relative"; continue }
    $hash = (Get-FileHash -Algorithm SHA256 $file.FullName).Hash.ToLowerInvariant()
    if ($hash -ne $listed[$relative]) { Add-Problem "file changed since staging: $relative" }
    if ($file.Extension -in '.md', '.txt', '.json', '.py', '.qml', '.toml', '.cfg', '.ini', '.xml', '.html') {
        $text = Get-Content -Raw -LiteralPath $file.FullName
        if ($text -match '(?i)[a-z]:\\users\\|[a-z]:/users/|d:\\a\\plynth|d:/a/plynth') { Add-Problem "local path in text file: $relative" }
    }
}
foreach ($path in $listed.Keys) {
    if (-not $present.ContainsKey($path)) { Add-Problem "listed in package.json but missing: $path" }
}

# Every import of every program and library must resolve inside the package, to Windows or to the C++ runtime.
$system = Join-Path $env:SystemRoot 'System32'
$binaries = Get-ChildItem -Recurse -File $Stage -Include *.exe, *.com, *.dll
$names = @{}
foreach ($b in $binaries) { $names[$b.Name.ToLowerInvariant()] = $true }
foreach ($b in $binaries) {
    $relative = [System.IO.Path]::GetRelativePath($Stage, $b.FullName).Replace('\', '/')
    $output = & dumpbin /nologo /dependents $b.FullName
    if ($LASTEXITCODE -ne 0) { Add-Problem "dumpbin could not read $relative"; continue }
    $imports = $output | Where-Object { $_ -match '^\s+\S+\.(dll|DLL|exe)\s*$' } | ForEach-Object { $_.Trim() }
    foreach ($import in $imports) {
        $lower = $import.ToLowerInvariant()
        if ($script:DebugRuntimeNames -contains $lower) { Add-Problem "$relative is a Debug build: it imports $import"; continue }
        if ($names.ContainsKey($lower)) { continue }
        $runtime = $false
        foreach ($pattern in $script:RuntimeNames) { if ($lower -like $pattern) { $runtime = $true } }
        if ($runtime) { continue }
        if (Test-Path (Join-Path $system $import)) { continue }
        Add-Problem "$relative imports $import, which is not in the package or in Windows"
    }
}
$global:LASTEXITCODE = 0

$logo = (Get-FileHash -Algorithm SHA256 (Join-Path $root 'app/resources/logo.png')).Hash.ToLowerInvariant()
$expected = (Get-Content (Join-Path $PSScriptRoot 'logo.sha256') -TotalCount 1).Trim().ToLowerInvariant()
if ($logo -ne $expected) { Add-Problem 'app/resources/logo.png differs from the recorded hash' }

if ($problems.Count -gt 0) {
    "The package check found $($problems.Count) problems." | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
    exit 1
}
"Package check passed: $($present.Count) files, $(@($binaries).Count) programs and libraries."
