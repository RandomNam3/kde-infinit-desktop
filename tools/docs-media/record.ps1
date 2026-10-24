<#
    KDE Infinit Desktop - records the images and animations of the documentation
    (Windows / Podman). Uses the demo image (tools/demo); the running demo is not touched.
    SPDX-License-Identifier: GPL-3.0-or-later

    Usage: .\tools\docs-media\record.ps1 [-Scenes pan,zoom,...] [-Output docs\images] [-Language de|en]
#>
param(
    [string]$Scenes = 'all',
    [string]$Output = '',
    [ValidateSet('de', 'en')][string]$Language = 'de'
)
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = (Resolve-Path (Join-Path $here '..\..')).Path
if (-not $Output) { $Output = Join-Path $project 'docs\images' }
New-Item -ItemType Directory -Force $Output | Out-Null
$image = 'localhost/kid-demo:latest'

wsl.exe -d podman-machine-default -u root -- modprobe vgem | Out-Null
podman image exists $image 2>$null
if ($LASTEXITCODE -ne 0) {
    podman build -t $image -f (Join-Path $project 'tools\demo\Containerfile') (Join-Path $project 'tools\demo')
}
podman run --rm --device /dev/dri -e "MEDIA_SCENES=$Scenes" -e "MEDIA_LANG=$Language" `
    -v "${project}:/src:ro" -v "${here}:/media:ro" -v "$((Resolve-Path $Output).Path):/out" `
    -v kid-media-build:/build -v kid-media-harness:/thbuild `
    --entrypoint dbus-run-session $image -- bash /media/session.sh 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -like '`[media`]*' -or $_ -match 'error|Traceback|File "' }
