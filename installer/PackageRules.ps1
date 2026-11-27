# What goes into the package and what never does, shared by the stage and the package check.

$script:DataRoots = @('blender', 'schema', 'samples', 'docs', 'licenses', 'LICENSE', 'AUTHORS', 'installer/text')

# Committed files that also go to a second place in the package. The skill file stays in docs/templates,
# where the programs read it, and a copy sits in the Plynth folder for the user to find.
$script:RenamedFiles = @{
    'installer/text/README.txt' = 'README.txt'
    'docs/templates/SKILL.md' = 'SKILL.md'
}

# The three marker files are how the app finds its docs, samples and licenses beside the program.
$script:RequiredFiles = @(
    'plynth.exe', 'plynth.com', 'cli/plynth.com', 'platforms/qwindows.dll', 'blender/plynth_worker.py',
    'blender/guard/startup/plynth_fetch_guard.py', 'blender/guard/tested_versions.json',
    'schema/plan.schema.json', 'samples/shapes/keyhole.shape.json', 'docs/GETTING_STARTED.md', 'LICENSE',
    'AUTHORS', 'README.txt', 'licenses/THIRD_PARTY_NOTICES.md', 'licenses/texts/CC0-1.0.txt', 'samples/LICENSE',
    'samples/samples.json', 'docs/templates/project-agents.md', 'docs/templates/SKILL.md', 'SKILL.md'
)

# Files that later work adds. A trial stage may lack them, a release stage may not.
$script:LaterFiles = @('README.txt')

# C++ runtime files the user installs from Microsoft. Programs may import them, the package never holds them.
$script:RuntimeNames = @('msvcp140*.dll', 'vcruntime140*.dll', 'concrt140*.dll', 'vcomp140*.dll', 'ucrtbase*.dll',
    'api-ms-win-*.dll', 'ext-ms-win-*.dll')

# Says whether a file name is one the package must never hold. Debug builds are found by their imports
# in the package check, not by name.
function Test-Never([string]$name) {
    $never = $script:RuntimeNames + @('dxil.dll', 'dxcompiler.dll', 'd3dcompiler_47.dll', 'opengl32sw.dll',
        '*.pdb', '*.lib', '*.exp', '*.ilk', '*.qmlc', '*.pyc', '*_tests.exe', 'fake_*.exe',
        '*update_driver*', '*httplib*', '*.key', '*.pub', '*.minisig')
    foreach ($pattern in $never) {
        if ($name -like $pattern) { return $true }
    }
    return $false
}

# C++ runtime imports that only a Debug build has.
$script:DebugRuntimeNames = @('msvcp140d.dll', 'vcruntime140d.dll', 'vcruntime140_1d.dll', 'ucrtbased.dll', 'concrt140d.dll')

# Says whether a committed file belongs in the package.
function Test-DataFile([string]$file) {
    if ($file -eq 'LICENSE' -or $file -eq 'AUTHORS') { return $true }
    if ($file -like 'blender/*') {
        # The guard folder is loaded by Blender before any add-on when a store fetch runs.
        $shipped = $file -eq 'blender/plynth_worker.py' -or $file -like 'blender/plynth/*' -or $file -like 'blender/guard/*'
        return $shipped -and $file -notlike '*/__pycache__/*'
    }
    if ($file -like 'schema/*') { return $true }
    if ($file -like 'docs/*') { return $file -notlike 'docs/architecture/*' }
    if ($file -like 'licenses/*') { return $true }
    if ($file -like 'samples/*') {
        if ($file -like 'samples/tools/*' -or $file -like '*.expected.json' -or $file -like '*/__pycache__/*') { return $false }
        return (Split-Path $file -Leaf) -notlike 'make_*.py'
    }
    return $false
}

# Reads the project version from the root CMakeLists.txt.
function Get-ProjectVersion([string]$root) {
    $text = Get-Content -Raw (Join-Path $root 'CMakeLists.txt')
    if ($text -notmatch 'project\(\s*Plynth\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'no project version in CMakeLists.txt' }
    return $Matches[1]
}

# Says whether a line is a minisign public key: "Ed", an 8 byte key id and a 32 byte key, in base64.
function Test-PublicKeyLine([string]$line) {
    if ($line -notmatch '^RW[A-Za-z0-9+/]{54}$') { return $false }
    try { $bytes = [Convert]::FromBase64String($line) } catch { return $false }
    return $bytes.Length -eq 42 -and $bytes[0] -eq 0x45 -and $bytes[1] -eq 0x64
}

# Reads the public keys the programs trust. Blank lines and lines that start with "#" or "untrusted comment:"
# are skipped, as the programs skip them. Any other line must be a key.
function Get-TrustedKeys([string]$root) {
    $file = Join-Path $root 'core/src/update/trusted-keys.txt'
    if (-not (Test-Path -LiteralPath $file)) { return @() }
    return @(Get-Content -LiteralPath $file | ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') -and -not $_.StartsWith('untrusted comment:') })
}

# Finds every public key line in the committed test files, so no test key can reach a release.
function Get-TestKeys([string]$root) {
    $files = @(git -C $root ls-files -- tests cli/tests app/tests)
    $global:LASTEXITCODE = 0
    $keys = @{}
    foreach ($file in $files) {
        $path = Join-Path $root $file
        if ((Get-Item -LiteralPath $path).Length -gt 4MB) { continue }
        $text = [System.IO.File]::ReadAllText($path)
        foreach ($m in [regex]::Matches($text, 'RW[A-Za-z0-9+/]{54}')) {
            if (Test-PublicKeyLine $m.Value) { $keys[$m.Value] = $true }
        }
    }
    return @($keys.Keys)
}
