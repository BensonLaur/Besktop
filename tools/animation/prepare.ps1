[CmdletBinding()]
param([switch]$SkipBlend, [switch]$BuildPreview)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$workspace = Join-Path $repository 'out/animation-foundation'
$downloads = Join-Path $workspace 'downloads'
$toolsDirectory = Join-Path $workspace 'tools'
$assets = Join-Path $workspace 'assets'
$dependencies = Get-Content (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force $downloads,$toolsDirectory,$assets | Out-Null

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Get-VerifiedFile($Entry) {
    $target = Join-Path $downloads $Entry.file
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "Downloading $($Entry.file) into the project-local cache..."
        Invoke-Checked 'curl.exe' @('--fail','--location','--retry','2','--max-time','900','--output',$target,$Entry.url)
    }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($actual -ne $Entry.sha256) { throw "Checksum mismatch for $target. Kept the file for inspection; no execution." }
    return $target
}
foreach ($entry in @($dependencies.blender,$dependencies.ozz)) {
    $archive = Get-VerifiedFile $entry
    $destination = Join-Path $toolsDirectory $entry.directory
    if (-not (Test-Path -LiteralPath $destination)) { Expand-Archive -LiteralPath $archive -DestinationPath $toolsDirectory }
}
foreach ($motion in $dependencies.motions) { $null = Get-VerifiedFile $motion }
$blender = Join-Path $toolsDirectory "$($dependencies.blender.directory)/blender.exe"
$ozz = Join-Path $toolsDirectory $dependencies.ozz.directory
$python = Join-Path $toolsDirectory "$($dependencies.blender.directory)/4.5/python/bin/python.exe"
Invoke-Checked $python @((Join-Path $PSScriptRoot 'test_anthropometry.py'))
Invoke-Checked $python @((Join-Path $PSScriptRoot 'anthropometry.py'))
$toolBuild = Join-Path $repository 'build-animation-tools-portable'
Invoke-Checked 'cmake' @('-S',$ozz,'-B',$toolBuild,'-G','Visual Studio 17 2022','-A','x64','-Dozz_build_tools=ON','-Dozz_build_gltf=ON','-Dozz_build_fbx=OFF','-Dozz_build_samples=OFF','-Dozz_build_howtos=OFF','-Dozz_build_tests=OFF','-Dozz_build_postfix=OFF')
Invoke-Checked 'cmake' @('--build',$toolBuild,'--config','Release','--target','gltf2ozz','-j','6')
$arguments = @('--background','--factory-startup','--python',(Join-Path $PSScriptRoot 'build_assets.py'),'--','--source',$downloads,'--output',$assets)
if ($SkipBlend) { $arguments += '--skip-blend' }
Invoke-Checked $blender $arguments
Push-Location $assets
try {
    Invoke-Checked (Join-Path $toolBuild 'src/animation/offline/gltf/gltf2ozz.exe') @('--file=foundation.gltf',"--config_file=$(Join-Path $PSScriptRoot 'ozz-import.json')")
} finally { Pop-Location }
# Blender's bundled Python keeps asset packaging reproducible without changing system Python.
Invoke-Checked $python @((Join-Path $PSScriptRoot 'package_assets.py'),$assets)
if ($BuildPreview) {
    $build = Join-Path $repository 'build-animation-foundation'
    Invoke-Checked 'cmake' @('-S',$repository,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-DBESKTOP_BUILD_ANIMATION_LAB=ON',"-DBESKTOP_OZZ_SOURCE_DIR=$ozz","-DBESKTOP_ANIMATION_ASSETS=$assets")
    Invoke-Checked 'cmake' @('--build',$build,'--config','Release','-j','6')
    Invoke-Checked 'ctest' @('--test-dir',$build,'-C','Release','--output-on-failure')
}
Write-Host "Ready: $assets"
