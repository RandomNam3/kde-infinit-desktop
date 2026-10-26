#!/bin/bash
# Notes (with the companion app), groups, areas, the map and the canvas menu.
source /th/lib.sh
FAILURES=0
NOTES=$HOME/.local/share/kde-infinite-desktop/notes
KEY_ESC=1 KEY_E=18 KEY_H=35 KEY_A=30 KEY_L=38 KEY_O=24 KEY_LEFTCTRL=29 KEY_LEFTMETA=125
companion() { qdbus6 org.kde.InfiniteDesktop.Companion /Companion "org.kde.InfiniteDesktop.Companion.$1" "${@:2}"; }
note_id() { jq -r '.notes[0].id' "$NOTES/index.json" 2>/dev/null; }
# Popup windows (menus) KWin knows of, printed by a KWin script into its log.
popups() {
  local tag="POPUPS$RANDOM"
  kwin_js "print('$tag ' + workspace.windowList().filter(w => w.popupWindow).length);"
  grep -o "$tag [0-9]*" /tmp/kwin.log | tail -1 | cut -d' ' -f2
}
input abs 800 450

echo "--- notes: the companion announces its window, the canvas places it"
kde-infinite-desktop-companion > /tmp/companion.log 2>&1 &
for _ in $(seq 1 100); do qdbus6 org.kde.InfiniteDesktop.Companion >/dev/null 2>&1 && break; sleep 0.1; done
input abs 400 300
dbuscall newNote
for _ in $(seq 1 100); do state | jq -e '[.windows[] | select(.note != "")] | length > 0' >/dev/null && break; sleep 0.1; done
sleep 1
check "a new note window appears as a note on the canvas" '[.windows[] | select(.note != "")] | length == 1 and .[0].mode == "canvas"'
check "the note opens at the pointer" '[.windows[] | select(.note != "")][0].frame | (.x <= 400 and .x + .width >= 400 and .y <= 330 and .y + .height >= 300)'
shot 01-new-note

echo "--- notes: typing goes into the Markdown source, Ctrl+E shows it formatted"
input key $KEY_H key $KEY_A key $KEY_L key $KEY_L key $KEY_O sleep 1500
ID=$(note_id)
if [ -n "$ID" ] && grep -qi "hallo" "$NOTES/$ID.md" 2>/dev/null; then echo "PASS: the note opened in edit mode and saved what was typed"; else echo "FAIL: typed text not in the note file ('$(cat "$NOTES/$ID.md" 2>/dev/null)')"; FAILURES=$((FAILURES+1)); fi
input keydown $KEY_LEFTCTRL key $KEY_E keyup $KEY_LEFTCTRL sleep 1500
if jq -e '.notes[0].formatted == true' "$NOTES/index.json" >/dev/null 2>&1; then echo "PASS: Ctrl+E shows the note formatted (stored per note)"; else echo "FAIL: formatted not stored: $(cat "$NOTES/index.json")"; FAILURES=$((FAILURES+1)); fi
shot 02-formatted
input keydown $KEY_LEFTCTRL key $KEY_E keyup $KEY_LEFTCTRL sleep 1500
if jq -e '.notes[0].formatted == false' "$NOTES/index.json" >/dev/null 2>&1; then echo "PASS: Ctrl+E again goes back to editing"; else echo "FAIL: still formatted"; FAILURES=$((FAILURES+1)); fi

echo "--- notes: right click opens the note's own menu"
read -r NX NY < <(state | jq -r '[.windows[] | select(.note != "")][0].frame | "\((.x + 60) | floor) \((.y + 90) | floor)"')
BEFORE=$(popups)
input abs "$NX" "$NY" sleep 100 click right sleep 1000
AFTER=$(popups)
if [ "${AFTER:-0}" -gt "${BEFORE:-0}" ]; then echo "PASS: right click in the text opened a menu"; else echo "FAIL: no menu after right click ($BEFORE -> $AFTER)"; FAILURES=$((FAILURES+1)); fi
shot 03-note-menu
input key $KEY_ESC sleep 500
if [ "$(popups)" = "${BEFORE:-0}" ]; then echo "PASS: Escape closed the menu"; else echo "FAIL: the menu stayed open"; FAILURES=$((FAILURES+1)); fi

echo "--- notes: pinning over D-Bus"
dbuscall setNotePinned "$ID" true false >/dev/null; sleep 0.5
check "a pinned note is pinned to the screen" '[.windows[] | select(.note != "")][0].mode == "pinned"'
dbuscall pan 500 0 >/dev/null; sleep 0.3
check "a pinned note does not move with the canvas" '[.windows[] | select(.note != "")][0].frame.x <= 400'
dbuscall setNotePinned "$ID" false false >/dev/null; sleep 0.5
check "released, it is a canvas window again" '[.windows[] | select(.note != "")][0].mode == "canvas"'
dbuscall goHome >/dev/null; sleep 0.3
companion setNotesVisible false; sleep 1
check "hidden notes have no window" '[.windows[] | select(.note != "")] | length == 0'
if grep -qiE "ReferenceError|TypeError|is not defined|Cannot assign|Unable to assign" /tmp/companion.log; then echo "FAIL: QML errors in the companion:"; grep -iE "Error|Cannot|Unable" /tmp/companion.log | head -5; FAILURES=$((FAILURES+1)); else echo "PASS: no QML errors in the companion"; fi

