# PrintScreen SKSE Plugin Build Script
# Usage: .\build.ps1 [-Config Release|Debug] [-Clean] [-Configure]

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Configure
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$VsInstallPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"

# --- PATH repair -----------------------------------------------------------
# The system PATH on this machine is missing core Windows directories
# (even C:\Windows\System32), which breaks cmd.exe, and in turn vcpkg and
# CMake, both of which spawn cmd internally. Prepend the essentials here so
# this script and every child process (cmake, ninja, vcpkg) inherit a sane
# PATH regardless of the machine's environment settings.
$CorePaths = @(
    "$env:SystemRoot\System32",
    "$env:SystemRoot",
    "$env:SystemRoot\System32\Wbem",
    "$env:SystemRoot\System32\WindowsPowerShell\v1.0"
)
foreach ($p in $CorePaths) {
    if ((Test-Path $p) -and ($env:PATH -notlike "*$p*")) {
        $env:PATH = "$p;$env:PATH"
    }
}
# ---------------------------------------------------------------------------

# Import VS Developer Shell
Import-Module "$VsInstallPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $VsInstallPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64"

# Ensure powershell.exe is on PATH — vcpkg's applocal.ps1 post-build step
# calls "powershell.exe" explicitly, which may not be found in the VS Dev
# Shell environment (only pwsh.exe or Windows PowerShell module is loaded).
$psPath = "$env:WINDIR\System32\WindowsPowerShell\v1.0"
if (Test-Path "$psPath\powershell.exe") {
    $env:PATH = "$psPath;$env:PATH"
}

Write-Host "=== PrintScreen Build Script ===" -ForegroundColor Cyan
Write-Host "Configuration: $Config" -ForegroundColor Yellow

# --- Stale cache detection -------------------------------------------------
# CMake refuses to build if CMakeCache.txt was generated in a different
# directory (e.g. the build tree was copied from another checkout such as
# the OpenClaw workspace). Detect this by reading the header comment that
# CMake writes into the cache, and force a clean rebuild on mismatch.
$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CacheFile) {
    $cachedDir = $null
    foreach ($line in (Get-Content $CacheFile -TotalCount 10)) {
        if ($line -match '^# For build in directory:\s*(.+)$') {
            $cachedDir = $Matches[1].Trim()
            break
        }
    }
    if ($cachedDir) {
        $normCached = ($cachedDir -replace '/', '\').TrimEnd('\')
        $normActual = ($BuildDir  -replace '/', '\').TrimEnd('\')
        if (-not $normCached.Equals($normActual, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Stale CMake cache detected:" -ForegroundColor Yellow
            Write-Host "  Cache was created for: $normCached" -ForegroundColor Yellow
            Write-Host "  Actual build dir is:   $normActual" -ForegroundColor Yellow
            Write-Host "Forcing clean rebuild..." -ForegroundColor Yellow
            $Clean = $true
        }
    }
}
# ---------------------------------------------------------------------------

# Clean build directory if requested
# Use cmd /c rmdir to avoid PowerShell Remove-Item failures on paths with
# special characters (@, parentheses) in vcpkg's cached build trees.
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) {
        & $env:ComSpec /c "rmdir /s /q `"$BuildDir`"" 2>$null
        if (Test-Path $BuildDir) {
            # Fallback: try Remove-Item with -LiteralPath
            Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path $BuildDir) {
            Write-Host "Warning: Could not fully remove build directory. Continuing anyway." -ForegroundColor Yellow
        }
    }
    $Configure = $true
}

# Run a native command with stderr safely merged into stdout.
# PowerShell 5.1 + $ErrorActionPreference=Stop turns any native stderr text
# (e.g. a harmless CMake warning) redirected via 2>&1 into a terminating
# NativeCommandError. Temporarily relax the preference and stringify each
# output record; real failures are detected via $LASTEXITCODE afterward.
function Invoke-Native {
    param([scriptblock]$Command)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Command 2>&1 | ForEach-Object { Write-Host "$_" }
    } finally {
        $ErrorActionPreference = $prev
    }
}

# Determine preset based on configuration
$PresetName = if ($Config -eq "Debug") { "debug" } else { "release" }

# Configure if needed or requested
if ($Configure -or -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "Configuring with CMake (preset: $PresetName)..." -ForegroundColor Yellow
    Invoke-Native { cmake -B $BuildDir -S $ProjectRoot --preset=$PresetName }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Configuration failed!" -ForegroundColor Red
        exit 1
    }
}

# Build
Write-Host "Building $Config..." -ForegroundColor Yellow
Invoke-Native { cmake --build $BuildDir --config $Config }

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=== Build Successful ===" -ForegroundColor Green
    $dll = Join-Path $BuildDir "bin\Printscreen.dll"
    if (Test-Path $dll) {
        $size = [math]::Round((Get-Item $dll).Length / 1KB, 1)
        Write-Host "Output: $dll ($size KB)" -ForegroundColor Green
    }
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
