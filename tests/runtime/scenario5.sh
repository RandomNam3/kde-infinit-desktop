#!/bin/bash
# Compatibility: the same canvas behaviour with other KWin effects active
# (blur or Better Blur DX, translucency, wobbly windows, magic lamp).
source /th/lib.sh
FAILURES=0
input abs 800 450

loaded=$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | tr '\n' ' ')
echo "loaded effects: $loaded"
for effect in infinitedesktop translucency wobblywindows magiclamp; do
  if echo " $loaded " | grep -q " $effect "; then echo "PASS: $effect loaded"; else echo "FAIL: $effect not loaded"; FAILURES=$((FAILURES+1)); fi
done
if echo " $loaded " | grep -q " better_blur_dx "; then echo "INFO: testing with Better Blur DX"; else echo "INFO: testing with the built-in blur"; fi

# Translucent windows so that blur behind them is actually rendered.
for n in Glass1 Glass2; do
  foot --title "$n" --app-id "kid.$n" --override=colors-dark.alpha=0.55 --window-size-chars 60x15 -e bash -c "echo $n; sleep 600" >/dev/null 2>&1 &
  for _ in $(seq 1 50); do state | grep -q "\"$n\"" && break; sleep 0.1; done
done
sleep 1
GX=$(state | jq '.windows[] | select(.caption=="Glass1") | .frame.x')
shot 01-blur-initial

dbuscall pan 300 0; sleep 0.3
check "panning works with blur/translucency active" "(.windows[] | select(.caption==\"Glass1\") | .frame.x) == ($GX - 300)"
dbuscall setZoom 0.5; sleep 0.4
shot 02-blur-zoomed
check "zoom works with blur active" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
dbuscall resetZoom; sleep 0.3
dbuscall goHome; sleep 0.3
check "windows restored with blur active" "(.windows[] | select(.caption==\"Glass1\") | .frame.x) == $GX"

# Wobbly windows deform a window while it is dragged; the canvas must still see a normal move.
# Drag the top window: Glass1's title bar may lie inside Glass2's invisible resize border.
G2X=$(state | jq '.windows[] | select(.caption=="Glass2") | .frame.x')
G2CX=$(state | jq '.windows[] | select(.caption=="Glass2") | .canvasX')
TX=$(state | jq '.windows[] | select(.caption=="Glass2") | .frame.x + .frame.width / 2 | floor')
TY=$(state | jq '.windows[] | select(.caption=="Glass2") | .frame.y + 14 | floor')
input drag "$TX" "$TY" $((TX - 250)) $((TY + 120)) 20 600 left
sleep 1
check "drag with wobbly windows moved the window" "(.windows[] | select(.caption==\"Glass2\") | .frame.x) == ($G2X - 250)"
check "the canvas position followed the drag" "(.windows[] | select(.caption==\"Glass2\") | .canvasX) == ($G2CX - 250)"
shot 03-after-wobbly-drag

# Magic lamp animates minimize/unminimize.
KEY_LEFTMETA=125; KEY_PAGEDOWN=109
input keydown $KEY_LEFTMETA key $KEY_PAGEDOWN keyup $KEY_LEFTMETA sleep 900
check "minimize with magic lamp" '[.windows[] | select(.mode=="minimized")] | length == 1'
dbuscall pan 200 100; sleep 0.3
dbuscall goHome; sleep 0.3

# Close animations (fade, scale, ...) keep a closed window alive until they have run.
# A parked window is outside of every screen, where nothing is painted: it must still
# be released instead of lingering forever.
open_term Doomed 40 10 || exit 1
sleep 0.5
dbuscall pan 6000 0; sleep 0.3
check "the window is parked" '(.windows[] | select(.caption=="Doomed") | .parked) == true'
pkill -f "title Doomed"
sleep 2
check "a window closed while parked is released despite close animations" '[.windows[] | select(.caption=="Doomed")] | length == 0'
dbuscall goHome; sleep 0.3

check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
