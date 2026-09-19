@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\preview-settings.ps1" %*
if errorlevel 1 pause
