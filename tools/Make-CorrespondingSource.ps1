<#
.SYNOPSIS
    Assemble the GPL "Corresponding Source" bundle for a Printscreen.dll release.

.DESCRIPTION
    Produces dist\Printscreen-<version>-corresponding-source.7z containing
    everything needed to rebuild the released DLL offline:

      printscreen/              this repository at -Ref (git archive)
      deps/CommonLibSSE-NG/     the CommonLibSSE-NG tree CMake fetched (build\_deps\commonlibsse-src)
      deps/hde64-minhook/       the MinHook tree CMake fetched for hde64  (build\_deps\hde64-src)
      deps/vcpkg-sources/       the exact source tarballs vcpkg downloaded for every port
      deps/vcpkg-ports/         the vcpkg port recipes (portfile.cmake, patches) at the pinned baseline, when available
      deps/VCPKG-BASELINE.txt   baseline commit and installed port versions
      BUILD.md, LICENSE*, LICENSES.md, MANIFEST.txt (SHA-256 of every file)

    Run this after a successful `cmake --preset release` so that build\_deps
    and build\vcpkg_installed reflect the release build. Upload the result to
    the GitHub Release next to the binary.

.PARAMETER Ref
    Git ref to archive. Defaults to HEAD. Use the release tag (v5.0.0).

.PARAMETER BuildDir
    CMake build directory. Defaults to build\.

.PARAMETER VcpkgDownloads
    vcpkg downloads cache. Defaults to %LOCALAPPDATA%\vcpkg\downloads, which is
    where the Visual Studio bundled vcpkg stores fetched sources.

.PARAMETER OutDir
    Output directory. Defaults to dist\.
#>
[CmdletBinding()]
param(
    [string]$Ref = "HEAD",
    [string]$BuildDir,
    [string]$VcpkgDownloads,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir)       { $BuildDir       = Join-Path $RepoRoot "build" }
if (-not $OutDir)         { $OutDir         = Join-Path $RepoRoot "dist" }
if (-not $VcpkgDownloads) { $VcpkgDownloads = Join-Path $env:LOCALAPPDATA "vcpkg\downloads" }

. (Join-Path $PSScriptRoot "Common.ps1")

$SevenZip = Find-SevenZip
$Version  = Get-ProjectVersion $RepoRoot
$Commit   = (& git -C $RepoRoot rev-parse "$Ref^{commit}").Trim()
if (& git -C $RepoRoot status --porcelain --untracked-files=no) {
    Write-Warning "Working tree has uncommitted changes; the bundle will contain commit $Commit as committed, not your working copy."
}

# Expected vcpkg download file names for the pinned baseline. Port version is
# read from build\vcpkg_installed\vcpkg\info so a mismatch is reported rather
# than silently bundling the wrong tarball.
$VcpkgSources = @{
    "directxmath"   = @{ Version = "2025-04-03"; Files = @("Microsoft-DirectXMath-apr2025.tar.gz") }
    "directxtex"    = @{ Version = "2025-10-27"; Files = @("Microsoft-DirectXTex-oct2025.tar.gz") }
    "directxtk"     = @{ Version = "2025-10-27"; Files = @("Microsoft-DirectXTK-oct2025.tar.gz") }
    "fmt"           = @{ Version = "12.1.0";     Files = @("fmtlib-fmt-12.1.0.tar.gz") }
    "libpng"        = @{ Version = "1.6.53";     Files = @("pnggroup-libpng-v1.6.53.tar.gz", "libpng-1.6.53-apng.patch.gz") }
    "zlib"          = @{ Version = "1.3.1";      Files = @("madler-zlib-v1.3.1.tar.gz") }
    "lodepng"       = @{ Version = "2021-12-04"; Files = @("lvandeve-lodepng-8c6a9e30576f07bf470ad6f09458a2dcd7a6a84a.tar.gz") }
    "nlohmann-json" = @{ Version = "3.12.0";     Files = @("nlohmann-json-v3.12.0.tar.gz") }
    "rapidcsv"      = @{ Version = "8.90";       Files = @("d99kris-rapidcsv-v8.90.tar.gz") }
    "simpleini"     = @{ Version = "4.25";       Files = @("brofield-simpleini-v4.25.tar.gz") }
    "spdlog"        = @{ Version = "1.16.0";     Files = @("gabime-spdlog-v1.16.0.tar.gz") }
    "toml11"        = @{ Version = "4.4.0";      Files = @("ToruNiina-toml11-v4.4.0.tar.gz") }
    "xbyak"         = @{ Version = "7.28";       Files = @("herumi-xbyak-v7.28.tar.gz") }
}

