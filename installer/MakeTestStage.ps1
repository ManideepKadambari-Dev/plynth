param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$Out
)

# Makes a test copy of a checked stage whose three programs differ by a few bytes at their end, so an update
# to it can be seen file by file. The bytes lie past the end of the program image and change nothing when it
# runs. Only test installers are built from it. It refuses to run anywhere but a GitHub runner.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'MakeTestStage.ps1 runs only on a GitHub hosted runner'
}
if (Test-Path $Out) { throw "the folder $Out already exists" }
Copy-Item -Recurse $Stage $Out
$changed = @('plynth.exe', 'plynth.com', 'cli/plynth.com')
$marker = [System.Text.Encoding]::ASCII.GetBytes('plynth-update-test-build')
foreach ($relative in $changed) {
    $stream = [System.IO.File]::Open((Join-Path $Out $relative), 'Append', 'Write')
    try { $stream.Write($marker, 0, $marker.Length) } finally { $stream.Dispose() }
}
$packageFile = Join-Path $Out 'package.json'
$package = Get-Content -Raw $packageFile | ConvertFrom-Json
foreach ($entry in $package.files) {
    if ($changed -contains $entry.path) {
        $entry.sha256 = (Get-FileHash -Algorithm SHA256 (Join-Path $Out $entry.path)).Hash.ToLowerInvariant()
    }
}
$package | ConvertTo-Json -Depth 5 | Out-File -FilePath $packageFile -Encoding utf8NoBOM
"Test stage in $Out"
