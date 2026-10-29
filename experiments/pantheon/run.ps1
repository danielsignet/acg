param(
    [ValidateSet('rotunda', 'dome', 'lapis', 'mirror', 'aes', 'silva', 'astra', 'materials', 'city', 'gallery', 'dragon', 'gyroscopes', 'euler', 'axis-angle', 'quaternion', 'astra-exit', 'astra-sky', 'city-sky', 'astra-floor', 'astra-floor-gate', 'metal-close', 'city-gate-high')]
    [string]$View = 'rotunda',
    [ValidateSet('off', 'environment', 'cubemaps')]
    [string]$Reflections = 'environment',
    [switch]$Benchmark,
    [switch]$Portals,
    [switch]$Debug
)
$ErrorActionPreference = 'Stop'
$viewAliases = @{ materials='lapis'; city='aes'; gallery='silva'; dragon='astra' }
if ($viewAliases.ContainsKey($View)) { $View = $viewAliases[$View] }
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$configuration = if ($Debug) { 'Debug' } else { 'RelWithDebInfo' }
$viewerPath = Join-Path $PSScriptRoot "output/capture-build/$configuration/pantheon_rooms_fixed_viewer.exe"
if (-not (Test-Path -LiteralPath $viewerPath)) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $configuration
}
$scenePath = Join-Path $PSScriptRoot ('scene.xml')
if ($View -ne 'rotunda') { $scenePath = Join-Path $PSScriptRoot ('views/' + $View + '.xml') }
if ($Portals) {
    if ($View -ne 'rotunda') { throw 'Use -Portals with the rotunda view; use verify.py --audit-portals for all audit cameras.' }
    $scenePath = Join-Path $PSScriptRoot 'portal-scene.xml'
}
$viewerArgs = @('--root', $repoPath, '--scene', $scenePath, '--shading', 'cook', '--aa', 'fxaa', '--reflections', $Reflections)
if ($Benchmark) {
    $outputPath = Join-Path $PSScriptRoot 'output'
    New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
    $viewerArgs += @('--hidden', '--no-menus', '--no-vsync', '--frames', '600', '--benchmark', (Join-Path $outputPath ($View + '.csv')))
}
if ($Debug) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio debugger not found' }
    $studioPath = & $vswherePath -latest -products '*' -property installationPath
    $debuggerPath = Join-Path $studioPath 'Common7/IDE/devenv.exe'
    if (-not (Test-Path -LiteralPath $debuggerPath)) { throw 'Visual Studio debugger not found' }
    $debuggerArgs = @('/Command', 'Debug.Start', '/DebugExe', ('"' + $viewerPath + '"'))
    $debuggerArgs += $viewerArgs | ForEach-Object { '"' + $_ + '"' }
    Start-Process -FilePath $debuggerPath -ArgumentList $debuggerArgs -WorkingDirectory $repoPath -Wait
    return
}
& $viewerPath @viewerArgs | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Viewer failed: $LASTEXITCODE" }
