#!/bin/bash
# Stress and robustness: random navigation while windows open/close, clients
# killed while parked, click during a glide.
source /th/lib.sh
FAILURES=0
input abs 800 450

for n in One Two Three Four Five Six; do open_term "$n" 40 10 || exit 1; done
sleep 0.5

echo "--- random navigation with windows coming and going"
RANDOM=42
for i in $(seq 1 40); do
  case $((RANDOM % 7)) in
    0) dbuscall pan $((RANDOM % 4000 - 2000)) $((RANDOM % 3000 - 1500)) ;;
    1) dbuscall setZoom "0.$((RANDOM % 9 + 1))" ;;
    2) dbuscall resetZoom ;;
    3) dbuscall zoomToFit ;;
    4) dbuscall goHome ;;
    5) victim=$(state | jq -r '[.windows[] | select(.mode=="canvas") | .caption] | .[0] // empty'); [ -n "$victim" ] && pkill -f "title $victim" ;;
    6) open_term "Extra$i" 30 8 >/dev/null 2>&1 & ;;
  esac
  sleep 0.05
done
sleep 1
check "still operational after random navigation" '.operational == true'

echo "--- kill clients while their windows are parked"
dbuscall resetZoom; sleep 0.2
dbuscall pan 6000 0; sleep 0.3
state | jq -c '[.windows[] | select(.mode=="canvas") | {caption, parked}]'
pkill -f "title Extra" ; pkill -f "title Six"
sleep 1
dbuscall goHome; sleep 0.3
check "no parked window left behind after goHome" '[.windows[] | select(.mode=="canvas" and .parked)] | length == 0'

echo "--- a click during a glide only stops the glide"
open_term Target 50 12 || exit 1
sleep 0.3
# fast drag on the empty top-left corner → glide
input drag 30 30 400 40 4 40 left
input sleep 30 abs 800 450 click left sleep 200
check "the glide was stopped and committed" '[.canvases[] | select(.current)][0] | (.animating == false and .camera.x == .committed.x)'
sleep 0.5
dbuscall goHome; sleep 0.3

echo "--- zoom out, open a window (returns to 100 %), zoom out again, close everything"
dbuscall setZoom 0.3; sleep 0.2
open_term Late 30 8 || exit 1
sleep 0.5
check "a new window returned the canvas to 100 %" '[.canvases[] | select(.current)][0] | .camera.zoom == 1 and .navigation == false'
dbuscall setZoom 0.3; sleep 0.2
pkill -f "sleep 600"
sleep 1.5
dbuscall resetZoom; sleep 0.3
check "everything closed, plugin operational" '.operational == true and ([.windows[] | select(.mode=="canvas")] | length == 0)'
check "KWin still running" '.version != null'
echo "FAILURES=$FAILURES"
exit $FAILURES
