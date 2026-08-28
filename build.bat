@echo off
REM PrintScreen SKSE Plugin Build Script
REM Usage: build.bat [Release|Debug] [clean]

setlocal

set CONFIG=Release
set CLEAN=

:parse_args
if "%~1"=="" goto run
if /i "%~1"=="debug" set CONFIG=Debug
if /i "%~1"=="release" set CONFIG=Release
if /i "%~1"=="clean" set CLEAN=-Clean
shift
goto parse_args

:run
echo === PrintScreen Build Script ===
echo Configuration: %CONFIG%

REM Locate PowerShell by full path -- do not rely on PATH, which may not
REM contain the WindowsPowerShell directory on this system.
set "PSEXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PSEXE%" set "PSEXE=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PSEXE%" where pwsh >nul 2>nul && set "PSEXE=pwsh"
if not exist "%PSEXE%" if /i not "%PSEXE%"=="pwsh" (
    echo ERROR: Could not locate powershell.exe or pwsh.
    pause
    exit /b 1
)

"%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Config %CONFIG% %CLEAN%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
pause
