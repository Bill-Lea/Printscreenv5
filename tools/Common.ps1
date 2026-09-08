# Shared helpers for the packaging scripts. Dot-source this file.

function Find-SevenZip {
    $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe")) {
        if (Test-Path $p) { return $p }
    }
    throw "7z.exe not found. Install 7-Zip or add it to PATH."
}

function Get-ProjectVersion {
    param([Parameter(Mandatory)][string]$RepoRoot)
    $manifest = Get-Content -LiteralPath (Join-Path $RepoRoot "vcpkg.json") -Raw | ConvertFrom-Json
    if (-not $manifest.'version-string') { throw "vcpkg.json has no version-string" }
    return [string]$manifest.'version-string'
}

# Recursive copy that skips named directories (e.g. .git) and file patterns.
# Uses robocopy so long paths and read-only attributes in fetched git trees
# don't trip Copy-Item.
function Copy-Tree {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [string[]]$ExcludeDirs = @(),
        [string[]]$ExcludeFiles = @()
    )
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $args = @($Source, $Destination, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NC", "/NS", "/NP", "/R:1", "/W:1")
    if ($ExcludeDirs.Count  -gt 0) { $args += "/XD"; $args += $ExcludeDirs }
    if ($ExcludeFiles.Count -gt 0) { $args += "/XF"; $args += $ExcludeFiles }
    & robocopy @args | Out-Null
    # robocopy exit codes 0-7 are success; 8+ are failures.
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE) copying $Source -> $Destination" }
    $global:LASTEXITCODE = 0
}
