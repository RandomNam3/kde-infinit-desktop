#!/bin/bash
# KDE Infinit Desktop - demo session inside the container.
# Builds the plugin from /src, starts KWin (virtual output) + plasmashell and
# serves the session over RDP with KRdp.
# SPDX-License-Identifier: GPL-3.0-or-later
set -u

WIDTH=${DEMO_WIDTH:-1920}
HEIGHT=${DEMO_HEIGHT:-1080}
OUTPUTS=${DEMO_OUTPUTS:-1}
RDP_USER=${DEMO_USER:-kde}
# Empty: no login (the start scripts publish the port on 127.0.0.1 only).
RDP_PASSWORD=${DEMO_PASSWORD:-}

log() { echo "[demo] $*"; }

# ---------------------------------------------------------------- build & install
build() {
    cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
          -DKDE_INSTALL_USE_QT_SYS_PATHS=ON -DBUILD_TESTING=OFF "$@" > /tmp/configure.log 2>&1 &&
    cmake --build /build > /tmp/build.log 2>&1 &&
    cmake --install /build > /tmp/install.log 2>&1
}
if [ -d /src ]; then
    log "building KDE Infinit Desktop from /src ..."
    # shellcheck disable=SC2086
    if build ${DEMO_CMAKE_ARGS:-}; then
        log "plugin, settings app and System Settings module installed"
    elif build -DBUILD_KCM=OFF -DBUILD_SETTINGS_APP=OFF; then
        log "WARNING: settings UIs did not build, only the KWin plugin is installed"
    else
        log "build failed:"
        grep -E "error|Error" /tmp/configure.log /tmp/build.log | head -40
        exit 1
    fi
fi

# ---------------------------------------------------------------- environment
export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export HOME=/home/demo
mkdir -p "$HOME/.config" "$HOME/Desktop" "$HOME/Documents"
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_STATE_HOME=$HOME/.local/state
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export QT_FORCE_STDERR_LOGGING=1
export QT_LOGGING_RULES="kwin_effect_infinitdesktop.debug=true"
export SHELL=/bin/bash

# Language, formats and keyboard layout of the session (DEMO_LANG=de or en).
case "${DEMO_LANG:-de}" in
    de) LOCALE=de_DE.UTF-8; LANGUAGE_LIST=de; KEYBOARD=de ;;
    *) LOCALE=en_US.UTF-8; LANGUAGE_LIST=en_US; KEYBOARD=us ;;
esac
export LANG=$LOCALE LANGUAGE=$LANGUAGE_LIST
kwriteconfig6 --file plasma-localerc --group Formats --key LANG "$LOCALE"
kwriteconfig6 --file plasma-localerc --group Translations --key LANGUAGE "$LANGUAGE_LIST"
kwriteconfig6 --file kxkbrc --group Layout --key Use true
kwriteconfig6 --file kxkbrc --group Layout --key LayoutList "$KEYBOARD"

# A test video for fullscreen (mpv --fs): moving colours, a title and a time code.
if [ ! -f "$HOME/Videos/Testvideo.mp4" ] && command -v ffmpeg >/dev/null; then
    bash "$(dirname "$0")/make-testvideo.sh" "$HOME/Videos/Testvideo.mp4" 120 || log "WARNING: could not create the test video"
fi

if [ ! -f "$HOME/.config/kwinrc" ]; then
    kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled true
    kwriteconfig6 --file kwinrc --group Effect-infinitdesktop --key DebugLogging true
    # The virtual session is software rendered: keep the wobbly bits light.
    kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false
fi

# ---------------------------------------------------------------- PipeWire (screen casting for KRdp)
pipewire > /tmp/pipewire.log 2>&1 &
sleep 0.5
wireplumber > /tmp/wireplumber.log 2>&1 &

