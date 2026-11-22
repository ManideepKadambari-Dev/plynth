@echo off
rem Loads the Visual Studio C++ environment, then runs the given command.
rem Example: tools\msvc-env.cmd cmake --build --preset debug
if defined VSCMD_VER goto run
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
    echo msvc-env: Visual Studio with the C++ tools was not found 1>&2
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem The CMake bundled with Visual Studio is not on PATH outside its own prompt.
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
:run
%*
exit /b %ERRORLEVEL%