echo "--- groups: opening a group puts its window at the group's place"
open_term Alpha 50 12 || exit 1
sleep 0.5
APP=$(state | jq -r '.windows[] | select(.caption=="Alpha") | .app')
read -r AX AY AW AH < <(state | jq -r '.windows[] | select(.caption=="Alpha") | "\(.canvasX) \(.canvasY) \(.frame.width) \(.frame.height)"')
cat > /tmp/groups.json <<EOF
{"version": 1, "groups": [{"id": "g1", "name": "Work", "x": $(( ${AX%.*} + 1500 )), "y": ${AY%.*}, "layout": "manual",
  "members": [{"app": "$APP", "launch": "", "geometry": {"x": 0, "y": 0, "width": ${AW%.*}, "height": ${AH%.*}}}]}]}
EOF
dbuscall setGroups "$(cat /tmp/groups.json)" >/dev/null
dbuscall openGroup g1 >/dev/null; sleep 1
check "the window belongs to the group" '(.windows[] | select(.caption=="Alpha") | .group) == "g1"'
check "the window is at the group's place" "(.windows[] | select(.caption==\"Alpha\") | .canvasX) == $(( ${AX%.*} + 1500 ))"
check "the camera followed the group" '(.windows[] | select(.caption=="Alpha") | .parked) == false'
if dbuscall groups | jq -e '.groups[0].openWindows == 1' >/dev/null; then echo "PASS: groups() counts the open window"; else echo "FAIL: groups(): $(dbuscall groups)"; FAILURES=$((FAILURES+1)); fi

echo "--- areas: a dynamic area takes in a new window and fits itself around it"
dbuscall goHome >/dev/null; sleep 0.3
# Canvas key: "<output>|<desktop id or *>"; the area belongs to that output and desktop.
read -r OUTPUT DESKTOP < <(state | jq -r '[.canvases[] | select(.current)][0].key | split("|") | "\(.[0]) \(.[1])"')
read -r CX CY < <(state | jq -r '[.canvases[] | select(.current)][0].camera | "\(.x | floor) \(.y | floor)"')
cat > /tmp/areas.json <<EOF
{"version": 1, "areas": [{"id": "a1", "name": "Projekt", "output": {"name": "$OUTPUT"}, "desktop": "$DESKTOP",
  "drawn": {"x": $((CX - 700)), "y": $((CY - 400)), "width": 1400, "height": 800},
  "rect": {"x": $((CX - 700)), "y": $((CY - 400)), "width": 1400, "height": 800}, "dynamic": true, "color": 1}]}
EOF
dbuscall setAreas "$(cat /tmp/areas.json)" >/dev/null; sleep 0.5
open_term Beta 40 10 || exit 1
sleep 1
check "a window that opens inside the area belongs to it" '(.windows[] | select(.caption=="Beta") | .area) == "a1"'
read -r BW < <(state | jq -r '.windows[] | select(.caption=="Beta") | .frame.width | floor')
if dbuscall areas | jq -e ".areas[0].rect.width < 1400 and .areas[0].rect.width >= $BW" >/dev/null; then echo "PASS: the dynamic area fits itself around its window"; else echo "FAIL: areas(): $(dbuscall areas | jq -c '.areas[0].rect')"; FAILURES=$((FAILURES+1)); fi
dbuscall setZoom 0.5 >/dev/null; sleep 0.5
shot 04-area-zoomed-out
dbuscall resetZoom >/dev/null; sleep 0.3

echo "--- the map: opens, closes with Escape and frees its textures"
dbuscall showMap >/dev/null; sleep 0.5
check "the map is open" '.organizer.overlay == "map"'
shot 05-map
input key $KEY_ESC sleep 300
check "Escape closed the map" '.organizer.overlay == "none"'
check "no overlay textures left after closing" '.organizer.menuTexture == false and .organizer.labelTextures == 0 and .organizer.iconTextures == 0'

echo "--- the canvas menu: Meta + right click on the free desktop"
input abs 1300 750 keydown $KEY_LEFTMETA sleep 50 click right sleep 50 keyup $KEY_LEFTMETA sleep 500
check "Meta + right click opened the canvas menu" '.organizer.overlay == "menu"'
shot 06-canvas-menu
input key $KEY_ESC sleep 300
check "Escape closed the menu and freed its texture" '.organizer.overlay == "none" and .organizer.menuTexture == false'

companion quit >/dev/null 2>&1
pkill -f "sleep 600"
sleep 1
check "plugin still operational" '.operational == true'
echo "FAILURES=$FAILURES"
exit $FAILURES