# --- Validate inputs --------------------------------------------------------
$CommonLibSrc = Join-Path $BuildDir "_deps\commonlibsse-src"
$Hde64Src     = Join-Path $BuildDir "_deps\hde64-src"
$InfoDir      = Join-Path $BuildDir "vcpkg_installed\vcpkg\info"
foreach ($p in @($CommonLibSrc, $Hde64Src, $InfoDir)) {
    if (-not (Test-Path $p)) { throw "Missing $p -- run 'cmake --preset release' first." }
}
if (-not (Test-Path $VcpkgDownloads)) { throw "vcpkg downloads cache not found at $VcpkgDownloads (pass -VcpkgDownloads)" }

$Installed = @{}
Get-ChildItem -Path $InfoDir -Filter *.list | ForEach-Object {
    if ($_.BaseName -match '^(?<port>[a-z0-9\-]+)_(?<ver>[^_]+)_') { $Installed[$Matches.port] = $Matches.ver }
}
$Problems = @()
foreach ($port in $VcpkgSources.Keys) {
    $want = $VcpkgSources[$port].Version
    $have = $Installed[$port]
    if (-not $have)          { $Problems += "$port is not installed in $InfoDir" }
    elseif ($have -ne $want) { $Problems += "$port installed version $have differs from expected $want -- update `$VcpkgSources in this script" }
    foreach ($f in $VcpkgSources[$port].Files) {
        if (-not (Test-Path (Join-Path $VcpkgDownloads $f))) { $Problems += "$port source tarball missing: $f" }
    }
}
foreach ($port in $Installed.Keys) {
    if ($port -like "vcpkg-cmake*") { continue }
    if (-not $VcpkgSources.ContainsKey($port)) { $Problems += "$port is installed but not listed in `$VcpkgSources -- add it" }
}
if ($Problems.Count -gt 0) { throw ("Cannot build a complete bundle:`n  " + ($Problems -join "`n  ")) }

$Baseline = (Get-Content -LiteralPath (Join-Path $RepoRoot "vcpkg-configuration.json") -Raw | ConvertFrom-Json).'default-registry'.baseline

