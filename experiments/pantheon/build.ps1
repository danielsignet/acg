param(
    [ValidateSet('Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [string]$CMakeCommand = 'cmake'
)
$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cmakePath = (Get-Command $CMakeCommand -ErrorAction Stop).Source
$baseBuild = Join-Path $repoPath 'build/windows'
& $cmakePath --preset windows -S (Join-Path $repoPath 'cmake/standalone')
if ($LASTEXITCODE -ne 0) { throw 'Base library configuration failed' }
& $cmakePath --build $baseBuild --config $Configuration --target viewer pantheon --parallel 1 -- /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Base library build failed' }
$experimentBuild = Join-Path $PSScriptRoot 'output/capture-build'
& $cmakePath -S (Join-Path $PSScriptRoot 'tools') -B $experimentBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Experiment configuration failed' }
& $cmakePath --build $experimentBuild --config $Configuration --parallel 1 -- /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Experiment build failed' }
