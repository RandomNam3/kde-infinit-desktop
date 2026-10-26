#!/bin/bash
# Remembered placements, pinning (shortcut), fullscreen lock and fullscreen tiles, also for
# borderless fullscreen (X11 window without frame covering the screen, like games under Proton).
source /th/lib.sh
FAILURES=0
KEY_LEFTMETA=125; KEY_LEFTALT=56; KEY_LEFTSHIFT=42; KEY_T=20; KEY_PAGEDOWN=109

input abs 800 450

# ---------------------------------------------------------------- placement memory
open_term Alpha 60 15 || exit 1
dbuscall pan 500 0          # Alpha moves 500 px to the left on screen
sleep 0.3
AX=$(state | jq '.windows[] | select(.caption=="Alpha") | .canvasX')
AW=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.width')
echo "Alpha canvas x before closing: $AX (width $AW)"
pkill -f "title Alpha" ; sleep 1
echo "remembered:"; dbuscall windowPlacements | jq -c '.applications["kid.alpha"][0] | {canvas, screen, width, height}'
check "Alpha is gone" '[.windows[] | select(.caption=="Alpha" and .mode != "canvas")] | length == 0'
dbuscall goHome; sleep 0.3
open_term Alpha 30 8 || exit 1     # opens smaller and centred by KWin, must be restored
sleep 0.5
win "Alpha"
check "reopened Alpha at its remembered canvas position" "(.windows[] | select(.caption==\"Alpha\") | .canvasX) == $AX"
check "the view moved to the reopened window (fully visible)" '(.windows[] | select(.caption=="Alpha") | .parked) == false'
check "reopened Alpha got its old size back" "(.windows[] | select(.caption==\"Alpha\") | .frame.width) == $AW"
shot 01-restored

# ---------------------------------------------------------------- pin to screen (Meta+Alt+T)
open_term Pinme 40 10 || exit 1
sleep 0.3
PX=$(state | jq '.windows[] | select(.caption=="Pinme") | .frame.x')
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT key $KEY_T keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
check "Meta+Alt+T pinned the active window" '(.windows[] | select(.caption=="Pinme") | .mode) == "pinned"'
check "a plain pin does not keep the window above others" '(.windows[] | select(.caption=="Pinme") | .keepAbove) == false'
dbuscall pan 300 200; sleep 0.3
check "pinned window ignores panning" "(.windows[] | select(.caption==\"Pinme\") | .frame.x) == $PX"
dbuscall setZoom 0.4; sleep 0.3
check "pinned window ignores zooming (still interactive, not parked)" "(.windows[] | select(.caption==\"Pinme\") | .frame.x) == $PX"
shot 02-pinned-while-zoomed
dbuscall resetZoom; sleep 0.3
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT key $KEY_T keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
check "Meta+Alt+T again releases it onto the canvas" '(.windows[] | select(.caption=="Pinme") | .mode) == "canvas"'
# pin + always on top
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT keydown $KEY_LEFTSHIFT key $KEY_T keyup $KEY_LEFTSHIFT keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
check "Meta+Alt+Shift+T pins" '(.windows[] | select(.caption=="Pinme") | .mode) == "pinned"'
check "Meta+Alt+Shift+T keeps it above others" '(.windows[] | select(.caption=="Pinme") | .keepAbove) == true'
# release, plain pin, then Shift: adds "always on top" instead of releasing
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT key $KEY_T keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
check "Meta+Alt+T releases a window pinned on top, and on top ends" '(.windows[] | select(.caption=="Pinme") | .mode == "canvas" and .keepAbove == false)'
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT key $KEY_T keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT keydown $KEY_LEFTSHIFT key $KEY_T keyup $KEY_LEFTSHIFT keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
check "Meta+Alt+Shift+T on a pinned window adds always on top" '(.windows[] | select(.caption=="Pinme") | .mode == "pinned" and .keepAbove == true)'

# ---------------------------------------------------------------- two windows of one application
open_twin() { foot --title "$1" --app-id kid.twin --window-size-chars 40x10 -e sleep 600 >/dev/null 2>&1 & for _ in $(seq 1 50); do state | grep -q "\"$1\"" && break; sleep 0.1; done; sleep 0.6; }
# Two pinned windows closed at the same place leave two remembered placements there.
for t in Twin1 Twin2; do
  open_twin $t
  kwin_js "for (const w of workspace.windowList()) if (w.caption == \"$t\") w.frameGeometry = {x: 200, y: 150, width: w.frameGeometry.width, height: w.frameGeometry.height};"
  input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT key $KEY_T keyup $KEY_LEFTALT keyup $KEY_LEFTMETA sleep 300
done
pkill -f "title Twin"; sleep 1
TWINS=$(dbuscall windowPlacements | jq '[.applications["kid.twin"][] | select(.pinned)] | length')
if [ "$TWINS" -ge 2 ]; then echo "PASS: two pinned placements of the application remembered"; else echo "FAIL: expected two pinned placements, got $TWINS"; FAILURES=$((FAILURES+1)); fi
open_twin Twin3
check "the reopened window of a pinned application is pinned again" '(.windows[] | select(.caption=="Twin3") | .mode) == "pinned"'
open_twin Twin4
check "an additional window of that application is not pinned" '(.windows[] | select(.caption=="Twin4") | .mode) == "canvas"'
check "the additional window does not open exactly on top of the first" '([.windows[] | select(.caption=="Twin3")][0].frame) as $a | ([.windows[] | select(.caption=="Twin4")][0].frame) as $b | (($a.x - $b.x) | fabs) + (($a.y - $b.y) | fabs) >= 16'
shot 02b-twins
pkill -f "title Twin"; sleep 0.5

