@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-TypxBoard.ps1"
exit /b %ERRORLEVEL%
