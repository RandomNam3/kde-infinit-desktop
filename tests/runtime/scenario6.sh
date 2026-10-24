#!/bin/bash
# Working while zoomed out (clicks, wheel and keys reach the window, title bar moves it,
# sharp content), markers for windows outside the view, and no glide after circling.
source /th/lib.sh
FAILURES=0
input abs 5 5

QT_QPA_PLATFORM=wayland /usr/lib/qt6/bin/qml /th/probe.qml > /tmp/probe.log 2>&1 &
for _ in $(seq 1 80); do state | grep -q '"Probe"' && break; sleep 0.1; done
sleep 0.8
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "Probe") w.frameGeometry = {x: 500, y: 250, width: w.frameGeometry.width, height: w.frameGeometry.height};'
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "Probe") print("PROBEGEO " + (w.clientGeometry.x - w.frameGeometry.x) + " " + (w.clientGeometry.y - w.frameGeometry.y));'
read -r OX OY <<<"$(grep 'PROBEGEO' /tmp/kwin.log | tail -1 | sed 's/.*PROBEGEO //')"
W0=$(state | jq '.windows[] | select(.caption=="Probe") | .frame.width')
echo "client area starts at $OX,$OY inside the frame; width $W0"
echo "Probe before zooming: $(win Probe) $(canvases)"

