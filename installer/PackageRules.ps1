# What goes into the package and what never does, shared by the stage and the package check.

$script:DataRoots = @('blender', 'schema', 'samples', 'docs', 'licenses', 'LICENSE', 'AUTHORS', 'installer/text')

$script:RenamedFiles = @{ 'installer/text/README.txt' = 'README.txt' }

# The three marker files are how the app finds its docs, samples and licenses beside the program.
$script:RequiredFiles = @(
    'plynth.exe', 'plynth.com', 'cli/plynth.com', 'platforms/qwindows.dll', 'blender/plynth_worker.py',
    'schema/plan.schema.json', 'samples/shapes/keyhole.shape.json', 'docs/GETTING_STARTED.md', 'LICENSE',
    'AUTHORS', 'README.txt', 'licenses/THIRD_PARTY_NOTICES.md', 'licenses/texts/CC0-1.0.txt', 'samples/LICENSE',
    'samples/samples.json'
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
        '*.pdb', '*.lib', '*.exp', '*.ilk', '*.qmlc', '*.pyc', '*_tests.exe', 'fake_*.exe')
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
        return ($file -eq 'blender/plynth_worker.py' -or $file -like 'blender/plynth/*') -and $file -notlike '*/__pycache__/*'
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
