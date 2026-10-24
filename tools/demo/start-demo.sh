#!/bin/bash
# KDE Infinit Desktop - interactive demo on Linux (Podman or Docker)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts a complete Plasma Wayland session with the plugin in a container and
# makes it reachable over RDP (e.g. with KRDC, Remmina or xfreerdp).
#
# usage: tools/demo/start-demo.sh [--stop] [--rebuild] [--port 13389] [--size 1920x1080] [--lang de|en]
set -eu

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/../.." && pwd)
image=localhost/kid-demo:latest
name=kid-demo
port=13389
size=1920x1080
lang=de
rebuild=0

engine=$(command -v podman || command -v docker || true)
[ -n "$engine" ] || { echo "podman or docker is required" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --stop) "$engine" rm -f "$name" >/dev/null 2>&1 || true; echo "Demo stopped."; exit 0 ;;
        --rebuild) rebuild=1 ;;
        --port) port=$2; shift ;;
        --size) size=$2; shift ;;
        --lang) lang=$2; shift ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
    shift
done

if [ ! -e /dev/dri ]; then
    echo "No /dev/dri: loading the virtual GEM driver (software rendering) ..."
    sudo modprobe vgem
fi

if [ "$rebuild" = 1 ] || ! "$engine" image inspect "$image" >/dev/null 2>&1; then
    "$engine" build -t "$image" -f "$here/Containerfile" "$here"
fi

"$engine" rm -f "$name" >/dev/null 2>&1 || true
# 127.0.0.1 only: the session needs no login, so it must not be reachable from the network.
"$engine" run -d --name "$name" --device /dev/dri -p "127.0.0.1:$port:3389" \
    -e "DEMO_WIDTH=${size%x*}" -e "DEMO_HEIGHT=${size#*x}" -e "DEMO_HOST_PORT=$port" -e "DEMO_LANG=$lang" \
    -v "$project:/src:ro" -v kid-demo-build:/build --entrypoint dbus-run-session "$image" \
    -- bash /src/tools/demo/demo-session.sh >/dev/null

echo "Waiting for the session (the plugin is compiled on first start) ..."
for _ in $(seq 1 900); do
    logs=$("$engine" logs "$name" 2>&1 | grep '^\[demo\]' || true)
    if echo "$logs" | grep -q 'ready:'; then break; fi
    if echo "$logs" | grep -qE 'build failed|KWin exited'; then echo "$logs"; exit 1; fi
    sleep 1
done
echo "$logs"
echo
echo "Connect with an RDP client to localhost:$port - no login needed (user name root, TLS security):"
echo "  xfreerdp /v:localhost:$port /u:root /p:x /sec:tls /cert:ignore"
echo "Stop with: $0 --stop"
