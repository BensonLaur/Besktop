[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$Generator = "NMake Makefiles",
    [switch]$SkipHashFile
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "build-release-package"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot "dist"
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$Command,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    throw "cmake was not found in PATH. Install CMake and run from Visual Studio Developer PowerShell."
}

if ($Generator -match "NMake" -and $null -eq (Get-Command cl -ErrorAction SilentlyContinue)) {
    throw "NMake/MSVC requires an initialized developer environment. Run from Visual Studio Developer PowerShell."
}

$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Invoke-Checked $cmake.Source @(
    "-S", $repositoryRoot,
    "-B", $BuildDirectory,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=Release"
)
Invoke-Checked $cmake.Source @(
    "--build", $BuildDirectory,
    "--config", "Release",
    "--target", "besktop", "besktop_mvp_cli",
    "--clean-first"
)

$cliCandidates = @(
    (Join-Path $BuildDirectory "besktop_mvp_cli.exe"),
    (Join-Path $BuildDirectory "Release\besktop_mvp_cli.exe")
)
$guiCandidates = @(
    (Join-Path $BuildDirectory "besktop.exe"),
    (Join-Path $BuildDirectory "Release\besktop.exe")
)
$cliPath = $cliCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$guiPath = $guiCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($null -eq $cliPath -or $null -eq $guiPath) {
    throw "Build completed, but besktop or besktop_mvp_cli was not found."
}

Invoke-Checked $cliPath @()

$destination = Join-Path $OutputDirectory "Besktop.exe"
Copy-Item -LiteralPath $guiPath -Destination $destination -Force
$artifact = Get-Item -LiteralPath $destination
if ($artifact.Length -le 0 -or $artifact.Name -cne "Besktop.exe") {
    throw "Release artifact is empty or has an incorrect filename: $destination"
}

$version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($destination)
if ($version.FileMajorPart -ne 0 -or $version.FileMinorPart -ne 1 -or
    $version.FileBuildPart -ne 0 -or $version.FilePrivatePart -ne 0) {
    throw "FileVersion is not 0.1.0.0: $($version.FileVersion)"
}
if ($version.ProductMajorPart -ne 0 -or $version.ProductMinorPart -ne 1 -or
    $version.ProductBuildPart -ne 0) {
    throw "ProductVersion does not represent 0.1.0: $($version.ProductVersion)"
}

$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($null -eq $dumpbin) {
    throw "dumpbin was not found in PATH. Run from Visual Studio Developer PowerShell."
}
$dependencyOutput = & $dumpbin.Source /nologo /dependents $destination 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin dependency check failed with exit code $LASTEXITCODE."
}
$dependencyText = $dependencyOutput -join [Environment]::NewLine
if ($dependencyText -match '(?im)^\s*(VCRUNTIME|MSVCP|CONCRT)[^\s]*\.dll\s*$') {
    throw "Release still depends on an external MSVC C/C++ runtime:`n$dependencyText"
}

$unexpectedDlls = @(Get-ChildItem -LiteralPath $OutputDirectory -File -Filter *.dll)
if ($unexpectedDlls.Count -gt 0) {
    throw "Output directory contains unexpected DLL files: $($unexpectedDlls.Name -join ', ')"
}

$hash = Get-FileHash -LiteralPath $destination -Algorithm SHA256
if (-not $SkipHashFile) {
    $hashLine = "$($hash.Hash.ToLowerInvariant()) *Besktop.exe"
    Set-Content -LiteralPath (Join-Path $OutputDirectory "SHA256SUMS.txt") -Value $hashLine -Encoding ascii
}

Write-Host ""
Write-Host "Besktop Release build completed"
Write-Host "Path: $destination"
Write-Host "Size: $($artifact.Length) bytes"
Write-Host "FileVersion: $($version.FileVersion)"
Write-Host "ProductVersion: $($version.ProductVersion)"
Write-Host "SHA-256: $($hash.Hash)"
Write-Host "Dependencies: no external MSVC C/C++ runtime DLL found"
