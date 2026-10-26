#!/bin/bash
# KDE Infinite Desktop - records the images and animations of the documentation (Linux,
# Podman or Docker). Uses the demo image (tools/demo); a running demo is not touched.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/docs-media/record.sh [--scenes pan,zoom,...] [--output DIR] [--lang de|en]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/../.." && pwd)
image=localhost/kid-demo:latest
scenes=all
output=$project/docs/images
lang=de
while [ $# -gt 0 ]; do
    case "$1" in
        --scenes) scenes=$2; shift ;;
        --output) output=$2; shift ;;
        --lang) lang=$2; shift ;;
        -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
    shift
done
engine=$(command -v podman || command -v docker || true)
[ -n "$engine" ] || { echo "podman or docker is required" >&2; exit 1; }
[ -e /dev/dri ] || sudo modprobe vgem
"$engine" image inspect "$image" >/dev/null 2>&1 || "$engine" build -t "$image" -f "$project/tools/demo/Containerfile" "$project/tools/demo"
mkdir -p "$output"
"$engine" run --rm --device /dev/dri -e "MEDIA_SCENES=$scenes" -e "MEDIA_LANG=$lang" \
    -v "$project:/src:ro" -v "$here:/media:ro" -v "$(cd "$output" && pwd):/out" \
    -v kid-media-build:/build -v kid-media-harness:/thbuild \
    --entrypoint dbus-run-session "$image" -- bash /media/session.sh 2>&1 | grep -E '^\[media\]|error|Traceback|File "' || true
