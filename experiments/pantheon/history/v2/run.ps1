param(
    [ValidateSet('rotunda', 'dome', 'lapis', 'mirror', 'aes', 'silva', 'astra', 'materials', 'city', 'gallery', 'dragon')]
    [string]$View = 'rotunda',
    [switch]$Benchmark,
    [switch]$Portals
)
$ErrorActionPreference = 'Stop'
$viewAliases = @{ materials='lapis'; city='aes'; gallery='silva'; dragon='astra' }
if ($viewAliases.ContainsKey($View)) { $View = $viewAliases[$View] }
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$viewerPath = Join-Path $repoPath 'build/windows/bin/viewer.exe'
if (-not (Test-Path -LiteralPath $viewerPath)) {
    throw 'Build the viewer first; see the repository README.'
}
$scenePath = Join-Path $PSScriptRoot ('scene.xml')
if ($View -ne 'rotunda') { $scenePath = Join-Path $PSScriptRoot ('views/' + $View + '.xml') }
if ($Portals) {
    if ($View -ne 'rotunda') { throw 'Use -Portals with the rotunda view; use verify.py --audit-portals for all audit cameras.' }
    $scenePath = Join-Path $PSScriptRoot 'portal-scene.xml'
}
$viewerArgs = @('--root', $repoPath, '--scene', $scenePath, '--shading', 'cook', '--aa', 'fxaa', '--reflections', 'environment', '--no-menus')
if ($Benchmark) {
    $outputPath = Join-Path $PSScriptRoot 'output'
    New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
    $viewerArgs += @('--hidden', '--no-vsync', '--frames', '600', '--benchmark', (Join-Path $outputPath ($View + '.csv')))
}
& $viewerPath @viewerArgs | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Viewer failed: $LASTEXITCODE" }
