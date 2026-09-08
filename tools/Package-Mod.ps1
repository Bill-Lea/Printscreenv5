<#
.SYNOPSIS
    Assemble the Nexus Mods release archive (dist\Printscreen.7z).

.DESCRIPTION
    Builds a Mod Organizer / Vortex compatible archive from:
      - the compiled SKSE plugin (build\bin\Printscreen.dll by default)
      - compiled Papyrus scripts   (Papyrus Scripts\Compiled\*.pex)
      - Papyrus sources            (Papyrus Scripts\*.psc -> Scripts\Source)
      - the mod payload            (mod\ : esp, Interface textures, JSON defaults, docs)
      - license files              (LICENSE, LICENSE-CommonLibSSE-NG-EXCEPTIONS.md, LICENSES.md)
      - SOURCE.txt                 (generated: where the Corresponding Source lives)

    The license files and SOURCE.txt are what GPL-3.0 section 6 requires to
    accompany the binary. Do not remove them from the archive.

.PARAMETER Dll
    Path to Printscreen.dll. Defaults to build\bin\Printscreen.dll.

.PARAMETER OutDir
    Output directory. Defaults to dist\ under the repository root.

.PARAMETER Ref
    Git ref that the DLL was built from, recorded in SOURCE.txt. Defaults to HEAD.

.EXAMPLE
    .\tools\Package-Mod.ps1
    .\tools\Package-Mod.ps1 -Dll C:\path\to\Printscreen.dll -Ref v5.0.0
#>
[CmdletBinding()]
param(
    [string]$Dll,
    [string]$OutDir,
    [string]$Ref = "HEAD"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Dll)    { $Dll    = Join-Path $RepoRoot "build\bin\Printscreen.dll" }
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "dist" }

. (Join-Path $PSScriptRoot "Common.ps1")

$SevenZip = Find-SevenZip
$Version  = Get-ProjectVersion $RepoRoot
$Commit   = (& git -C $RepoRoot rev-parse $Ref).Trim()
$Dirty    = (& git -C $RepoRoot status --porcelain --untracked-files=no)
if ($Dirty) {
    Write-Warning "Working tree has uncommitted changes. SOURCE.txt will record commit $Commit, which may not match the DLL you built."
}

# --- Inputs -----------------------------------------------------------------
if (-not (Test-Path $Dll)) { throw "DLL not found: $Dll  (build first, or pass -Dll)" }

$PapyrusDir  = Join-Path $RepoRoot "Papyrus Scripts"
$CompiledDir = Join-Path $PapyrusDir "Compiled"
$PscFiles    = Get-ChildItem -Path $PapyrusDir -Filter *.psc -File
if ($PscFiles.Count -eq 0) { throw "No .psc files found in $PapyrusDir" }
$MissingPex = @()
foreach ($psc in $PscFiles) {
    $pex = Join-Path $CompiledDir ($psc.BaseName + ".pex")
    if (-not (Test-Path $pex)) { $MissingPex += $psc.BaseName }
}
if ($MissingPex.Count -gt 0) {
    throw "Compiled .pex missing for: $($MissingPex -join ', ').  Compile the Papyrus scripts into '$CompiledDir' first (see BUILD.md)."
}

$LicenseFiles = @("LICENSE", "LICENSE-CommonLibSSE-NG-EXCEPTIONS.md", "LICENSES.md")
foreach ($f in $LicenseFiles) {
    if (-not (Test-Path (Join-Path $RepoRoot $f))) { throw "Required license file missing: $f" }
}

# --- Stage ------------------------------------------------------------------
$Stage = Join-Path $OutDir "stage-mod"
if (Test-Path $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

# mod payload (esp, Interface, SKSE\Plugins\StorageUtilData, Documentation)
$ModDir = Join-Path $RepoRoot "mod"
if (-not (Test-Path $ModDir)) { throw "mod\ payload directory not found" }
Copy-Tree -Source $ModDir -Destination $Stage -ExcludeDirs @("__pycache__") -ExcludeFiles @("*.py", "*.pyc")

# plugin
$PluginDir = Join-Path $Stage "SKSE\Plugins"
New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $PluginDir "Printscreen.dll")

# scripts
$ScriptsDir = Join-Path $Stage "Scripts"
$SourceDir  = Join-Path $ScriptsDir "Source"
New-Item -ItemType Directory -Force -Path $SourceDir | Out-Null
foreach ($psc in $PscFiles) {
    Copy-Item -LiteralPath $psc.FullName -Destination $SourceDir
    Copy-Item -LiteralPath (Join-Path $CompiledDir ($psc.BaseName + ".pex")) -Destination $ScriptsDir
}

# licenses
foreach ($f in $LicenseFiles) {
    Copy-Item -LiteralPath (Join-Path $RepoRoot $f) -Destination $Stage
}

# SOURCE.txt
$SourceTxt = @"
PrintScreen V5 $Version -- Corresponding Source
================================================

Printscreen.dll in this archive is free software licensed under the
GNU General Public License v3.0 or later. See LICENSE, LICENSES.md and
LICENSE-CommonLibSSE-NG-EXCEPTIONS.md in this archive.

Source code for this exact build:

  Repository : https://github.com/Bill-Lea/Printscreenv5
  Tag        : v$Version
  Commit     : $Commit

A self-contained bundle of this repository plus the source of every
statically linked dependency (CommonLibSSE-NG, hde64, and all vcpkg ports)
is attached to the matching GitHub Release as

  Printscreen-$Version-corresponding-source.7z

Build instructions are in BUILD.md inside the repository and the bundle.
"@
Set-Content -LiteralPath (Join-Path $Stage "SOURCE.txt") -Value $SourceTxt -Encoding UTF8

# --- Archive ----------------------------------------------------------------
$Archive = Join-Path $OutDir "Printscreen.7z"
if (Test-Path $Archive) { Remove-Item -LiteralPath $Archive -Force }
Push-Location $Stage
try {
    & $SevenZip a -t7z -mx=9 -ms=on $Archive * | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
Remove-Item -LiteralPath $Stage -Recurse -Force

Write-Host ""
Write-Host "Created $Archive" -ForegroundColor Green
Write-Host "  version $Version, commit $Commit" -ForegroundColor Green
& $SevenZip l $Archive | Select-String -Pattern '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} [D.]\.{3}[A.] ' | ForEach-Object { $_.Line.Substring(53) } | Sort-Object | ForEach-Object { Write-Host "  $_" }
