[CmdletBinding()]
param(
    [ValidateSet("x64", "Win32", "All")]
    [string]$Architecture = "All",
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$Generator = "NMake Makefiles",
    [switch]$SkipHashFile
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
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

function Invoke-VcVarsChecked {
    param(
        [Parameter(Mandatory)] [string]$VcVarsPath,
        [Parameter(Mandatory)] [string]$VcArchitecture,
        [Parameter(Mandatory)] [string]$Command,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Arguments
    )

    $quotedArguments = $Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
    $commandLine = 'set CL=&& set _CL_=&& call "{0}" {1} >nul && "{2}" {3}' -f $VcVarsPath, $VcArchitecture, $Command, ($quotedArguments -join ' ')
    & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed in the $VcArchitecture MSVC environment with exit code $LASTEXITCODE."
    }
}

function Get-PeMachine {
    param([Parameter(Mandatory)] [string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        switch ($reader.ReadUInt16()) {
            0x8664 { return "x64" }
            0x014C { return "x86" }
            default { throw "Unsupported PE machine in ${Path}." }
        }
    } finally {
        $stream.Dispose()
    }
}

if (-not ("Besktop.Release.NativeMethods" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace Besktop.Release {
    public static class NativeMethods {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
        [DllImport("kernel32.dll")]
        public static extern bool FreeLibrary(IntPtr module);
    }
}
"@
}

function Assert-AppIconResource {
    param([Parameter(Mandatory)] [string]$Path)

    $module = [Besktop.Release.NativeMethods]::LoadLibraryEx($Path, [IntPtr]::Zero, 0x00000002)
    if ($module -eq [IntPtr]::Zero) { throw "Unable to inspect resources: $Path" }
    try {
        $resource = [Besktop.Release.NativeMethods]::FindResource($module, [IntPtr]1001, [IntPtr]14)
        if ($resource -eq [IntPtr]::Zero) { throw "Application icon resource ID 1001 is missing: $Path" }
    } finally {
        [void][Besktop.Release.NativeMethods]::FreeLibrary($module)
    }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
$installationPath = $null
$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswherePath -PathType Leaf) {
    $installationPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ($null -eq $dumpbin) {
    if (-not [string]::IsNullOrWhiteSpace($installationPath)) {
            $dumpbinPath = Get-ChildItem -LiteralPath (Join-Path $installationPath "VC\Tools\MSVC") -Directory |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Select-Object -First 1
            if ($null -ne $dumpbinPath) { $dumpbin = Get-Item -LiteralPath $dumpbinPath }
    }
}
if ($null -eq $cmake) { throw "cmake was not found in PATH." }
if ($null -eq $dumpbin) { throw "dumpbin was not found. Install the Visual Studio C++ toolset or run from Developer PowerShell." }
$dumpbinCommand = if ($null -ne $dumpbin.PSObject.Properties["Source"]) { $dumpbin.Source } else { $dumpbin.FullName }
if ($Generator -notmatch "NMake") { throw "The release script currently supports the NMake Makefiles generator only." }
if ([string]::IsNullOrWhiteSpace($installationPath)) { throw "Visual Studio with the x86/x64 C++ toolset was not found." }
$vcVarsPath = Join-Path $installationPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path -LiteralPath $vcVarsPath -PathType Leaf)) { throw "vcvarsall.bat was not found: $vcVarsPath" }

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$architectures = if ($Architecture -eq "All") { @("x64", "Win32") } else { @($Architecture) }
$managedNames = @("Besktop.exe", "Besktop-win32.exe", "SHA256SUMS.txt")
foreach ($name in $managedNames) {
    Remove-Item -LiteralPath (Join-Path $OutputDirectory $name) -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $OutputDirectory -File -Filter "SHA256SUMS*.txt" -ErrorAction SilentlyContinue |
    Remove-Item -Force

$results = @()
foreach ($currentArchitecture in $architectures) {
    $suffix = $currentArchitecture.ToLowerInvariant()
    $currentBuildDirectory = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
        Join-Path $repositoryRoot "build-release-package-$suffix"
    } elseif ($architectures.Count -gt 1) {
        "$([System.IO.Path]::GetFullPath($BuildDirectory))-$suffix"
    } else {
        [System.IO.Path]::GetFullPath($BuildDirectory)
    }
    $currentBuildDirectory = [System.IO.Path]::GetFullPath($currentBuildDirectory)

    $vcArchitecture = if ($currentArchitecture -eq "x64") { "amd64" } else { "x86" }
    Invoke-VcVarsChecked $vcVarsPath $vcArchitecture $cmake.Source @(
        "-S", $repositoryRoot,
        "-B", $currentBuildDirectory,
        "-G", $Generator,
        "-DCMAKE_BUILD_TYPE=Release"
    )
    Invoke-VcVarsChecked $vcVarsPath $vcArchitecture $cmake.Source @(
        "--build", $currentBuildDirectory,
        "--target", "besktop", "besktop_mvp_cli",
        "--clean-first"
    )

    $cliPath = Join-Path $currentBuildDirectory "besktop_mvp_cli.exe"
    $guiPath = Join-Path $currentBuildDirectory "besktop.exe"
    if (-not (Test-Path -LiteralPath $cliPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $guiPath -PathType Leaf)) {
        throw "Build completed, but the Release GUI or Pack CLI was not found for $currentArchitecture."
    }
    Invoke-Checked $cliPath @()

    $fileName = if ($currentArchitecture -eq "x64") { "Besktop.exe" } else { "Besktop-win32.exe" }
    $expectedMachine = if ($currentArchitecture -eq "x64") { "x64" } else { "x86" }
    $destination = Join-Path $OutputDirectory $fileName
    Copy-Item -LiteralPath $guiPath -Destination $destination -Force
    $artifact = Get-Item -LiteralPath $destination
    if ($artifact.Length -le 0 -or $artifact.Name -cne $fileName) {
        throw "Release artifact is empty or has an incorrect filename: $destination"
    }
    $machine = Get-PeMachine $destination
    if ($machine -ne $expectedMachine) {
        throw "PE architecture mismatch for ${fileName}: expected $expectedMachine, found $machine."
    }

    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($destination)
    if ($version.FileVersion -ne "0.1.0.0" -or $version.ProductVersion -ne "0.1.0") {
        throw "Unexpected version for ${fileName}: FileVersion=$($version.FileVersion), ProductVersion=$($version.ProductVersion)"
    }
    Assert-AppIconResource $destination

    $dependencyOutput = & $dumpbinCommand /nologo /dependents $destination 2>&1
    if ($LASTEXITCODE -ne 0) { throw "dumpbin dependency check failed for $fileName." }
    $dependencyText = $dependencyOutput -join [Environment]::NewLine
    if ($dependencyText -match '(?im)^\s*(VCRUNTIME|MSVCP|CONCRT)[^\s]*\.dll\s*$') {
        throw "${fileName} depends on an external MSVC runtime:`n$dependencyText"
    }

    $hash = Get-FileHash -LiteralPath $destination -Algorithm SHA256
    $results += [pscustomobject]@{
        Architecture = $machine
        Path = $destination
        Name = $fileName
        Size = $artifact.Length
        Hash = $hash.Hash.ToLowerInvariant()
    }
}

$unexpectedDlls = @(Get-ChildItem -LiteralPath $OutputDirectory -File -Filter *.dll)
if ($unexpectedDlls.Count -gt 0) {
    throw "Output directory contains unexpected DLL files: $($unexpectedDlls.Name -join ', ')"
}
if (-not $SkipHashFile) {
    $hashLines = $results | ForEach-Object { "$($_.Hash) *$($_.Name)" }
    Set-Content -LiteralPath (Join-Path $OutputDirectory "SHA256SUMS.txt") -Value $hashLines -Encoding ascii
}

Write-Host ""
Write-Host "Besktop Release build completed"
foreach ($result in $results) {
    Write-Host "[$($result.Architecture)] $($result.Path)"
    Write-Host "  Size: $($result.Size) bytes"
    Write-Host "  SHA-256: $($result.Hash)"
}
Write-Host "Versions, Pack CLI, icon resources, PE architectures and static runtime dependencies verified."