# screen position of a point inside the client area of Probe (or window $3 with client offset $4,$5) at the current zoom: "x y"
probe_point() { state | jq -r --argjson lx "$1" --argjson ly "$2" --arg caption "${3:-Probe}" --argjson ox "${4:-$OX}" --argjson oy "${5:-$OY}" '([.canvases[] | select(.current)][0].camera) as $c | (.outputs[0].geometry) as $o | .windows[] | select(.caption==$caption) |
    "\(($o.width/2 + (.canvasX + $ox + $lx - $c.x) * $c.zoom) | floor) \(($o.height/2 + (.canvasY + $oy + $ly - $c.y) * $c.zoom) | floor)"'; }
last() { grep "$1" /tmp/probe.log | tail -1 | sed "s/.*$1 //"; }

# ---------------------------------------------------------------- using a window at 50 %
input abs 800 450 sleep 50          # setZoom zooms around the pointer
dbuscall setZoom 0.5; sleep 1.2
check "zoomed out" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
echo "Probe after zooming: $(win Probe) $(canvases)"
read -r PX PY <<<"$(probe_point 200 120)"
input abs $((PX + 4)) "$PY" sleep 60 abs "$PX" "$PY" sleep 60
input drag "$PX" "$PY" "$PX" "$PY" 1 40 left sleep 300
read -r CX CY _ <<<"$(last PRESS)"
echo "click at screen $PX,$PY arrived at $CX,$CY (expected about 200,120)"
if [ -n "$CX" ] && [ $((CX > 200 ? CX - 200 : 200 - CX)) -le 3 ] && [ $((CY > 120 ? CY - 120 : 120 - CY)) -le 3 ]; then echo "PASS: a click while zoomed out reaches the window at the right position"; else echo "FAIL: click while zoomed out arrived at '$CX,$CY'"; FAILURES=$((FAILURES+1)); fi
check "the click did not return to 100 %" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
check "the clicked window is active" '(.windows[] | select(.caption=="Probe") | .mode) == "canvas"'
input wheel 15 sleep 300
if [ -n "$(last WHEEL)" ]; then echo "PASS: the wheel over a zoomed out window scrolls the window"; else echo "FAIL: no wheel event in the window"; FAILURES=$((FAILURES+1)); fi
check "the wheel did not zoom" '[.canvases[] | select(.current)][0] | .camera.zoom == 0.5'
# KEY_A=30 KEY_B=48 KEY_C=46
input key 30 key 48 key 46 sleep 300
if [ "$(last TEXT)" = "abc" ]; then echo "PASS: typing goes to the window used while zoomed out"; else echo "FAIL: typed text is '$(last TEXT)'"; FAILURES=$((FAILURES+1)); fi
check "the window draws for its zoomed size (content scale 0.5)" '(.windows[] | select(.caption=="Probe") | .contentScale) == 0.5'
shot 01-working-zoomed

# a context menu (xdg popup) opens at the pointer, not where KWin put it next to the parked window
read -r PX PY <<<"$(probe_point 260 160)"
input abs "$PX" "$PY" sleep 60 click right sleep 700
shot 01b-popup-zoomed
POPUP=$(python3 - /out/01b-popup-zoomed.png <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load(); w, h = im.size
pts = [(x, y) for y in range(0, h, 2) for x in range(0, w, 2) if px[x, y][1] >= 200 and px[x, y][0] <= 60 and px[x, y][2] <= 60]
if pts:
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    print(f"{min(xs)} {min(ys)} {max(xs) - min(xs) + 2}")
EOF
)
read -r GX GY GW <<<"$POPUP"
echo "right click at $PX,$PY; popup drawn at ${GX:-none},${GY:-} (width ${GW:-0}, expected about 80)"
if [ -n "$GX" ] && [ $((GX > PX ? GX - PX : PX - GX)) -le 4 ] && [ $((GY > PY ? GY - PY : PY - GY)) -le 4 ] && [ "$GW" -ge 74 ] && [ "$GW" -le 86 ]; then
  echo "PASS: the popup of a zoomed out window opens at the pointer, drawn at the zoom"
else
  echo "FAIL: the popup is drawn at '${GX:-none},${GY:-}' (width ${GW:-0}) instead of at $PX,$PY"; FAILURES=$((FAILURES+1))
fi
if [ -n "$GX" ]; then
  input abs $((GX + 20)) $((GY + 15)) sleep 60 click left sleep 400
  read -r QX QY <<<"$(last POPUPPRESS)"
  echo "click into the popup arrived at ${QX:-none},${QY:-} (expected about 40,30)"
  if [ -n "$QX" ] && [ $((QX > 40 ? QX - 40 : 40 - QX)) -le 4 ] && [ $((QY > 30 ? QY - 30 : 30 - QY)) -le 4 ]; then echo "PASS: a click into the popup reaches it at the right position"; else echo "FAIL: click into the popup arrived at '${QX:-none},${QY:-}'"; FAILURES=$((FAILURES+1)); fi
fi
check "the popup did not return to 100 %" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'

# title bar: drag moves on the canvas, double click returns to 100 %
CX0=$(state | jq '.windows[] | select(.caption=="Probe") | .canvasX')
read -r TX TY <<<"$(probe_point 300 $((-OY / 2)))"
input abs "$TX" "$TY" sleep 60
input drag "$TX" "$TY" $((TX + 100)) "$TY" 10 250 left sleep 400
check "dragging the title bar while zoomed out moves the window on the canvas" "(.windows[] | select(.caption==\"Probe\") | .canvasX) >= ($CX0 + 180)"

# client side title bar (browser tabs): the client asks KWin to move it, the plugin moves it on the canvas
CX0=$(state | jq '.windows[] | select(.caption=="Probe") | .canvasX')
CY0=$(state | jq '.windows[] | select(.caption=="Probe") | .canvasY')
read -r SX SY <<<"$(probe_point 300 380)"
input abs "$SX" "$SY" sleep 60
input drag "$SX" "$SY" $((SX + 100)) $((SY - 60)) 10 250 left sleep 400
if [ "$(last SYSTEMMOVE)" = true ]; then echo "PASS: the client asked to be moved (client side title bar)"; else echo "FAIL: the client did not start a move ('$(last SYSTEMMOVE)')"; FAILURES=$((FAILURES+1)); fi
echo "Probe after the client side move: $(win Probe)"
# 100 x -60 screen pixels are 200 x -120 on the canvas at 50 %, less what the client needs to see a drag
check "a client side move while zoomed out moves the window on the canvas" "(.windows[] | select(.caption==\"Probe\") | .canvasX >= ($CX0 + 120) and .canvasX <= ($CX0 + 210) and .canvasY <= ($CY0 - 60) and .canvasY >= ($CY0 - 130))"
check "the client side move is not left to KWin (window parked, drawn at the zoom)" '(.windows[] | select(.caption=="Probe") | .mode == "canvas" and .parked == true)'
check "the client side move did not return to 100 %" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
# (another point than the clicks before, so an old PRESS line cannot pass)
read -r PX PY <<<"$(probe_point 150 200)"
input abs "$PX" "$PY" sleep 60
input drag "$PX" "$PY" "$PX" "$PY" 1 40 left sleep 300
read -r CX CY _ <<<"$(last PRESS)"
echo "click at screen $PX,$PY after the move arrived at $CX,$CY (expected about 150,200)"
if [ -n "$CX" ] && [ $((CX > 150 ? CX - 150 : 150 - CX)) -le 3 ] && [ $((CY > 200 ? CY - 200 : 200 - CY)) -le 3 ]; then echo "PASS: after the client side move clicks reach the window at the right position"; else echo "FAIL: click after the client side move arrived at '$CX,$CY'"; FAILURES=$((FAILURES+1)); fi

# the same for an X11 client (Steam, Wine/Proton): KWin first warps the pointer to where the client
# thinks it is, i.e. onto the parked window at the screen edge
QT_QPA_PLATFORM=xcb /usr/lib/qt6/bin/qml /th/probe.qml > /tmp/probe-x11.log 2>&1 &
XPID=$!
for _ in $(seq 1 80); do state | grep -q '"ProbeX11"' && break; sleep 0.1; done
sleep 0.8
dbuscall setZoom 0.5; sleep 1.2      # a new window may have ended the zoom
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "ProbeX11") print("PROBEX11GEO " + (w.clientGeometry.x - w.frameGeometry.x) + " " + (w.clientGeometry.y - w.frameGeometry.y));'
read -r XOX XOY <<<"$(grep 'PROBEX11GEO' /tmp/kwin.log | tail -1 | sed 's/.*PROBEX11GEO //')"
echo "ProbeX11 before the client side move: $(win ProbeX11)"
# Xwayland keeps the pointer of X11 clients inside the screen: a click must not end at the edge
read -r PX PY <<<"$(probe_point 200 120 ProbeX11 "$XOX" "$XOY")"
input abs $((PX + 4)) "$PY" sleep 60 abs "$PX" "$PY" sleep 60
input drag "$PX" "$PY" "$PX" "$PY" 1 40 left sleep 300
read -r CX CY _ <<<"$(grep PRESS /tmp/probe-x11.log | tail -1 | sed 's/.*PRESS //')"
echo "click into ProbeX11 at screen $PX,$PY arrived at $CX,$CY (expected about 200,120)"
if [ -n "$CX" ] && [ $((CX > 200 ? CX - 200 : 200 - CX)) -le 3 ] && [ $((CY > 120 ? CY - 120 : 120 - CY)) -le 3 ]; then echo "PASS: a click into a zoomed out X11 window reaches it at the right position"; else echo "FAIL: click into the X11 window arrived at '$CX,$CY'"; FAILURES=$((FAILURES+1)); fi
check "the X11 window used zoomed out is still a canvas window, at the zoom" '(.windows[] | select(.caption=="ProbeX11") | .mode == "canvas" and .parked == true) and ([.canvases[] | select(.current)][0] | .camera.zoom == 0.5)'
CX0=$(state | jq '.windows[] | select(.caption=="ProbeX11") | .canvasX')
CY0=$(state | jq '.windows[] | select(.caption=="ProbeX11") | .canvasY')
read -r SX SY <<<"$(probe_point 300 380 ProbeX11 "$XOX" "$XOY")"
input abs "$SX" "$SY" sleep 60
input drag "$SX" "$SY" $((SX + 100)) $((SY - 60)) 10 250 left sleep 400
XMOVE=$(grep SYSTEMMOVE /tmp/probe-x11.log | tail -1 | sed 's/.*SYSTEMMOVE //')
if [ "$XMOVE" = true ]; then echo "PASS: the X11 client asked to be moved (_NET_WM_MOVERESIZE)"; else echo "FAIL: the X11 client did not start a move ('$XMOVE')"; FAILURES=$((FAILURES+1)); fi
echo "ProbeX11 after the client side move: $(win ProbeX11)"
check "an X11 client side move while zoomed out moves the window on the canvas, from where it was grabbed" "(.windows[] | select(.caption==\"ProbeX11\") | .canvasX >= ($CX0 + 120) and .canvasX <= ($CX0 + 210) and .canvasY <= ($CY0 - 60) and .canvasY >= ($CY0 - 130))"
check "the X11 client side move is not left to KWin" '(.windows[] | select(.caption=="ProbeX11") | .mode == "canvas" and .parked == true)'
kill "$XPID" 2>/dev/null
sleep 0.5

