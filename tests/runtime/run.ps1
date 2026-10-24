<#
    Runs the runtime scenarios in a headless KWin inside a container (Windows / podman machine).
    SPDX-License-Identifier: GPL-3.0-or-later

    Usage: .\tests\runtime\run.ps1 [-Asan] [-Scenarios 1,2,3,4,5] [-BetterBlur <checkout>]
    Scenario 5 runs with other KWin effects enabled; -BetterBlur points to a checkout of
    Better Blur DX (github.com/xarblu/kwin-effects-better-blur-dx) to test with it.
#>
param(
    [switch]$Asan,
    [int[]]$Scenarios = @(1, 2, 3, 4, 5, 6),
    [string]$BetterBlur = ''
)
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = (Resolve-Path (Join-Path $here '..\..')).Path
$image = 'localhost/kid-runtime-tests:latest'
$out = Join-Path $env:TEMP 'kid-runtime-tests'

wsl.exe -d podman-machine-default -u root -- modprobe vgem | Out-Null
podman image exists $image 2>$null
if ($LASTEXITCODE -ne 0) { podman build -t $image -f (Join-Path $here 'Containerfile') $here }

$failures = 0
foreach ($s in $Scenarios) {
    $outputs = if ($s -eq 2) { 2 } else { 1 }
    $dir = Join-Path $out "s$s"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $envArgs = @()
    $volume = 'kid-runtime-build'
    if ($Asan) { $envArgs = @('-e', 'USE_ASAN=1', '-e', 'CMAKE_EXTRA=-DENABLE_SANITIZERS=ON'); $volume = 'kid-runtime-build-asan' }
    $session = 'session.sh'
    if ($s -eq 5) {
        $session = 'compat-session.sh'
        if ($BetterBlur) { $envArgs += @('-v', "$((Resolve-Path $BetterBlur).Path):/betterblur:ro", '-v', 'kid-runtime-betterblur:/bbbuild') }
    }
    Write-Host "=== scenario $s"
    podman run --rm --device /dev/dri @envArgs -v "${project}:/src:ro" -v "${here}:/th:ro" -v "${dir}:/out" `
        -v "${volume}:/build" -v kid-runtime-harness:/thbuild $image dbus-run-session -- bash "/th/$session" "/th/scenario$s.sh" $outputs 2>&1 |
        ForEach-Object { "$_" } | Select-String -Pattern 'PASS|FAIL|FAILURES|INFO|error|Segmentation|Abort|Sanitizer'
    if ($LASTEXITCODE -ne 0) { $failures++ }
}
Write-Host "screenshots and logs: $out"
exit $failures
