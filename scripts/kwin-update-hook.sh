#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Automatic rebuild of KDE Infinite Desktop after KWin updates (DKMS style).
#
# Installed by scripts/install.sh to <prefix>/lib/kde-infinite-desktop/ and
# called by the package manager hooks in scripts/hooks/ (pacman, apt, dnf,
# zypper, apk) and - as a fallback on every system - by the login check.
#
# KWin only loads plugins built for exactly its version (the plugin IID
# contains MAJOR.MINOR.PATCH), so every KWin patch release needs a rebuild.
# This script compares <prefix>/share/kde-infinite-desktop/built-for-kwin with
# the installed KWin and, if they differ, rebuilds from the source copy in
# /usr/src/kde-infinite-desktop-<version> (no network needed).
#
# It NEVER fails a package transaction: it always exits 0 and logs to
# /var/log/kde-infinite-desktop-rebuild.log.

set -u

readonly PROJECT=kde-infinite-desktop
LOG=${KID_REBUILD_LOG:-/var/log/kde-infinite-desktop-rebuild.log}
STATE_DIR=${KID_STATE_DIR:-/var/lib/kde-infinite-desktop}
LOCK_FILE=/run/kde-infinite-desktop-rebuild.lock

SELF=$(readlink -f -- "${BASH_SOURCE[0]}" 2>/dev/null || printf '%s' "${BASH_SOURCE[0]}")
HERE=$(dirname -- "$SELF")

TRIGGER=manual
WAIT_LOCK=0
BACKGROUND=0
FORCE=0
NOTIFY=1

usage() {
    cat <<EOF
Usage: kwin-update-hook.sh [OPTIONS] [TRIGGER]

Rebuilds and reinstalls KDE Infinite Desktop if the installed KWin version
differs from the one the plugin was built for. Run as root.

TRIGGER is informational: pacman, apt, dnf, zypper, apk, login or manual.

Options:
  --wait-lock    wait until no package manager is running any more
  --background   detach and return immediately (used by zypper/apk hooks)
  --force        rebuild even if the versions match
  --no-notify    do not send desktop notifications
  -h, --help     show this help
Log: $LOG
EOF
}

while [ $# -gt 0 ]; do
    case $1 in
        --wait-lock) WAIT_LOCK=1 ;;
        --background) BACKGROUND=1 ;;
        --force) FORCE=1 ;;
        --no-notify) NOTIFY=0 ;;
        -h | --help)
            usage
            exit 0
            ;;
        -*) ;; # unknown options are ignored: never break a transaction
        *) TRIGGER=$1 ;;
    esac
    shift
done

have() { command -v "$1" >/dev/null 2>&1; }

log() {
    local line
    line="$(date '+%Y-%m-%d %H:%M:%S') [$TRIGGER] $*"
    { printf '%s\n' "$line" >>"$LOG"; } 2>/dev/null || true
    # pacman/apt/dnf show the hook output - keep it to one useful line
    [ "$BACKGROUND" -eq 1 ] || printf 'kde-infinite-desktop: %s\n' "$*" >&2
}

if [ "$(id -u)" -ne 0 ]; then
    echo "kwin-update-hook.sh must run as root (e.g. sudo $SELF)" >&2
    exit 0
fi

# Detach from the package manager (zypper plugins and apk hooks must return).
if [ "$BACKGROUND" -eq 1 ] && [ -z "${KID_HOOK_DETACHED:-}" ]; then
    args=(--wait-lock "$TRIGGER")
    [ "$FORCE" -eq 1 ] && args+=(--force)
    [ "$NOTIFY" -eq 0 ] && args+=(--no-notify)
    if have setsid; then
        KID_HOOK_DETACHED=1 setsid -f bash "$SELF" --background "${args[@]}" </dev/null >/dev/null 2>&1 || true
    else
        KID_HOOK_DETACHED=1 nohup bash "$SELF" --background "${args[@]}" </dev/null >/dev/null 2>&1 &
    fi
    exit 0
fi

# ------------------------------------------------------------- helpers --
info_value() {
    # info_value KEY: value from install-info of the installation
    local f
    for f in "$HERE/../../share/$PROJECT/install-info" /usr/share/$PROJECT/install-info /usr/local/share/$PROJECT/install-info; do
        if [ -r "$f" ]; then
            sed -n "s/^$1=//p" "$f" | head -n1
            return 0
        fi
    done
    return 1
}

find_source() {
    local src d
    src=$(info_value source_dir 2>/dev/null || true)
    if [ -n "$src" ] && [ -r "$src/CMakeLists.txt" ] && [ -r "$src/scripts/install.sh" ]; then
        printf '%s\n' "$src"
        return 0
    fi
    src=''
    for d in "/usr/src/$PROJECT"-*/; do
        [ -r "$d/CMakeLists.txt" ] && [ -r "$d/scripts/install.sh" ] && src=${d%/}
    done
    [ -n "$src" ] && printf '%s\n' "$src"
}

