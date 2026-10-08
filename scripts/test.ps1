param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
& "$PSScriptRoot\build.ps1" $Configuration
if($LASTEXITCODE){exit $LASTEXITCODE}
& "$Root\build\$Configuration\SilentVibrance_tests.exe"
exit $LASTEXITCODE

