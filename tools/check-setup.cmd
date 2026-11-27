@echo off
rem Checks that the tools needed to build Plynth are installed. It only looks and changes nothing.
rem The execution policy is bypassed for this one run only, so no PowerShell setting is changed.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-setup.ps1" %*
exit /b %ERRORLEVEL%