package_manager_busy() {
    local p
    for p in pacman apt apt-get dpkg dnf dnf5 yum zypper rpm apk xbps-install emerge eopkg; do
        if have pgrep && pgrep -x "$p" >/dev/null 2>&1; then
            return 0
        fi
    done
    [ -e /var/lib/pacman/db.lck ] && return 0
    return 1
}

wait_for_package_manager() {
    local i
    for ((i = 0; i < 360; i++)); do # max. 30 minutes
        package_manager_busy || return 0
        sleep 5
    done
    log "package manager still busy after 30 minutes - rebuilding anyway"
}

# notify_users TITLE BODY: desktop notification in every graphical session
notify_users() {
    [ "$NOTIFY" -eq 1 ] || return 0
    local title=$1 body=$2 dir uid user bus
    for dir in /run/user/[0-9]*; do
        [ -S "$dir/bus" ] || continue
        uid=${dir##*/}
        [ "$uid" -ge 1000 ] 2>/dev/null || continue
        user=$(id -nu "$uid" 2>/dev/null) || continue
        bus="unix:path=$dir/bus"
        if have runuser && have notify-send; then
            runuser -u "$user" -- env DBUS_SESSION_BUS_ADDRESS="$bus" \
                notify-send --app-name="KDE Infinite Desktop" --icon=org.kde.infinitedesktop "$title" "$body" >/dev/null 2>&1 && continue
        fi
        if have runuser && have gdbus; then
            runuser -u "$user" -- env DBUS_SESSION_BUS_ADDRESS="$bus" \
                gdbus call --session --dest org.freedesktop.Notifications \
                --object-path /org/freedesktop/Notifications \
                --method org.freedesktop.Notifications.Notify \
                "KDE Infinite Desktop" 0 kde-infinite-desktop "$title" "$body" '[]' '{}' 0 >/dev/null 2>&1 && continue
        fi
        if have su && have notify-send; then
            su -s /bin/sh "$user" -c "DBUS_SESSION_BUS_ADDRESS='$bus' notify-send --app-name='KDE Infinite Desktop' '$title' '$body'" >/dev/null 2>&1 || true
        fi
    done
}

write_state() {
    mkdir -p "$STATE_DIR" 2>/dev/null || return 0
    {
        printf 'status=%s\n' "$1"
        printf 'kwin=%s\n' "$2"
        printf 'date=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } >"$STATE_DIR/last-rebuild" 2>/dev/null || true
}

# --------------------------------------------------------------- main --
main() {
    local check="$HERE/check-kwin-update.sh" rc built current src
    if [ ! -r "$check" ]; then
        log "check-kwin-update.sh not found next to $SELF - nothing to do"
        return 0
    fi

    [ "$WAIT_LOCK" -eq 1 ] && wait_for_package_manager

    built=$(bash "$check" --print-built-for 2>/dev/null || true)
    current=$(bash "$check" --print-installed-kwin 2>/dev/null || true)
    if [ -z "$built" ]; then
        # not installed (any more) - e.g. hook left over after a manual removal
        return 0
    fi
    if [ -z "$current" ]; then
        log "KWin seems to be removed - nothing to rebuild"
        return 0
    fi
    if [ "$built" = "$current" ] && [ "$FORCE" -eq 0 ]; then
        return 0
    fi

    src=$(find_source || true)
    if [ -z "$src" ]; then
        log "KWin changed ($built -> $current) but no source copy in /usr/src/$PROJECT-* - run scripts/install.sh --rebuild"
        notify_users "KDE Infinite Desktop: rebuild needed" \
            "KWin was updated to $current. Run 'install.sh --rebuild' from the KDE Infinite Desktop sources."
        write_state failed "$current"
        return 0
    fi

    log "KWin changed ($built -> $current): rebuilding from $src (log: $LOG)"
    local args=(--rebuild --hook --yes --no-deps)
    local prefix
    prefix=$(info_value prefix 2>/dev/null || true)
    [ -n "$prefix" ] && args+=(--prefix "$prefix")

    printf '\n===== %s rebuild for KWin %s (trigger: %s) =====\n' "$(date)" "$current" "$TRIGGER" >>"$LOG" 2>/dev/null || true
    if bash "$src/scripts/install.sh" "${args[@]}" >>"$LOG" 2>&1; then
        rc=0
    else
        rc=$?
    fi

    if [ "$rc" -eq 0 ]; then
        log "rebuilt successfully for KWin $current - log out and back in to activate it"
        write_state ok "$current"
        notify_users "KDE Infinite Desktop was updated" \
            "Rebuilt for KWin $current. Log out and back in to activate it."
    else
        log "rebuild FAILED (exit $rc) - see $LOG; missing build dependencies? Run: $src/scripts/install.sh --rebuild --deps"
        write_state failed "$current"
        notify_users "KDE Infinite Desktop: rebuild failed" \
            "The automatic rebuild for KWin $current failed. See $LOG or run 'install.sh --rebuild --deps'."
    fi
    return 0
}

# Serialize concurrent triggers (apt runs Post-Invoke several times).
if have flock && (: >>"$LOCK_FILE") 2>/dev/null; then
    exec 9>>"$LOCK_FILE"
    flock 9 2>/dev/null || true
fi
main || true; exit 0
