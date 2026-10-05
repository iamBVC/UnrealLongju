@echo off
setlocal
rem The PowerShell validator lives beside this wrapper in Scripts.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ValidateLocalization.ps1" %*
exit /b %errorlevel%
