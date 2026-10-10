param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$sourceDirectory = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Build Tools with C++ tools is required.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'No MSVC x86/x64 toolchain found.' }
$cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
& $cmake -S $sourceDirectory -B (Join-Path $sourceDirectory 'build') -G 'Visual Studio 17 2022' -A Win32
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
# Reviewed source snapshots may preserve older timestamps when restored. Always
# rebuild this small target so the validation manifest cannot describe
# newer source content paired with a timestamp-cached object from another copy.
& $cmake --build (Join-Path $sourceDirectory 'build') --config $Configuration --clean-first --parallel
if ($LASTEXITCODE -ne 0) { throw 'The plugin build failed.' }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $ctest --test-dir (Join-Path $sourceDirectory 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'The ABI tests failed.' }
# The search patterns must find every address where it was researched; this needs
# the researched game build installed. Without it the check is skipped.
$packaging = Join-Path (Split-Path -Parent $sourceDirectory) 'tools\packaging'
$game = & python -c "import sys; sys.path.insert(0, sys.argv[1]); import game_files; print(game_files.find_game() or '')" $packaging
if ($game -and (Test-Path -LiteralPath (Join-Path $game 'left4dead2.exe'))) {
    & (Join-Path $sourceDirectory "build\$Configuration\splitscreen_check.exe") $game --researched
    if ($LASTEXITCODE -ne 0) { throw 'The search patterns do not fit the installed game; see tools/analysis/make_signatures.py.' }
} else { Write-Warning 'Game not found: search patterns not checked.' }
& python (Join-Path $sourceDirectory 'validate_build.py') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'The build validation failed.' }
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $sourceDirectory "build\$Configuration\splitscreen_support.dll")
