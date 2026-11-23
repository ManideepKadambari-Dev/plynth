param(
    [Parameter(Mandatory)][string]$OutFolder
)

$ErrorActionPreference = 'Stop'
$root = $env:GITHUB_WORKSPACE
$name = $env:WRITE_FIXTURE
if (-not $name) { throw 'WRITE_FIXTURE is empty' }

# Writes a line to the job summary.
function Add-Summary([string]$text) {
    $text | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}

$program = Get-ChildItem (Join-Path $root 'out/build/msvc') -Recurse -File -Filter 'plynth_core_tests.exe' |
    Where-Object { $_.FullName -match '[\\/]Debug[\\/]' } | Select-Object -First 1
if (-not $program) { throw 'plynth_core_tests.exe was not found in the Debug build' }

# Catch reads commas, brackets, stars and backslashes in a name as part of its filter language.
$spec = $name -replace '([\\,\[\]\*"~])', '\$1'

$listed = @(& $program.FullName $spec --list-tests --verbosity quiet | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($listed.Count -ne 1 -or $listed[0] -ne $name) {
    Add-Summary '### Written fixtures'
    Add-Summary "The name did not match exactly one test case. It matched: $($listed -join '; ')"
    throw 'the name must match exactly one test case'
}

& $program.FullName $spec --warn NoTests
$code = $LASTEXITCODE

New-Item -ItemType Directory -Force $OutFolder | Out-Null
$changed = @(git -C $root status --porcelain --untracked-files=all -- tests | ForEach-Object { $_.Substring(3).Trim('"') })
foreach ($file in $changed) {
    $source = Join-Path $root $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    $target = Join-Path $OutFolder $file
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $source $target
}
git -C $root diff -- tests | Out-File -FilePath (Join-Path $OutFolder 'tracked-changes.patch') -Encoding utf8

Add-Summary '### Written fixtures'
Add-Summary "The test case ended with code $code. This run is not a gate."
if ($changed.Count -eq 0) { Add-Summary 'No file under tests changed.' }
foreach ($file in $changed) { Add-Summary "- $file" }
Add-Summary ''

exit $code
