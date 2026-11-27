# Checks that the tools needed to build Plynth are installed. It only looks: it installs and changes nothing.
# Start it with tools\check-setup.cmd. Exit code 1 means something required is missing.
param(
    # A build server may have another Visual Studio version. This reports that version instead of failing.
    [switch]$AllowOtherVisualStudio
)

$script:missing = 0

# Prints one line for a tool that was found.
function Write-Found([string]$name, [string]$detail) {
    Write-Host ('  found    {0}: {1}' -f $name, $detail)
}

# Prints one line for a missing tool with what to do, and counts it.
function Write-Missing([string]$name, [string]$todo) {
    Write-Host ('  missing  {0}: {1}' -f $name, $todo) -ForegroundColor Red
    $script:missing++
}

# Asks the Visual Studio Installer which copies of Visual Studio are installed.
function Get-VisualStudioList([string]$vswhere, [string[]]$extra = @()) {
    $text = & $vswhere -all -prerelease -products '*' -format json @extra | Out-String
    if (-not $text.Trim()) { return @() }
    $parsed = $text | ConvertFrom-Json
    return @($parsed | Where-Object { $_ })
}

# Looks for Visual Studio 2026 with the C++ workload and returns its folder, or nothing.
function Test-VisualStudio {
    $name = 'Visual Studio 2026 with C++'
    $install = 'Install Visual Studio 2026 Community with the "Desktop development with C++" workload.'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        Write-Missing $name $install
        return $null
    }
    $all = @(Get-VisualStudioList $vswhere)
    $withCpp = @(Get-VisualStudioList $vswhere @('-requires', 'Microsoft.VisualStudio.Workload.NativeDesktop'))

    # Visual Studio 2026 is version 18.
    $vs2026 = $withCpp | Where-Object { ([version]$_.installationVersion).Major -eq 18 } | Select-Object -First 1
    if ($vs2026) {
        Write-Found $name "$($vs2026.displayName) $($vs2026.installationVersion) at $($vs2026.installationPath)"
        return $vs2026.installationPath
    }
    $noCpp = $all | Where-Object { ([version]$_.installationVersion).Major -eq 18 } | Select-Object -First 1
    if ($noCpp) {
        Write-Missing $name "$($noCpp.displayName) has no C++ workload. Open the Visual Studio Installer, choose Modify and tick ""Desktop development with C++""."
        return $null
    }
    $other = $withCpp | Sort-Object { [version]$_.installationVersion } -Descending | Select-Object -First 1
    if ($other -and $AllowOtherVisualStudio) {
        Write-Found $name "not installed, using $($other.displayName) $($other.installationVersion) at $($other.installationPath), allowed here"
        return $other.installationPath
    }
    if ($other) {
        Write-Missing $name "only $($other.displayName) was found. $install"
        return $null
    }
    Write-Missing $name $install
    return $null
}

# Looks for the CMake that comes with Visual Studio and reads its version.
function Test-CMake([string]$vsPath) {
    $name = 'CMake from Visual Studio'
    if (-not $vsPath) {
        Write-Missing $name 'It comes with Visual Studio, so install Visual Studio first.'
        return
    }
    $cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $cmake)) {
        Write-Missing $name 'Open the Visual Studio Installer, choose Modify and tick "C++ CMake tools for Windows".'
        return
    }
    $first = & $cmake --version | Select-Object -First 1
    if ($first -notmatch '(\d+\.\d+\.\d+)') {
        Write-Missing $name "$cmake did not report its version. Repair Visual Studio with the Visual Studio Installer."
        return
    }
    # CMakePresets.json needs CMake 3.25 or newer.
    $version = [version]$Matches[1]
    if ($version -lt [version]'3.25.0') {
        Write-Missing $name "version $version is older than 3.25. Update Visual Studio with the Visual Studio Installer."
        return
    }
    Write-Found $name "$version at $cmake"
}

