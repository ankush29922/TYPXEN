@echo off
setlocal
set "SCRIPT=%~dp0verify_typx_firmware.py"
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  py -3 "%SCRIPT%"
  exit /b %ERRORLEVEL%
)
where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  python "%SCRIPT%"
  exit /b %ERRORLEVEL%
)
echo FAIL: Python 3 is required.
exit /b 1
