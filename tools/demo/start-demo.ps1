<#
    KDE Infinit Desktop - interactive demo on Windows (Podman Desktop / podman machine)
    SPDX-License-Identifier: GPL-3.0-or-later

    Starts a complete Plasma Wayland session (KWin with the plugin, plasmashell,
    Konsole, Dolphin, Kate) in a container and opens it with the Windows
    Remote Desktop client.

    Usage:   .\tools\demo\start-demo.ps1 [-Width 1920] [-Height 1080] [-Port 13389] [-Rebuild] [-Stop]
#>
param(
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$Port = 13389,
    [ValidateSet('de', 'en')][string]$Language = 'de',
    [switch]$Rebuild,
    [switch]$Stop,
    [switch]$NoClient
)

# Native tools (podman, wsl) write progress to stderr; that is not an error.
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = (Resolve-Path (Join-Path $here '..\..')).Path
$image = 'localhost/kid-demo:latest'
$name = 'kid-demo'

if ($Stop) {
    podman rm -f $name 2>$null | Out-Null
    Write-Host 'Demo stopped.'
    return
}

# KWin needs a DRM render node for OpenGL compositing. WSL2 has none by
# default, the virtual GEM driver provides one (software rendering via llvmpipe).
Write-Host 'Loading the vgem kernel module in the podman machine ...'
wsl.exe -d podman-machine-default -u root -- modprobe vgem | Out-Null

podman image exists $image 2>$null
if ($Rebuild -or $LASTEXITCODE -ne 0) {
    Write-Host 'Building the demo image (first run takes a few minutes) ...'
    podman build -t $image -f (Join-Path $here 'Containerfile') $here
    if ($LASTEXITCODE -ne 0) { throw 'Building the demo image failed.' }
}

podman rm -f $name 2>$null | Out-Null
# 127.0.0.1 only: the session needs no login, so it must not be reachable from the network.
podman run -d --name $name --device /dev/dri -p "127.0.0.1:${Port}:3389" `
    -e "DEMO_WIDTH=$Width" -e "DEMO_HEIGHT=$Height" -e "DEMO_HOST_PORT=$Port" -e "DEMO_LANG=$Language" `
    -v "${project}:/src:ro" -v kid-demo-build:/build --entrypoint dbus-run-session $image `
    -- bash /src/tools/demo/demo-session.sh | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Starting the demo container failed.' }

function Get-DemoLines { (podman logs $name 2>&1 | ForEach-Object { "$_" }) | Where-Object { $_ -like '`[demo`]*' } }

Write-Host 'Waiting for the session (the plugin is compiled on first start) ...'
$ready = $false
for ($i = 0; $i -lt 900; $i++) {
    $lines = Get-DemoLines
    if ($lines -match 'ready:') { $ready = $true; break }
    if ($lines -match 'build failed|KWin exited') { $lines; throw 'The demo session failed to start.' }
    Start-Sleep -Seconds 1
}
Get-DemoLines
if (-not $ready) { throw 'The demo session did not become ready in time (see: podman logs kid-demo).' }

# Connection file: TLS without CredSSP/NLA (no NTLM, which Windows may have disabled),
# no password prompt, Windows key combinations go to Plasma. Authentication level 2
# makes the client insist on TLS (level 0 falls back to the legacy RDP security layer,
# which KRdp does not offer) and warn once about the self-signed certificate.
$rdpFile = Join-Path $env:TEMP 'kde-infinit-desktop-demo.rdp'
@(
    "full address:s:localhost:$Port"
    'username:s:root'
    'enablecredsspsupport:i:0'
    'negotiate security layer:i:1'
    'authentication level:i:2'
    'prompt for credentials:i:0'
    'keyboardhook:i:1'
    'screen mode id:i:1'
    "desktopwidth:i:$Width"
    "desktopheight:i:$Height"
    'smart sizing:i:1'
    'redirectclipboard:i:1'
) | Set-Content -Path $rdpFile -Encoding Unicode

Write-Host ''
Write-Host "Connect to localhost:$Port - no login needed. Connection file: $rdpFile"
Write-Host "Stop the demo with:  .\tools\demo\start-demo.ps1 -Stop"
if (-not $NoClient) {
    Start-Process mstsc.exe -ArgumentList "`"$rdpFile`""
}
