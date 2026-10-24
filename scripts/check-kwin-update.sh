#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks whether the installed KDE Infinit Desktop KWin plugin was built for
# the KWin version that is installed right now.
#
# KWin plugins have no stable ABI and KWin refuses to load a plugin that was
# built for a different KWin version (the plugin interface id contains the
# exact KWin version). After a KWin update the effect therefore silently
# disappears until it is rebuilt. Installations made by scripts/install.sh
# are rebuilt automatically by a package manager hook (kwin-update-hook.sh);
# this script is the safety net at login (--offer-rebuild) and a manual check.
#
# The KWin version the plugin was built against is installed by CMake to
#   <prefix>/share/kde-infinit-desktop/built-for-kwin
#
# Exit status: 0 = up to date, 1 = rebuild needed, 2 = unknown/not installed.

set -eu
set -o pipefail

readonly PROJECT=kde-infinit-desktop

QUIET=0
NOTIFY=0
OFFER=0
MODE=check

usage() {
    cat <<EOF
Usage: check-kwin-update.sh [--quiet] [--notify] [--offer-rebuild]
       check-kwin-update.sh --print-installed-kwin | --print-built-for

  --quiet                 print nothing when everything is fine
  --notify                additionally show a desktop notification when a rebuild
                          is needed (used by the login autostart entry)
  --offer-rebuild         ask whether to rebuild now (administrator password via
                          pkexec), used by the login autostart entry
  --print-installed-kwin  print the installed KWin version and exit
  --print-built-for       print the KWin version the plugin was built for and exit
  -h, --help              show this help

Exit status: 0 = up to date, 1 = rebuild needed, 2 = unknown / not installed
EOF
}

while [ $# -gt 0 ]; do
    case $1 in
        -q | --quiet) QUIET=1 ;;
        --notify) NOTIFY=1 ;;
        --offer-rebuild) OFFER=1 NOTIFY=1 ;;
        --print-installed-kwin) MODE=installed ;;
        --print-built-for) MODE=builtfor ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "check-kwin-update.sh: unknown option: $1" >&2
            exit 2
            ;;
    esac
    shift
done

have() { command -v "$1" >/dev/null 2>&1; }

