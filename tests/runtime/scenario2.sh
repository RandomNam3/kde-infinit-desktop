#!/bin/bash
# Two outputs side by side (Virtual-0 at x=0, Virtual-1 at x=1600):
# independent canvases, clipping at the screen edge, slow drag vs. fling.
source /th/lib.sh
FAILURES=0

state | jq -c '.outputs[] | {name, geometry}'
input abs 800 450
open_term Alpha 60 15 || exit 1
input abs 2400 450
open_term Beta 50 12 || exit 1
sleep 1
win "Alpha|Beta"
check "Alpha lives on the canvas of Virtual-0" '(.windows[] | select(.caption=="Alpha") | .canvas) | startswith("Virtual-0")'
check "Beta lives on the canvas of Virtual-1" '(.windows[] | select(.caption=="Beta") | .canvas) | startswith("Virtual-1")'
BX0=$(state | jq '.windows[] | select(.caption=="Beta") | .frame.x')
shot 01-two-screens

# --- pan only the left screen: Alpha moves right towards the edge, Beta stays.
input abs 200 800
dbuscall pan -700 0
sleep 0.4
win "Alpha|Beta"
check "Beta untouched by panning the other screen" "(.windows[] | select(.caption==\"Beta\") | .frame.x) == $BX0"
check "Alpha straddles the screen edge (physically on both)" '(.windows[] | select(.caption=="Alpha") | .frame) | (.x < 1600 and (.x + .width) > 1600)'
shot 02-clipped-at-edge

# --- pointer over the hidden part on the right screen: Alpha gets parked (cannot steal input there)
AY=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.y + 100')
input abs 1620 "$AY" sleep 100 rel 5 0 sleep 300
win "Alpha"
check "hidden overlap resolved: Alpha parked" '(.windows[] | select(.caption=="Alpha") | .parked) == true'
shot 03-overlap-parked

# --- back home on the left screen
input abs 200 800
dbuscall goHome
sleep 0.4
win "Alpha"
check "Alpha back on the left screen, not parked" '(.windows[] | select(.caption=="Alpha")) | (.parked == false and (.frame.x + .frame.width) <= 1600)'

# --- slow drag of Alpha's title bar against the right edge: stays on Virtual-0, the canvas scrolls
TX=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x + .frame.width / 2 | floor')
TY=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.y + 14 | floor')
CX0=$(state | jq '[.canvases[] | select(.current and (.key | startswith("Virtual-0")))][0].camera.x')
input drag "$TX" "$TY" 1900 "$TY" 60 2400 left
sleep 1
win "Alpha"
CX1=$(state | jq '[.canvases[] | select(.current and (.key | startswith("Virtual-0")))][0].camera.x')
echo "left camera x during slow drag: $CX0 -> $CX1"
check "slow drag: Alpha stays on the canvas of Virtual-0" '(.windows[] | select(.caption=="Alpha") | .canvas) | startswith("Virtual-0")'
check "slow drag: Alpha did not cross the screen edge" '(.windows[] | select(.caption=="Alpha") | .frame.x) < 1600'
check "slow drag: edge scrolling moved the left canvas" "([.canvases[] | select(.current and (.key | startswith(\"Virtual-0\")))][0].camera.x) > $CX0"
check "slow drag: KWin did not quick-tile the window at the edge" '(.windows[] | select(.caption=="Alpha") | .frame.width) == 360'
shot 04-after-slow-drag

# --- fast fling: Alpha moves over to Virtual-1
TX=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x + .frame.width / 2 | floor')
TY=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.y + 14 | floor')
input drag "$TX" "$TY" 2300 "$TY" 8 90 left
sleep 1
win "Alpha"
check "fling: Alpha moved to the canvas of Virtual-1" '(.windows[] | select(.caption=="Alpha") | .canvas) | startswith("Virtual-1")'
check "fling: Alpha is physically on Virtual-1" '(.windows[] | select(.caption=="Alpha") | .frame.x) >= 1600'
shot 05-after-fling

# --- panning the right screen now moves Alpha and Beta together
input abs 3000 800
AX0=$(state | jq '.windows[] | select(.caption=="Alpha") | .frame.x')
dbuscall pan 100 0
sleep 0.4
check "Alpha now follows the right canvas" "(.windows[] | select(.caption==\"Alpha\") | .frame.x) == ($AX0 - 100)"

check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
