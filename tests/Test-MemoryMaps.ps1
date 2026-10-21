[CmdletBinding()]
param(
    [string]$NdkPath = $env:ANDROID_NDK_HOME,
    [switch]$Run,
    [string]$Serial
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$NdkPath) { throw 'Set ANDROID_NDK_HOME or pass -NdkPath.' }
$llvm = Join-Path (Resolve-Path -LiteralPath $NdkPath).Path 'toolchains/llvm/prebuilt/windows-x86_64'
$compiler = Join-Path $llvm 'bin/clang++.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw "Android compiler missing: $compiler" }
$output = Join-Path $repoRoot 'build/runtime-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$binary = Join-Path $output 'memory_maps_test'
$argsForCompiler = @(
    '--target=aarch64-linux-android29', ('--sysroot=' + (Join-Path $llvm 'sysroot')),
    '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden', '-fno-exceptions', '-fno-rtti', '-O2', '-UNDEBUG', '-static-libstdc++', '-D_GNU_SOURCE', '-Wl,-z,max-page-size=16384',
    '-I', (Join-Path $repoRoot 'include'),
    (Join-Path $repoRoot 'tests/MemoryMapsTest.cpp'),
    (Join-Path $repoRoot 'src/MemoryMaps.cpp'), '-ldl', '-o', $binary
)
& $compiler @argsForCompiler
if ($LASTEXITCODE -ne 0) { throw "Memory-map regression compile failed ($LASTEXITCODE)." }
Write-Host "Built $binary"
if ($Run) {
    $adbCommand = Get-Command adb -ErrorAction SilentlyContinue
    $sdkPath = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { $env:ANDROID_HOME }
    $adb = if ($adbCommand) { $adbCommand.Source } elseif ($sdkPath) { Join-Path $sdkPath 'platform-tools/adb.exe' } else { '' }
    if (!$adb -or !(Test-Path -LiteralPath $adb)) { throw 'adb not found; add platform-tools to PATH.' }
    $deviceArgs = @()
    if ($Serial) { $deviceArgs = @('-s', $Serial) }
    $remote = '/data/local/tmp/hideso-memory-maps-test'
    & $adb @deviceArgs push $binary $remote
    if ($LASTEXITCODE -ne 0) { throw 'Memory-map regression push failed.' }
    & $adb @deviceArgs shell chmod 700 $remote
    if ($LASTEXITCODE -ne 0) { throw 'Could not mark regression executable.' }
    & $adb @deviceArgs shell $remote
    if ($LASTEXITCODE -ne 0) { throw "Memory-map regression failed ($LASTEXITCODE)." }
}
