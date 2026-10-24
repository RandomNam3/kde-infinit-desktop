#!/bin/bash
# Single output: panning, parking, zoom/navigation, mouse wheel and drag panning.
source /th/lib.sh
FAILURES=0

check "plugin operational" '.operational == true and .safeMode == false'
open_term Alpha 60 15 || exit 1
open_term Beta 50 12 || exit 1
open_term Gamma 70 18 || exit 1
sleep 1
echo "initial:"; win "Alpha|Beta|Gamma"; canvases
shot 01-initial
AX0=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x')

# --- D-Bus pan: the view moves right by 400 px, windows move left by 400 px
dbuscall pan 400 0
sleep 0.3
AX1=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x')
echo "Alpha x: $AX0 -> $AX1"
check "pan moved windows physically by -400" "(.windows[] | select(.caption==\"Alpha\") | .frame.x) == ($AX0 - 400)"
check "camera committed after pan" '[.canvases[] | select(.current)][0] | (.camera.x == .committed.x)'
shot 02-panned

# --- far away: everything parked outside of all outputs
dbuscall pan 5000 0
sleep 0.3
echo "after far pan:"; win "Alpha|Beta|Gamma"
check "all windows parked" '[.windows[] | select(.mode=="canvas") | .parked] | all'
check "parked windows are outside of the output" '[.windows[] | select(.mode=="canvas") | .frame.x >= 1600 or (.frame.x + .frame.width) <= 0 or .frame.y >= 900 or (.frame.y + .frame.height) <= 0] | all'
shot 03-empty

# --- back home
dbuscall goHome
sleep 0.3
echo "after home:"; win "Alpha"
check "Alpha back at its original position" "(.windows[] | select(.caption==\"Alpha\") | .frame.x) == $AX0"
check "nothing parked at home" '[.windows[] | select(.mode=="canvas") | .parked] | any | not'

# --- zoom out: navigation mode parks everything, the effect draws scaled windows
dbuscall setZoom 0.5
sleep 0.4
canvases
check "navigation mode at 50 %" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
check "windows parked while zoomed" '[.windows[] | select(.mode=="canvas") | .parked] | all'
shot 04-zoomed-50

dbuscall resetZoom
sleep 0.4
check "back to 100 %, navigation off" '[.canvases[] | select(.current)][0] | .navigation == false and .camera.zoom == 1'
check "Alpha restored after zoom" "(.windows[] | select(.caption==\"Alpha\") | .frame.x) == $AX0"

# --- real input: wheel over the empty desktop zooms (no windows at the top left corner).
# Wheel down = zoom out (we start at the 100 % maximum).
input abs 40 40 sleep 50 wheel 15 sleep 50 wheel 15 sleep 500
canvases
check "mouse wheel down on empty desktop zooms out" '[.canvases[] | select(.current)][0] | .camera.zoom < 0.9 and .navigation == true'
shot 05-wheel-zoom
input abs 40 40 sleep 50 wheel -15 sleep 50 wheel -15 sleep 50 wheel -15 sleep 600
canvases
check "mouse wheel up zooms back to exactly 100 %" '[.canvases[] | select(.current)][0] | .camera.zoom == 1 and .navigation == false'
check "windows unparked after zooming back" '[.windows[] | select(.mode=="canvas") | .parked] | any | not'

# --- real input: drag on the empty desktop pans the canvas
dbuscall resetZoom
sleep 0.3
CX0=$(state | jq '[.canvases[] | select(.current)][0].camera.x')
input drag 60 850 360 850 15 300 left
sleep 1.5
CX1=$(state | jq '[.canvases[] | select(.current)][0].camera.x')
echo "camera x after drag: $CX0 -> $CX1"
check "dragging the empty desktop pans (content follows the pointer)" "([.canvases[] | select(.current)][0].camera.x) < $CX0"
check "drag pan committed" '[.canvases[] | select(.current)][0] | (.camera.x == .committed.x) and (.navigation == false)'
shot 06-drag-panned

# --- Meta+Alt+drag on top of a window pans as well
dbuscall goHome
sleep 0.3
AX=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x + 40')
AY=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.y + 60')
CX0=$(state | jq '[.canvases[] | select(.current)][0].camera.x')
# KEY_LEFTMETA=125, KEY_LEFTALT=56
input keydown 125 keydown 56 drag "$AX" "$AY" "$((AX - 200))" "$AY" 10 200 left keyup 56 keyup 125
sleep 1.5
CX1=$(state | jq '[.canvases[] | select(.current)][0].camera.x')
echo "camera x after Meta+Alt drag: $CX0 -> $CX1"
check "Meta+Alt+drag over a window pans" "([.canvases[] | select(.current)][0].camera.x) > $CX0"