# an X11 client that moves itself with XMoveWindow (Steam, launchers under Wine/Proton): it follows
# the pointer it sees on the parked window, the plugin takes the move over
/usr/local/bin/kid-selfmove SelfMove > /tmp/selfmove.log 2>&1 &
SPID=$!
for _ in $(seq 1 80); do state | grep -q '"SelfMove"' && break; sleep 0.1; done
sleep 0.8
dbuscall setZoom 0.5; sleep 1.2      # a new window may have ended the zoom
echo "SelfMove before moving itself: $(win SelfMove)"
CX0=$(state | jq '.windows[] | select(.caption=="SelfMove") | .canvasX')
CY0=$(state | jq '.windows[] | select(.caption=="SelfMove") | .canvasY')
read -r SX SY <<<"$(probe_point 240 20 SelfMove 0 0)"
input abs "$SX" "$SY" sleep 60
input drag "$SX" "$SY" $((SX + 100)) $((SY + 60)) 10 250 left sleep 400
echo "SelfMove after the drag: $(win SelfMove) client: $(tr '\n' ' ' < /tmp/selfmove.log)"
if grep -q SELFMOVE /tmp/selfmove.log; then echo "PASS: the X11 client moved itself"; else echo "FAIL: the X11 client did not move itself"; FAILURES=$((FAILURES+1)); fi
check "a client that moves itself while zoomed out moves on the canvas, with the pointer" "(.windows[] | select(.caption==\"SelfMove\") | .canvasX >= ($CX0 + 120) and .canvasX <= ($CX0 + 210) and .canvasY >= ($CY0 + 60) and .canvasY <= ($CY0 + 130))"
check "the window that moved itself stays parked, drawn at the zoom" '(.windows[] | select(.caption=="SelfMove") | .mode == "canvas" and .parked == true)'
check "a client that moves itself did not change the zoom" '[.canvases[] | select(.current)][0] | .navigation == true and .camera.zoom == 0.5'
kill "$SPID" 2>/dev/null
sleep 0.5

