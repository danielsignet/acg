param(
    [ValidateSet('Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bundledCmake = Join-Path $repoPath '../.tools/cmake/cmake/data/bin/cmake.exe'
$cmakePath = if (Test-Path -LiteralPath $bundledCmake) { (Resolve-Path $bundledCmake).Path } else { 'cmake' }
$baseBuild = Join-Path $repoPath 'build/windows'
& $cmakePath --build $baseBuild --config $Configuration --target vrlib --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Base library build failed' }
$experimentBuild = Join-Path $PSScriptRoot 'output/capture-build'
& $cmakePath -S (Join-Path $PSScriptRoot 'tools') -B $experimentBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Experiment configuration failed' }
& $cmakePath --build $experimentBuild --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Experiment build failed' }
