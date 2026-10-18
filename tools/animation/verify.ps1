[CmdletBinding()]
param([switch]$Sequences)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $repository 'build-animation-foundation'
$output = Join-Path $repository 'out/animation-foundation/preview'
New-Item -ItemType Directory -Force $output | Out-Null
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
& (Join-Path $build 'Release/besktop_mvp_cli.exe')
if ($LASTEXITCODE -ne 0) { throw 'Legacy Pack verification failed' }

function Invoke-Preview([string]$Name, [string[]]$Arguments, [string]$Marker) {
    $markerPath = Join-Path $output $Marker
    $started = [DateTime]::UtcNow
    # Explicit waiting matters for WIN32-subsystem executables; launch success is not completion.
    $process = Start-Process -FilePath (Join-Path $build "Release/$Name") -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Timed out running $Name; only this test process was stopped."
    }
    if ($process.ExitCode -ne 0) { throw "$Name failed; inspect $output/preview-error.txt" }
    if (-not (Test-Path -LiteralPath $markerPath) -or (Get-Item -LiteralPath $markerPath).LastWriteTimeUtc -lt $started) {
        throw "Missing fresh verification marker: $markerPath"
    }
    Get-Content -LiteralPath $markerPath
}
$quotedOutput = '"' + $output + '"'
Invoke-Preview 'AnimationLab.exe' @('--ui-self-test',$quotedOutput) 'ui-smoke-test.txt'
Invoke-Preview 'besktop.exe' @('--animation-stage','--synthetic-stage-test',$quotedOutput) 'stage-smoke-test.txt'
$renderArguments = @('--render-preview',$quotedOutput)
if ($Sequences) { $renderArguments += '--sequences' }
Invoke-Preview 'AnimationLab.exe' $renderArguments 'preview-complete.txt'
Get-Content (Join-Path $output 'render-benchmark.csv')
Write-Host 'Only synthetic/offscreen scenes were rendered. No real desktop snapshot was read.'
