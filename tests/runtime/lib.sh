#!/bin/bash
# Helpers for scenario scripts.
OUT=/out
mkdir -p "$OUT"

state() { qdbus6 org.kde.KWin /InfiniteDesktop org.kde.InfiniteDesktop.state; }
shot() { python3 /th/screenshot.py "$OUT/$1.png" ${2:-} 2>&1 | tail -1; }
dbuscall() { local method=$1; shift; qdbus6 org.kde.KWin /InfiniteDesktop "org.kde.InfiniteDesktop.$method" "$@"; }
input() { /usr/local/bin/kid-input "$@"; }
win() { state | jq -c ".windows[] | select(.caption | test(\"$1\")) | {caption, mode, parked, canvasX, canvasY, frame}"; }
canvases() { state | jq -c '.canvases[] | select(.current) | {key, camera, committed, navigation}'; }

# Opens a Wayland test window (foot terminal) with a title and size in cells.
open_term() {
  local title=$1 cols=${2:-60} rows=${3:-16}
  foot --title "$title" --app-id "kid.$title" --window-size-chars "${cols}x${rows}" -e bash -c "echo $title; sleep 600" >/dev/null 2>&1 &
  for _ in $(seq 1 50); do state | grep -q "\"$title\"" && return 0; sleep 0.1; done
  echo "window $title did not appear"; return 1
}
open_xterm() {
  local title=$1
  xterm -T "$title" -geometry 60x14 -e "sleep 600" >/dev/null 2>&1 &
  for _ in $(seq 1 50); do state | grep -q "\"$title\"" && return 0; sleep 0.1; done
  echo "xterm $title did not appear"; return 1
}
# Runs a KWin script, e.g. kwin_js 'workspace.activeWindow.frameGeometry = {x: 0, y: 0, width: 800, height: 400};'
kwin_js() {
  printf '%s\n' "$1" > /tmp/kwin-js.js
  local id; id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript /tmp/kwin-js.js "js$RANDOM")
  qdbus6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run >/dev/null
  sleep 0.5
}
# Bounding box "x1 y1 x2 y2" of pure red (or pure blue) pixels in a screenshot.
colored_box() {
  python3 - "$1" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load(); w, h = im.size
xs, ys = [], []
for y in range(0, h, 2):
    for x in range(0, w, 2):
        r, g, b = px[x, y]
        if max(r, b) > 180 and g < 60 and min(r, b) < 60:
            xs.append(x); ys.append(y)
print(f"{min(xs)} {min(ys)} {max(xs)} {max(ys)}" if xs else "0 0 0 0")
EOF
}
check() {
  # check "<description>" <jq expression evaluating to true>
  if state | jq -e "$2" >/dev/null; then echo "PASS: $1"; else echo "FAIL: $1"; FAILURES=$((${FAILURES:-0}+1)); fi
}