# --- zoomed out, a window that reached over the right and bottom edge is drawn completely
# (KWin's software clipping ignores the scale of the window transform; the effect must avoid it)
dbuscall goHome
sleep 0.3
foot --title Edge --app-id kid.Edge --override=colors-dark.background=ff0000 --window-size-pixels 800x400 -e sleep 600 >/dev/null 2>&1 &
for _ in $(seq 1 50); do state | grep -q '"Edge"' && break; sleep 0.1; done
sleep 0.5
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "Edge") w.frameGeometry = {x: 1300, y: 600, width: 800, height: 400};'
dbuscall setZoom 0.5
sleep 0.6
shot 07-edge-zoomed
read -r EX1 EY1 EX2 EY2 <<<"$(colored_box /out/07-edge-zoomed.png)"
echo "Edge drawn at $EX1,$EY1 .. $EX2,$EY2 (content is ~400 x ~175 at 50 %; cut off it was ~275 wide)"
if [ $((EX2 - EX1)) -ge 390 ] && [ $((EY2 - EY1)) -ge 165 ]; then echo "PASS: zoomed out window at the screen edge drawn completely"; else echo "FAIL: zoomed out window at the screen edge is cut off"; FAILURES=$((FAILURES+1)); fi

# --- resizing while zoomed out: edges and corners of the miniature, Meta + right button
dbuscall pan 300 150; sleep 0.4    # the view moves right/down: Edge comes into the middle of the screen
# screen rectangle of Edge (zoom 0.5): "left top right bottom"
edge_rect() { state | jq -r '([.canvases[] | select(.current)][0].camera) as $c | (.outputs[0].geometry) as $o | .windows[] | select(.caption=="Edge") |
    "\(($o.width/2 + (.canvasX - $c.x) * $c.zoom) | floor) \(($o.height/2 + (.canvasY - $c.y) * $c.zoom) | floor) \(($o.width/2 + (.canvasX + .frame.width - $c.x) * $c.zoom) | floor) \(($o.height/2 + (.canvasY + .frame.height - $c.y) * $c.zoom) | floor)"'; }
read -r L T R B <<<"$(edge_rect)"
W0=$(state | jq '.windows[] | select(.caption=="Edge") | .frame.width'); H0=$(state | jq '.windows[] | select(.caption=="Edge") | .frame.height')
CX0=$(state | jq '.windows[] | select(.caption=="Edge") | .canvasX')
echo "Edge on screen: $L,$T .. $R,$B, size ${W0}x${H0}"
input drag $((R - 3)) $((B - 3)) $((R + 57)) $((B + 37)) 12 300 left sleep 600
check "dragging the bottom right corner while zoomed out makes the window larger" "(.windows[] | select(.caption==\"Edge\") | .frame.width >= ($W0 + 90) and .frame.height >= ($H0 + 50))"
check "the top left corner stayed on the canvas" "(.windows[] | select(.caption==\"Edge\") | .canvasX) == $CX0"
read -r L T R B <<<"$(edge_rect)"
RIGHT0=$(state | jq '.windows[] | select(.caption=="Edge") | .canvasX + .frame.width')
input drag $((L + 2)) $(((T + B) / 2)) $((L - 48)) $(((T + B) / 2)) 10 300 left sleep 600
check "dragging the left edge widens the window to the left" "(.windows[] | select(.caption==\"Edge\") | .canvasX) < ($CX0 - 60)"
check "the right edge stayed where it was" "(.windows[] | select(.caption==\"Edge\") | (.canvasX + .frame.width - $RIGHT0) | fabs) <= 12"
W1=$(state | jq '.windows[] | select(.caption=="Edge") | .frame.width')
read -r L T R B <<<"$(edge_rect)"
# KEY_LEFTMETA=125; Meta + right button near the bottom right corner, drag towards the top left
input keydown 125 drag $((R - 30)) $((B - 30)) $((R - 80)) $((B - 60)) 10 300 right keyup 125 sleep 600
check "Meta + right button resizes while zoomed out" "(.windows[] | select(.caption==\"Edge\") | .frame.width) <= ($W1 - 60)"
check "still zoomed out after resizing" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
shot 08-resized-zoomed
dbuscall resetZoom
sleep 0.4

check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