# ---------------------------------------------------------------- fullscreen
dbuscall goHome; sleep 0.3
foot --fullscreen --title Movie --app-id kid.Movie -e bash -c "sleep 600" >/dev/null 2>&1 &
for _ in $(seq 1 50); do state | grep -q '"Movie"' && break; sleep 0.1; done
sleep 1
check "fullscreen window is not on the canvas" '(.windows[] | select(.caption=="Movie") | .mode) == "fullscreen"'
check "output locked while the fullscreen window has focus" '[.outputs[] | .locked] | any'
CX0=$(state | jq '[.canvases[] | select(.current)][0].camera.x')
input keydown $KEY_LEFTMETA keydown $KEY_LEFTALT drag 700 500 300 500 10 200 left keyup $KEY_LEFTALT keyup $KEY_LEFTMETA
input abs 40 40 sleep 30 wheel 15 sleep 300
sleep 0.5
check "no panning/zooming while the fullscreen window has focus" "([.canvases[] | select(.current)][0] | .camera.x == $CX0 and .camera.zoom == 1)"
shot 03-fullscreen
# Meta+PgDown minimizes: the window becomes a tile on the canvas
input keydown $KEY_LEFTMETA key $KEY_PAGEDOWN keyup $KEY_LEFTMETA sleep 800
win "Movie"
check "minimized fullscreen window became a tile" '(.windows[] | select(.caption=="Movie") | .mode) == "tile" and ((.windows[] | select(.caption=="Movie") | .tileX) != null)'
shot 04-tile-live
# icon mode
kwriteconfig6 --file kwinrc --group Effect-infinitedesktop --key MinimizedFullscreenContent AppIcon
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect infinitedesktop
sleep 0.8
shot 05-tile-icon
# click on the tile restores fullscreen
TX=$(state | jq '.windows[] | select(.caption=="Movie") | .tileX')
TY=$(state | jq '.windows[] | select(.caption=="Movie") | .tileY')
read -r SX SY < <(state | jq -r --argjson tx "$TX" --argjson ty "$TY" '([.canvases[] | select(.current)][0].camera) as $c | ((.outputs[0].geometry) as $o | "\(($o.x + $o.width/2 + ($tx + 30 - $c.x) * $c.zoom) | floor) \(($o.y + $o.height/2 + ($ty + 30 - $c.y) * $c.zoom) | floor)")')
echo "clicking the tile at $SX,$SY"
input abs "$SX" "$SY" sleep 50 click left sleep 800
check "click on the tile restored the fullscreen window" '(.windows[] | select(.caption=="Movie") | .mode) == "fullscreen"'
shot 06-restored-fullscreen

# ---------------------------------------------------------------- borderless fullscreen (games under Wine/Proton)
pkill -f "title Movie"; sleep 0.5
dbuscall goHome; sleep 0.3
QT_QPA_PLATFORM=xcb /usr/lib/qt6/bin/qml /th/probe.qml > /tmp/probe-x11.log 2>&1 &
XPID=$!
for _ in $(seq 1 80); do state | grep -q '"ProbeX11"' && break; sleep 0.1; done
sleep 0.8
check "a framed X11 window is a canvas window" '(.windows[] | select(.caption=="ProbeX11") | .mode) == "canvas"'
# what such a game does: no frame, exactly the whole screen, no fullscreen state
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "ProbeX11") { w.noBorder = true; w.frameGeometry = {x: 0, y: 0, width: 1600, height: 900}; }'
win "ProbeX11"
check "a window without frame covering the whole screen counts as fullscreen" '(.windows[] | select(.caption=="ProbeX11") | .mode) == "fullscreen"'
check "output locked while the borderless fullscreen window has focus" '[.outputs[] | .locked] | any'
input keydown $KEY_LEFTMETA key $KEY_PAGEDOWN keyup $KEY_LEFTMETA sleep 800
check "minimized borderless fullscreen window became a tile" '(.windows[] | select(.caption=="ProbeX11") | .mode) == "tile" and ((.windows[] | select(.caption=="ProbeX11") | .tileX) != null)'
TX=$(state | jq '.windows[] | select(.caption=="ProbeX11") | .tileX')
TY=$(state | jq '.windows[] | select(.caption=="ProbeX11") | .tileY')
read -r SX SY < <(state | jq -r --argjson tx "$TX" --argjson ty "$TY" '([.canvases[] | select(.current)][0].camera) as $c | ((.outputs[0].geometry) as $o | "\(($o.x + $o.width/2 + ($tx + 30 - $c.x) * $c.zoom) | floor) \(($o.y + $o.height/2 + ($ty + 30 - $c.y) * $c.zoom) | floor)")')
input abs "$SX" "$SY" sleep 50 click left sleep 800
check "click on the tile restored the borderless fullscreen window" '(.windows[] | select(.caption=="ProbeX11") | .mode) == "fullscreen"'
# the game goes back to a smaller window: a canvas window again
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "ProbeX11") w.frameGeometry = {x: 200, y: 150, width: 800, height: 500};'
sleep 0.3
check "no longer covering the screen, it is a canvas window again" '(.windows[] | select(.caption=="ProbeX11") | .mode) == "canvas"'
check "and the output is no longer locked" '[.outputs[] | .locked] | any | not'
kill "$XPID" 2>/dev/null
sleep 0.5

check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