read -r TX TY <<<"$(probe_point 300 $((-OY / 2)))"
input abs "$TX" "$TY" sleep 60 click left sleep 80 click left sleep 900
check "double click on the title bar returns to 100 %" '[.canvases[] | select(.current)][0] | .navigation == false and .camera.zoom == 1'
check "back at 100 % the window draws at full resolution again" '(.windows[] | select(.caption=="Probe") | .contentScale) == 0'
check "the window kept its size" "(.windows[] | select(.caption==\"Probe\") | .frame.width) == $W0"

# ---------------------------------------------------------------- markers for windows outside the view
dbuscall goHome; sleep 0.4
foot --title Far --app-id kid.Far --window-size-chars 40x10 -e sleep 600 >/dev/null 2>&1 &
for _ in $(seq 1 50); do state | grep -q '"Far"' && break; sleep 0.1; done
sleep 0.5
kwin_js 'for (const w of workspace.windowList()) if (w.caption == "Far") w.frameGeometry = {x: -2600, y: 380, width: w.frameGeometry.width, height: w.frameGeometry.height};'
sleep 0.4
dbuscall pan 4 0; sleep 0.5          # some navigation: the markers appear
shot 02-markers
MARKER=$(python3 - /out/02-markers.png <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load(); w, h = im.size
pts = [(x, y) for y in range(h) for x in range(0, 80) if 18 <= min(px[x, y]) and max(px[x, y]) <= 60]
if pts:
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    print(f"{(min(xs) + max(xs)) // 2} {(min(ys) + max(ys)) // 2} {max(xs) - min(xs)}")
EOF
)
read -r MX MY MW <<<"$MARKER"
echo "marker at the left edge: ${MX:-none},${MY:-} (width ${MW:-0})"
if [ -n "$MX" ] && [ "$MW" -ge 30 ]; then echo "PASS: a marker points to the window outside the view"; else echo "FAIL: no marker for the window outside the view"; FAILURES=$((FAILURES+1)); fi
if [ -n "$MX" ]; then
  input abs "$MX" "$MY" sleep 60 click left sleep 1200
  check "clicking the marker brings the window into view" '(.windows[] | select(.caption=="Far") | .parked) == false'
fi

# ---------------------------------------------------------------- circling the mouse does not send the canvas gliding
dbuscall goHome; sleep 0.4
CAM0=$(state | jq -c '[.canvases[] | select(.current)][0].camera')
# press on the empty desktop, two circles, release while still moving (one input device)
CIRCLE=(abs 310 700 sleep 50 down left)
for i in $(seq 0 64); do
  read -r x y <<<"$(awk -v i="$i" 'BEGIN { printf "%d %d", 220 + 90 * cos(i * 0.19635), 700 + 90 * sin(i * 0.19635) }')"
  CIRCLE+=(abs "$x" "$y" sleep 8)
done
CIRCLE+=(up left sleep 800)
input "${CIRCLE[@]}"
CAM1=$(state | jq -c '[.canvases[] | select(.current)][0].camera')
echo "camera after circling: $CAM0 -> $CAM1"
check "no glide after circling the mouse" '[.canvases[] | select(.current)][0] | .animating == false'
DRIFT=$(jq -n --argjson a "$CAM0" --argjson b "$CAM1" '(($a.x - $b.x) | fabs) + (($a.y - $b.y) | fabs)')
if [ "$(jq -n --argjson d "$DRIFT" '$d < 120')" = true ]; then echo "PASS: the canvas stays near where the circles left it ($DRIFT px)"; else echo "FAIL: the canvas drifted $DRIFT px after circling"; FAILURES=$((FAILURES+1)); fi

check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