# Looks for the Ninja that comes with Visual Studio and reads its version.
function Test-Ninja([string]$vsPath) {
    $name = 'Ninja from Visual Studio'
    if (-not $vsPath) {
        Write-Missing $name 'It comes with Visual Studio, so install Visual Studio first.'
        return
    }
    $ninja = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (-not (Test-Path $ninja)) {
        Write-Missing $name 'Open the Visual Studio Installer, choose Modify and tick "C++ CMake tools for Windows".'
        return
    }
    $version = & $ninja --version | Select-Object -First 1
    Write-Found $name "$version at $ninja"
}

# Checks that QTDIR points at Qt 6.8 built for 64-bit MSVC.
function Test-Qt {
    $name = 'Qt 6.8 MSVC 64-bit (QTDIR)'
    $install = 'Install Qt 6.8 for MSVC 2022 64-bit with the Qt Online Installer and set QTDIR to its folder, such as C:\Qt\6.8.3\msvc2022_64.'
    $qt = $env:QTDIR
    if (-not $qt) {
        Write-Missing $name "QTDIR is not set. $install"
        return
    }
    $config = Join-Path $qt 'mkspecs\qconfig.pri'
    if (-not (Test-Path $config)) {
        Write-Missing $name "no Qt was found in $qt. $install"
        return
    }
    $text = Get-Content -Raw $config
    $version = 'unknown'
    if ($text -match '(?m)^QT_VERSION\s*=\s*(\S+)') { $version = $Matches[1] }
    $arch = 'unknown'
    if ($text -match '(?m)^QT_ARCH\s*=\s*(\S+)') { $arch = $Matches[1] }
    # Only the MSVC build has .lib files. The MinGW build has .a files instead.
    $msvc = Test-Path (Join-Path $qt 'lib\Qt6Core.lib')
    if (-not $version.StartsWith('6.8.')) {
        Write-Missing $name "QTDIR holds Qt $version, but Plynth needs Qt 6.8. $install"
        return
    }
    if ($arch -ne 'x86_64' -or -not $msvc) {
        Write-Missing $name "the Qt in $qt is not the MSVC 64-bit build. $install"
        return
    }
    Write-Found $name "Qt $version at $qt"
}

# Checks that VCPKG_ROOT points at a vcpkg clone with its program and CMake toolchain file.
function Test-Vcpkg {
    $name = 'vcpkg (VCPKG_ROOT)'
    $install = 'Clone https://github.com/microsoft/vcpkg, run bootstrap-vcpkg.bat in it and set VCPKG_ROOT to that folder.'
    $root = $env:VCPKG_ROOT
    if (-not $root) {
        Write-Missing $name "VCPKG_ROOT is not set. $install"
        return
    }
    # The copy inside Visual Studio is not a git clone, so it cannot read the baseline in vcpkg.json.
    if ($root -like '*\Microsoft Visual Studio\*') {
        Write-Missing $name "VCPKG_ROOT points at the copy inside Visual Studio ($root). $install"
        return
    }
    $toolchain = Join-Path $root 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path $toolchain)) {
        Write-Missing $name "no vcpkg was found in $root. $install"
        return
    }
    if (-not (Test-Path (Join-Path $root 'vcpkg.exe'))) {
        Write-Missing $name "vcpkg.exe is not in $root yet. Run bootstrap-vcpkg.bat in that folder once."
        return
    }
    Write-Found $name "$root, toolchain file $toolchain"
}

# Looks for Git on PATH and reads its version.
function Test-Git {
    $name = 'Git'
    $git = Get-Command git.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $git) {
        Write-Missing $name 'Install Git for Windows from https://git-scm.com and open a new terminal.'
        return
    }
    $version = (& $git.Source --version | Select-Object -First 1) -replace '^git version\s*', ''
    Write-Found $name "$version at $($git.Source)"
}

Write-Host 'Plynth setup check'
$vsPath = Test-VisualStudio
Test-CMake $vsPath
Test-Ninja $vsPath
Test-Qt
Test-Vcpkg
Test-Git

if ($script:missing -gt 0) {
    Write-Host "$($script:missing) required item(s) missing. Fix them, open a new terminal and run this check again." -ForegroundColor Red
    exit 1
}
Write-Host 'Everything needed to build Plynth was found.'
exit 0