# ---------------------------------------------------------------- KWin
# KWin's internal QtQuick windows (OSD) cannot get a GL context on llvmpipe+vgem.
QT_QUICK_BACKEND=software kwin_wayland --virtual --width "$WIDTH" --height "$HEIGHT" --output-count "$OUTPUTS" \
    --xwayland --no-lockscreen > /tmp/kwin.log 2>&1 &
KWIN_PID=$!
for _ in $(seq 1 150); do
    [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 && break
    sleep 0.1
done
export WAYLAND_DISPLAY=wayland-0
sleep 1
if ! qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded infinitdesktop 2>/dev/null | grep -q true; then
    log "WARNING: the effect is not loaded. Compositing information:"
    qdbus6 org.kde.KWin /KWin org.kde.KWin.supportInformation | grep -iE "Compositing|OpenGL" | head -8
fi

# ---------------------------------------------------------------- Plasma shell + a few apps
for daemon in /usr/lib/kactivitymanagerd /usr/bin/kactivitymanagerd; do
    [ -x "$daemon" ] && { "$daemon" > /tmp/kactivitymanagerd.log 2>&1 & break; }
done
kded6 > /tmp/kded.log 2>&1 &
sleep 1
plasmashell > /tmp/plasmashell.log 2>&1 &
sleep 4
konsole > /dev/null 2>&1 &
sleep 1
dolphin "$HOME" > /dev/null 2>&1 &
sleep 1
kate > /dev/null 2>&1 &

# ---------------------------------------------------------------- RDP
# Environment for "podman exec kid-demo bash -c '. /tmp/demo-env; qdbus6 ...'".
cat > /tmp/demo-env <<EOF
export DBUS_SESSION_BUS_ADDRESS='$DBUS_SESSION_BUS_ADDRESS'
export XDG_RUNTIME_DIR='$XDG_RUNTIME_DIR' WAYLAND_DISPLAY='$WAYLAND_DISPLAY' HOME='$HOME'
export XDG_CONFIG_HOME='$XDG_CONFIG_HOME' XDG_STATE_HOME='$XDG_STATE_HOME'
EOF
# KRdp needs a TLS certificate; a self-signed one is fine for a local demo.
if [ ! -f /tmp/krdp.crt ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout /tmp/krdp.key -out /tmp/krdp.crt -days 30 \
        -subj "/CN=kde-infinit-desktop-demo" > /dev/null 2>&1
fi
if [ -n "$RDP_PASSWORD" ]; then
    # Fixed user and password, checked with NLA (NTLM).
    log "starting RDP server on port 3389 (user: $RDP_USER, password: $RDP_PASSWORD)"
    login_args=(-u "$RDP_USER" -p "$RDP_PASSWORD")
else
    # No login. KRdp's system user mode uses TLS without NLA, so no NTLM is involved
    # (Windows may have NTLM disabled), and checks the user "root" through the PAM
    # service "login", which this throwaway container allows for everyone.
    cat > /etc/pam.d/login <<'EOF'
#%PAM-1.0
# KDE Infinit Desktop demo container: the RDP port is published on 127.0.0.1 only.
auth     sufficient pam_permit.so
account  sufficient pam_permit.so
session  sufficient pam_permit.so
password sufficient pam_permit.so
EOF
    kwriteconfig6 --file krdpserverrc --group General --key SystemUserEnabled true
    log "starting RDP server on port 3389 (no login, user name: root)"
    login_args=()
fi
krdpserver --plasma "${login_args[@]}" --port 3389 --quality 90 \
    --certificate /tmp/krdp.crt --certificate-key /tmp/krdp.key > /tmp/krdp.log 2>&1 &
sleep 3
if ! kill -0 $! 2>/dev/null; then
    log "WARNING: the RDP server stopped:"
    tail -5 /tmp/krdp.log
fi
log "ready: connect with an RDP client to localhost:${DEMO_HOST_PORT:-13389}"
log "logs: podman exec kid-demo tail -f /tmp/kwin.log | /tmp/krdp.log | /tmp/plasmashell.log"

wait $KWIN_PID
log "KWin exited:"
tail -30 /tmp/kwin.log