# normalize_version STRING: "4:6.8.1-0ubuntu1" -> "6.8.1", "kwin-6.8.1_2" -> "6.8.1"
normalize_version() {
    local v=$1
    v=${v#kwin-}
    v=${v##*:}
    if [[ $v =~ ^([0-9]+(\.[0-9]+)*) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

# --- installed KWin version, most reliable source first ---
kwin_from_packages() {
    local v='' p
    if have dpkg-query; then
        for p in libkwin6 kwin-common kwin-wayland; do
            # shellcheck disable=SC2016 # dpkg-query format, not a shell expansion
            v=$(dpkg-query -W -f='${Status} ${Version}\n' "$p" 2>/dev/null | sed -n 's/^install ok installed //p' | head -n1) || true
            [ -n "$v" ] && break
        done
    fi
    if [ -z "$v" ] && have pacman; then
        v=$(pacman -Q kwin 2>/dev/null | sed -n 's/^kwin //p') || true
    fi
    if [ -z "$v" ] && have rpm; then
        for p in kwin-libs kwin libkwin6 kwin6 lib64kwin6; do
            v=$(rpm -q --qf '%{VERSION}\n' "$p" 2>/dev/null | grep -E '^[0-9]' | head -n1) || true
            [ -n "$v" ] && break
        done
    fi
    if [ -z "$v" ] && have xbps-query; then
        v=$(xbps-query -p pkgver kwin 2>/dev/null) || true
    fi
    if [ -z "$v" ] && have apk; then
        v=$(apk info -e -v kwin 2>/dev/null | head -n1) || true
    fi
    if [ -z "$v" ] && [ -d /var/db/pkg/kde-plasma ]; then
        local d
        for d in /var/db/pkg/kde-plasma/kwin-[0-9]*; do
            [ -d "$d" ] && v=${d##*/}
        done
    fi
    [ -n "$v" ] && normalize_version "$v"
}

kwin_from_devfiles() {
    local f v
    for f in /usr/lib/cmake/KWin/KWinConfigVersion.cmake \
        /usr/lib64/cmake/KWin/KWinConfigVersion.cmake \
        /usr/lib/*/cmake/KWin/KWinConfigVersion.cmake \
        /usr/local/lib/cmake/KWin/KWinConfigVersion.cmake; do
        [ -r "$f" ] || continue
        v=$(sed -n 's/^ *set(PACKAGE_VERSION "\([0-9][0-9.]*\)")/\1/p' "$f" | head -n1)
        if [ -n "$v" ]; then
            printf '%s\n' "$v"
            return 0
        fi
    done
    for f in /usr/include/kwin/config-kwin.h /usr/local/include/kwin/config-kwin.h; do
        [ -r "$f" ] || continue
        v=$(sed -n 's/^#define KWIN_PLUGIN_VERSION_STRING "\([0-9][0-9.]*\)".*/\1/p' "$f" | head -n1)
        if [ -n "$v" ]; then
            printf '%s\n' "$v"
            return 0
        fi
    done
    return 1
}

kwin_from_binary() {
    local out
    have kwin_wayland || return 1
    out=$(timeout 10 kwin_wayland --version 2>/dev/null | head -n1) || return 1
    [[ $out =~ ([0-9]+\.[0-9]+(\.[0-9]+)*) ]] || return 1
    printf '%s\n' "${BASH_REMATCH[1]}"
}

installed_kwin() {
    kwin_from_packages || kwin_from_devfiles || kwin_from_binary
}

# --- version the plugin was built for ---
BUILT_FOR_FILE=''
find_built_for() {
    local d dirs
    if [ -n "${KID_BUILT_FOR_FILE:-}" ]; then
        BUILT_FOR_FILE=$KID_BUILT_FOR_FILE
        [ -r "$BUILT_FOR_FILE" ]
        return
    fi
    dirs="/usr/share:/usr/local/share:${XDG_DATA_DIRS:-}"
    local IFS=:
    for d in $dirs; do
        [ -n "$d" ] || continue
        if [ -r "$d/$PROJECT/built-for-kwin" ]; then
            BUILT_FOR_FILE="$d/$PROJECT/built-for-kwin"
            return 0
        fi
    done
    return 1
}

built_for_kwin() {
    find_built_for || return 1
    local v
    v=$(head -n1 "$BUILT_FOR_FILE")
    normalize_version "$v"
}

# --- notification ---
notify_user() {
    local title=$1 body=$2
    if have notify-send; then
        notify-send --app-name="KDE Infinit Desktop" --icon=org.kde.infinitdesktop \
            --urgency=normal "$title" "$body" 2>/dev/null && return 0
    fi
    if have kdialog; then
        kdialog --title "$title" --passivepopup "$body" 30 2>/dev/null && return 0
    fi
    if have gdbus; then
        gdbus call --session --dest org.freedesktop.Notifications \
            --object-path /org/freedesktop/Notifications \
            --method org.freedesktop.Notifications.Notify \
            "KDE Infinit Desktop" 0 kde-infinit-desktop "$title" "$body" '[]' '{}' 30000 \
            >/dev/null 2>&1 && return 0
    fi
    return 1
}

# Directory with the helper scripts of a scripts/install.sh installation
helper_dir() {
    local d self
    self=$(readlink -f -- "${BASH_SOURCE[0]}" 2>/dev/null || printf '%s' "${BASH_SOURCE[0]}")
    for d in "${self%/*}" /usr/lib/$PROJECT /usr/local/lib/$PROJECT; do
        if [ -r "$d/kwin-update-hook.sh" ] && [ -r "$d/check-kwin-update.sh" ]; then
            printf '%s\n' "$d"
            return 0
        fi
    done
    return 1
}

rebuild_hint() {
    local hd
    if hd=$(helper_dir); then
        printf 'sudo %s/kwin-update-hook.sh\n' "$hd"
    else
        printf 'rebuild/update your kde-infinit-desktop package (or run scripts/install.sh --rebuild)\n'
    fi
}

last_rebuild_failed() {
    local f=/var/lib/$PROJECT/last-rebuild
    [ -r "$f" ] && grep -q '^status=failed' "$f"
}

# offer_rebuild: ask in the session and run the rebuild via pkexec
offer_rebuild() {
    local hd question
    hd=$(helper_dir) || return 1
    have pkexec || return 1
    question="KWin was updated to $current, but KDE Infinit Desktop was built for KWin $built, so KWin does not load it.

Rebuild it now? This takes about a minute and needs the administrator password."
    if have kdialog; then
        kdialog --title "KDE Infinit Desktop" --icon org.kde.infinitdesktop --yesno "$question" 2>/dev/null || return 0
    else
        return 1
    fi
    if pkexec "$hd/kwin-update-hook.sh" --force login; then
        if [ "$(bash "${BASH_SOURCE[0]}" --print-built-for 2>/dev/null)" = "$current" ]; then
            notify_user "KDE Infinit Desktop was rebuilt" \
                "Rebuilt for KWin $current. Log out and back in to activate it." || true
            return 0
        fi
    fi
    notify_user "KDE Infinit Desktop: rebuild failed" \
        "See /var/log/kde-infinit-desktop-rebuild.log or run: $(rebuild_hint)" || true
    return 0
}

# ================================================================== main ==
case $MODE in
    installed)
        installed_kwin || exit 2
        exit 0
        ;;
    builtfor)
        built_for_kwin || exit 2
        exit 0
        ;;
esac

if ! built=$(built_for_kwin); then
    [ "$QUIET" -eq 1 ] || echo "KDE Infinit Desktop: no installed KWin plugin found (missing <prefix>/share/$PROJECT/built-for-kwin)." >&2
    exit 2
fi
if ! current=$(installed_kwin); then
    [ "$QUIET" -eq 1 ] || echo "KDE Infinit Desktop: could not determine the installed KWin version." >&2
    exit 2
fi

if [ "$built" = "$current" ]; then
    [ "$QUIET" -eq 1 ] || echo "KDE Infinit Desktop: plugin was built for KWin $built - up to date."
    exit 0
fi

hint=$(rebuild_hint)
msg="KDE Infinit Desktop was built for KWin $built, but KWin $current is installed.
KWin will not load the effect until it is rebuilt. Run:
  $hint
then log out and back in."
if last_rebuild_failed; then
    msg="$msg
The last automatic rebuild failed, see /var/log/kde-infinit-desktop-rebuild.log"
fi
echo "$msg" >&2
if [ "$OFFER" -eq 1 ] && offer_rebuild; then
    exit 1
fi
if [ "$NOTIFY" -eq 1 ]; then
    notify_user "KDE Infinit Desktop needs a rebuild" "$msg" || true
fi
exit 1
