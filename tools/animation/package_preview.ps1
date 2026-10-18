[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$destination = Join-Path $repository 'dist/animation-foundation-preview'
$preview = Join-Path $repository 'out/animation-foundation/preview'
$assets = Join-Path $repository 'out/animation-foundation/assets'
New-Item -ItemType Directory -Force $destination,(Join-Path $destination 'preview') | Out-Null
$files = @{
    'build-animation-foundation/Release/AnimationLab.exe' = 'AnimationLab.exe'
    'build-animation-foundation/Release/besktop.exe' = 'Besktop.exe'
    'tools/animation/Open-3D-Stage.cmd' = 'Open-3D-Stage.cmd'
    'tools/animation/PREVIEW_README.md' = 'README.md'
    'docs/animation/IMPLEMENTATION_REPORT.md' = 'IMPLEMENTATION_REPORT.md'
    'docs/animation/WORKFLOW.md' = 'WORKFLOW.md'
    'docs/animation/ANTHROPOMETRY.md' = 'ANTHROPOMETRY.md'
    'docs/animation/THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'LICENSE' = 'LICENSE'
    'out/animation-foundation/assets/G1-stick-figure.blend' = 'G1-stick-figure.blend'
    'out/animation-foundation/preview/G1-G5-preview.mp4' = 'G1-G5-preview.mp4'
    'build-animation-foundation/Testing/Temporary/LastTest.log' = 'preview/ctest-animation.txt'
    'build-animation-baseline-check/Testing/Temporary/LastTest.log' = 'preview/ctest-baseline.txt'
}
foreach ($entry in $files.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $repository $entry.Key) -Destination (Join-Path $destination $entry.Value) -Force
}
Get-ChildItem -LiteralPath $preview -File | Where-Object { $_.Extension -in '.png','.csv','.txt' -and $_.Name -ne 'preview-error.txt' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destination 'preview') -Force
}
Get-ChildItem -LiteralPath $assets -File -Filter 'G1-*.png' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destination 'preview') -Force
}
foreach ($name in @('blender-validation.json','provenance.json','skeleton.json','manifest.json','anthropometry-audit.json')) {
    Copy-Item -LiteralPath (Join-Path $assets $name) -Destination (Join-Path $destination "preview/$name") -Force
}
$hashes = Get-ChildItem -LiteralPath $destination -File -Recurse | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($destination.Length + 1).Replace('\','/')
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()) *$relative"
}
[IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), $hashes, [Text.Encoding]::ASCII)
Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip" -CompressionLevel Optimal -Force
Write-Host "Local preview ready: $destination"
Write-Host 'No tags, commits, pushes or releases were created.'
