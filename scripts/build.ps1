param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$Tools='C:\Users\burak\Documents\Codex\2026-08-18\new-chat\work\python-tools'
$CMake=Join-Path $Tools 'cmake\data\bin\cmake.exe'
$Zig=Join-Path $Tools 'ziglang\zig.exe'
$Ninja=Join-Path $Root 'work\tools\ninja.exe'
if(!(Test-Path $CMake) -or !(Test-Path $Zig) -or !(Test-Path $Ninja)){ throw 'CMake/Zig/Ninja toolchain not found.' }
$Zig=$Zig.Replace('\','/')
$Ninja=$Ninja.Replace('\','/')
$Build=Join-Path $Root "build\$Configuration"
& $CMake --fresh -S $Root -B $Build -G 'Ninja' "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_CXX_COMPILER=$Zig" '-DCMAKE_CXX_COMPILER_ARG1=c++' "-DCMAKE_RC_COMPILER=$Zig" '-DCMAKE_RC_COMPILER_ARG1=rc'
if($LASTEXITCODE){exit $LASTEXITCODE}
& $CMake --build $Build --parallel
exit $LASTEXITCODE