# --- Stage ------------------------------------------------------------------
$Stage = Join-Path $OutDir "stage-source"
if (Test-Path $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

Write-Host "Archiving repository at $Ref ($Commit)..."
$RepoZip = Join-Path $Stage "repo.zip"
& git -C $RepoRoot archive --format=zip --output=$RepoZip $Ref
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
Expand-Archive -LiteralPath $RepoZip -DestinationPath (Join-Path $Stage "printscreen")
Remove-Item -LiteralPath $RepoZip

Write-Host "Copying CommonLibSSE-NG source..."
# The OpenVR submodule ships ~250 MB of prebuilt binaries and libraries, Unity sample scenes
# and SDK samples. None of it is source, and OpenVR is not compiled into this
# build (ENABLE_SKYRIM_VR=OFF), so drop those subtrees but keep OpenVR's headers.
$OpenVrBloat = @("bin", "lib", "samples", "controller_callouts") | ForEach-Object { Join-Path $CommonLibSrc "extern\openvr\$_" }
Copy-Tree -Source $CommonLibSrc -Destination (Join-Path $Stage "deps\CommonLibSSE-NG") -ExcludeDirs (@(".git") + $OpenVrBloat)
Write-Host "Copying hde64 (MinHook) source..."
Copy-Tree -Source $Hde64Src -Destination (Join-Path $Stage "deps\hde64-minhook") -ExcludeDirs @(".git")

Write-Host "Copying vcpkg source tarballs..."
$TarballDir = Join-Path $Stage "deps\vcpkg-sources"
New-Item -ItemType Directory -Force -Path $TarballDir | Out-Null
foreach ($port in ($VcpkgSources.Keys | Sort-Object)) {
    foreach ($f in $VcpkgSources[$port].Files) {
        Copy-Item -LiteralPath (Join-Path $VcpkgDownloads $f) -Destination $TarballDir
    }
}

# vcpkg port recipes at the baseline (portfile.cmake + patches). The VS-bundled
# vcpkg keeps a clone of the registry under %LOCALAPPDATA%\vcpkg\registries\git.
$PortsDir  = Join-Path $Stage "deps\vcpkg-ports"
$PortsNote = "not included"
$RegistryGit = $null
foreach ($candidate in @((Join-Path $env:LOCALAPPDATA "vcpkg\registries\git"), $env:VCPKG_ROOT)) {
    if (-not $candidate -or -not (Test-Path $candidate)) { continue }
    & git -C $candidate cat-file -e "$Baseline^{commit}" 2>$null
    if ($LASTEXITCODE -eq 0) { $RegistryGit = Get-Item $candidate; break }
}
if ($RegistryGit) {
    $ok = $true
    New-Item -ItemType Directory -Force -Path $PortsDir | Out-Null
    foreach ($port in ($VcpkgSources.Keys | Sort-Object)) {
        $zip = Join-Path $Stage "port.zip"
        & git -C $RegistryGit.FullName archive --format=zip --output=$zip $Baseline "ports/$port" 2>$null
        if ($LASTEXITCODE -eq 0 -and (Test-Path $zip)) {
            Expand-Archive -LiteralPath $zip -DestinationPath $PortsDir
            Remove-Item -LiteralPath $zip
        } else { $ok = $false; break }
    }
    if ($ok) { $PortsNote = "deps/vcpkg-ports/ports/<port> (exported from registry commit $Baseline)" }
    else {
        Write-Warning "Could not export vcpkg port recipes for baseline $Baseline from $($RegistryGit.FullName); bundle will reference them by commit only."
        Remove-Item -LiteralPath $PortsDir -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Warning "No vcpkg registry git repository found; port recipes will be referenced by baseline commit only."
}

# Baseline / versions record
$lines = @(
    "PrintScreen V5 $Version -- vcpkg dependency record",
    "",
    "Repository commit : $Commit  (ref $Ref)",
    "vcpkg registry    : https://github.com/microsoft/vcpkg.git",
    "Registry baseline : $Baseline",
    "Triplet           : x64-windows-static-md",
    "Port recipes      : $PortsNote",
    "",
    "Installed ports (from build/vcpkg_installed/vcpkg/info):",
    ""
)
foreach ($port in ($VcpkgSources.Keys | Sort-Object)) {
    $lines += ("  {0,-14} {1,-12} {2}" -f $port, $VcpkgSources[$port].Version, ($VcpkgSources[$port].Files -join ", "))
}
$lines += ""
$lines += "CommonLibSSE-NG : https://github.com/alandtse/CommonLibSSE-NG tag v6.4.0, commit e7863a71523a2896c92ea9d3105c0d121dcdba0d (deps/CommonLibSSE-NG)"
$lines += "hde64           : https://github.com/TsudaKageyu/minhook tag v1.3.4, src/hde only (deps/hde64-minhook)"
$lines += ""
$lines += "deps/CommonLibSSE-NG/extern/openvr/{bin,samples,controller_callouts} are omitted: prebuilt binaries and"
$lines += "SDK samples, not source, and OpenVR is not compiled into this build (ENABLE_SKYRIM_VR=OFF)."
$lines += ""
$lines += "To build offline, see BUILD.md: place deps/vcpkg-sources/* in your vcpkg downloads"
$lines += "directory and point FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE / FETCHCONTENT_SOURCE_DIR_HDE64 at deps/."
Set-Content -LiteralPath (Join-Path $Stage "deps\VCPKG-BASELINE.txt") -Value ($lines -join "`r`n") -Encoding UTF8

foreach ($f in @("BUILD.md", "LICENSE", "LICENSE-CommonLibSSE-NG-EXCEPTIONS.md", "LICENSES.md", "README.md")) {
    $src = Join-Path $Stage "printscreen\$f"
    if (Test-Path $src) { Copy-Item -LiteralPath $src -Destination $Stage }
}

# Manifest
Write-Host "Hashing files..."
$manifest = Get-ChildItem -Path $Stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($Stage.Length + 1).Replace('\', '/')
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $rel
}
Set-Content -LiteralPath (Join-Path $Stage "MANIFEST.txt") -Value ($manifest -join "`n") -Encoding UTF8

# --- Archive ----------------------------------------------------------------
$Archive = Join-Path $OutDir "Printscreen-$Version-corresponding-source.7z"
if (Test-Path $Archive) { Remove-Item -LiteralPath $Archive -Force }
Push-Location $Stage
try {
    & $SevenZip a -t7z -mx=7 -ms=on $Archive * | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
Remove-Item -LiteralPath $Stage -Recurse -Force

$size = [math]::Round((Get-Item $Archive).Length / 1MB, 1)
Write-Host ""
Write-Host "Created $Archive ($size MB)" -ForegroundColor Green
Write-Host "  repository commit $Commit, vcpkg baseline $Baseline" -ForegroundColor Green
Write-Host "  Upload this to the GitHub Release for v$Version alongside Printscreen.7z." -ForegroundColor Green
