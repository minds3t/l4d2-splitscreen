param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$pluginDirectory = Split-Path -Parent $PSScriptRoot
& (Join-Path $pluginDirectory 'build.ps1') -Configuration $Configuration
