#!/bin/bash
# KDE Infinit Desktop - records the images and animations of the documentation from a
# real Plasma session (KWin on a virtual output, plasmashell, real applications).
# Runs inside the demo image; started by record.ps1 / record.sh.
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
log() { echo "[media] $*"; }

# ---------------------------------------------------------------- build & install
log "building KDE Infinit Desktop and the input helper ..."
if ! { cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
            -DKDE_INSTALL_USE_QT_SYS_PATHS=ON -DBUILD_TESTING=OFF > /tmp/configure.log 2>&1 &&
        cmake --build /build > /tmp/build.log 2>&1 && cmake --install /build > /dev/null 2>&1; }; then
    grep -E "error|Error" /tmp/configure.log /tmp/build.log | head -30
    exit 1
fi
if ! { cmake -S /src/tests/runtime -B /thbuild -G Ninja > /dev/null 2>&1 && cmake --build /thbuild > /tmp/th.log 2>&1 &&
        cmake --install /thbuild --prefix /usr/local > /dev/null 2>&1; }; then
    tail -20 /tmp/th.log
    exit 1
fi

# ---------------------------------------------------------------- environment
export XDG_RUNTIME_DIR=/tmp/xdg HOME=/home/demo SHELL=/bin/bash
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.config" "$HOME/Videos" && chmod 700 "$XDG_RUNTIME_DIR"
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_STATE_HOME=$HOME/.local/state
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 QT_FORCE_STDERR_LOGGING=1
case "${MEDIA_LANG:-de}" in
    de) export LANG=de_DE.UTF-8 LANGUAGE=de; KEYBOARD=de ;;
    *) export LANG=en_US.UTF-8 LANGUAGE=en_US; KEYBOARD=us ;;
esac
kwriteconfig6 --file plasma-localerc --group Formats --key LANG "$LANG"
kwriteconfig6 --file plasma-localerc --group Translations --key LANGUAGE "$LANGUAGE"
kwriteconfig6 --file kxkbrc --group Layout --key Use true
kwriteconfig6 --file kxkbrc --group Layout --key LayoutList "$KEYBOARD"
kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled true
kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false
# Scenes move the camera step by step; animations would only blur the frames.
kwriteconfig6 --file kwinrc --group Effect-infinitdesktop --key AnimationDuration 0
kwriteconfig6 --file kwinrc --group Effect-infinitdesktop --key ZoomIndicator false

cat > "$HOME/Notizen.md" <<'EOF'
# Notizen

- Fenster frei auf der Arbeitsfläche verteilen
- Herauszoomen für den Überblick
- Wichtiges am Bildschirm anpinnen
EOF
if [ ! -f "$HOME/Videos/Testvideo.mp4" ] && command -v ffmpeg >/dev/null; then
    bash /src/tools/demo/make-testvideo.sh "$HOME/Videos/Testvideo.mp4" 60 || log "WARNING: no test video"
fi

# ---------------------------------------------------------------- session
QT_QUICK_BACKEND=software kwin_wayland --virtual --width 1920 --height 1080 --xwayland --no-lockscreen > /tmp/kwin.log 2>&1 &
for _ in $(seq 1 150); do
    [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 && break
    sleep 0.1
done
export WAYLAND_DISPLAY=wayland-0
sleep 1
/usr/lib/kactivitymanagerd > /dev/null 2>&1 &
kded6 > /dev/null 2>&1 &
sleep 1
plasmashell > /tmp/plasmashell.log 2>&1 &
sleep 6
log "plugin loaded: $(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded infinitdesktop)"

python3 /media/scenes.py "${MEDIA_OUT:-/out}" "${MEDIA_SCENES:-all}"
rc=$?
log "done (exit $rc)"
exit $rc
