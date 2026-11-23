param(
    [Parameter(Mandatory)][ValidateSet('debug', 'release')][string]$Preset,
    [Parameter(Mandatory)][string]$LogFolder
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $LogFolder | Out-Null
$log = Join-Path $LogFolder "ctest-$Preset.log"
$junit = Join-Path $LogFolder "ctest-$Preset.xml"
$title = if ($Preset -eq 'debug') { 'Debug tests' } else { 'Release tests' }

# Sorts a CTest result word into failed, timed out, not run or crashed.
function Get-Kind([string]$status, [string]$output) {
    if ($status -eq 'Failed') {
        if ($output -match 'fatal error condition|SIGSEGV|SIGABRT|EXCEPTION_ACCESS_VIOLATION|Stack overflow') { return 'crashed' }
        return 'failed'
    }
    if ($status -eq 'Timeout') { return 'timed out' }
    if ($status -in 'Not Run', 'notrun', 'Disabled', 'BAD_COMMAND') { return 'not run' }
    if ($status -match 'SEGFAULT|ILLEGAL|INTERRUPT|NUMERICAL|OTHER_FAULT|Exception|aborted') { return 'crashed' }
    return 'other, see the log'
}

# Reads the failing tests from the JUnit file CTest writes, or from its log when that file is missing.
function Get-Problems {
    $found = @()
    if (Test-Path $junit) {
        [xml]$xml = Get-Content -Raw $junit
        foreach ($case in $xml.SelectNodes('//testcase')) {
            $status = $case.GetAttribute('status')
            if ($status -eq 'run') { continue }
            $node = $case.SelectSingleNode('failure|skipped')
            $word = if ($node) { $node.GetAttribute('message') } else { $status }
            $output = $case.SelectSingleNode('system-out')
            $text = if ($output) { $output.InnerText } else { '' }
            $kind = if ($word -match 'SKIP') { 'skipped' } elseif ($status -in 'notrun', 'disabled') { 'not run' } else { Get-Kind $word $text }
            $found += [pscustomobject]@{ Name = $case.GetAttribute('name'); Status = $word; Kind = $kind }
        }
        return $found
    }
    if (-not (Test-Path $log)) { return $found }
    $inList = $false
    foreach ($line in Get-Content $log) {
        if ($line -match '^The following tests FAILED:') { $inList = $true; continue }
        if (-not $inList) { continue }
        if ($line -match '^\s*\d+\s+-\s+(.+?)\s+\(([^)]+)\)\s*$') {
            $found += [pscustomobject]@{ Name = $Matches[1]; Status = $Matches[2]; Kind = Get-Kind $Matches[2] '' }
        } elseif ($line.Trim()) {
            $inList = $false
        }
    }
    return $found
}

$arguments = @('--preset', $Preset, '-LE', 'blender|gpu', '--timeout', '600', '--no-tests=error',
               '--output-log', $log, '--output-junit', $junit)
# The filter comes from the environment so its text is never part of a script line.
if ($env:TEST_FILTER) { $arguments += @('-R', $env:TEST_FILTER) }
& ctest @arguments
$code = $LASTEXITCODE

$lastTest = Join-Path $env:GITHUB_WORKSPACE 'out/build/msvc/Testing/Temporary/LastTest.log'
if (Test-Path $lastTest) { Copy-Item $lastTest (Join-Path $LogFolder "LastTest-$Preset.log") }

$total = $null
if (Test-Path $log) {
    $total = Get-Content $log | Select-String -Pattern '\d+% tests passed(?:, (\d+) tests? failed)? out of (\d+)' | Select-Object -Last 1
}
$problems = @(Get-Problems)
$lines = @("### $title", '')
if ($total) {
    # CTest leaves out the failed part when nothing failed, and counts skipped tests as passed.
    $groups = $total.Matches[0].Groups
    $failedCount = if ($groups[1].Success) { [int]$groups[1].Value } else { 0 }
    $count = [int]$groups[2].Value
    $skipped = @($problems | Where-Object Kind -eq 'skipped').Count
    $lines += "$($count - $failedCount - $skipped) of $count passed."
} else {
    $lines += "CTest ended with code $code before it printed a total. See the test-logs artifact."
}
if ($env:TEST_FILTER) { $lines += 'Only tests matching the filter ran.' }
if ($problems.Count -gt 0) {
    $groups = $problems | Group-Object Kind | ForEach-Object { "$($_.Count) $($_.Name)" }
    $lines += ($groups -join ', ') + '.'
    $lines += ''
    $lines += '| Test | Result | CTest status |'
    $lines += '|---|---|---|'
    foreach ($p in ($problems | Sort-Object Kind, Name | Select-Object -First 300)) {
        $name = $p.Name -replace '\|', '\|'
        $lines += "| $name | $($p.Kind) | $($p.Status) |"
    }
    if ($problems.Count -gt 300) { $lines += '' ; $lines += "$($problems.Count - 300) more are in the test-logs artifact." }
}
$lines += ''
$lines | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
$lines[2..($lines.Count - 1)] | Out-File -FilePath (Join-Path $LogFolder "result-$Preset.txt") -Encoding utf8

exit $code
