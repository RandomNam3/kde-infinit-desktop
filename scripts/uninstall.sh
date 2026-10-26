#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Removes a KDE Infinite Desktop installation made by scripts/install.sh
# (uses the install manifest saved in <prefix>/share/kde-infinite-desktop/).
# install.sh also copies this script to <prefix>/lib/kde-infinite-desktop/,
# so it keeps working after the source tree has been deleted.
#
# Packages (deb/rpm/pacman/...) are removed with the package manager instead.

set -eu
set -o pipefail

PROJECT=kde-infinite-desktop
EFFECT_ID=infinitedesktop
NAME='KDE Infinite Desktop'

ASSUME_YES=0
DRY_RUN=0
PREFIX=''
MANIFEST=''
KEEP_CONFIG=1
LEGACY=0

usage() {
    cat <<EOF
Usage: uninstall.sh [OPTIONS]

  -y, --yes          do not ask for confirmation
  -n, --dry-run      only print what would be removed
      --prefix DIR   prefix of the installation (default: search /usr, /usr/local)
      --manifest F   use this install_manifest.txt (e.g. from a manual cmake build)
      --purge        also remove your settings and saved canvas state
                     (kwinrc [Effect-infinitedesktop], ~/.local/state/$PROJECT)
      --legacy       remove an installation of the old name "KDE Infinit Desktop"
                     (up to 0.5.1); its settings and files stay for the new version
  -h, --help         show this help
EOF
}

while [ $# -gt 0 ]; do
    case $1 in
        -y | --yes) ASSUME_YES=1 ;;
        -n | --dry-run) DRY_RUN=1 ;;
        --prefix)
            [ $# -ge 2 ] || { echo "--prefix needs a directory" >&2; exit 1; }
            PREFIX=${2%/}
            shift
            ;;
        --prefix=*) PREFIX=${1#*=} PREFIX=${PREFIX%/} ;;
        --manifest)
            [ $# -ge 2 ] || { echo "--manifest needs a file" >&2; exit 1; }
            MANIFEST=$2
            shift
            ;;
        --manifest=*) MANIFEST=${1#*=} ;;
        --purge) KEEP_CONFIG=0 ;;
        --legacy) LEGACY=1 ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1 (see --help)" >&2
            exit 1
            ;;
    esac
    shift
done

if [ "$LEGACY" -eq 1 ]; then
    # Up to 0.5.1 the project was called "KDE Infinit Desktop".
    PROJECT=kde-infinit-desktop
    EFFECT_ID=infinitdesktop
    NAME='KDE Infinit Desktop'
    KEEP_CONFIG=1
fi
readonly PROJECT EFFECT_ID NAME
readonly AUTOSTART_FILE="/etc/xdg/autostart/$PROJECT-kwin-check.desktop"

info() { printf '==> %s\n' "$*"; }
warn() { printf 'Warning: %s\n' "$*" >&2; }
die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}
have() { command -v "$1" >/dev/null 2>&1; }

print_cmd() {
    local out='' a
    for a in "$@"; do out+=" $(printf '%q' "$a")"; done
    printf '   $%s\n' "$out"
}

SUDO=()
if [ "$(id -u)" -ne 0 ]; then
    if have sudo; then
        SUDO=(sudo)
    elif have doas; then
        SUDO=(doas)
    else
        SUDO=(sudo)
    fi
fi

run_root() {
    print_cmd "${SUDO[@]}" "$@"
    [ "$DRY_RUN" -eq 1 ] && return 0
    "${SUDO[@]}" "$@"
}

confirm() {
    [ "$ASSUME_YES" -eq 1 ] && return 0
    [ "$DRY_RUN" -eq 1 ] && return 0
    [ -t 0 ] || die "no terminal to ask for confirmation, re-run with --yes"
    local reply
    printf '%s [y/N] ' "$1"
    read -r reply || reply=n
    case $reply in
        y | Y | yes | YES | j | J | ja | Ja) return 0 ;;
        *) return 1 ;;
    esac
}

# --- locate the installation ---
INFO_DIR=''
if [ -n "$PREFIX" ]; then
    INFO_DIR="$PREFIX/share/$PROJECT"
else
    for p in /usr /usr/local; do
        if [ -d "$p/share/$PROJECT" ]; then
            PREFIX=$p
            INFO_DIR="$p/share/$PROJECT"
            break
        fi
    done
fi
if [ -z "$MANIFEST" ] && [ -n "$INFO_DIR" ] && [ -r "$INFO_DIR/install_manifest.txt" ]; then
    MANIFEST="$INFO_DIR/install_manifest.txt"
fi
[ -n "$MANIFEST" ] || die "no installation by scripts/install.sh found (looked in /usr and /usr/local). Use --prefix or --manifest."
[ -r "$MANIFEST" ] || die "cannot read $MANIFEST"

# --- installed by a package? then the package manager has to remove it ---
plugin=$(grep -E "/kwin/effects/plugins/${EFFECT_ID}\.so\$" "$MANIFEST" | head -n1 || true)
if [ -n "$plugin" ] && [ -e "$plugin" ]; then
    owner=''
    if have dpkg-query && owner=$(dpkg-query -S "$plugin" 2>/dev/null); then
        owner=${owner%%:*}
    elif have pacman && owner=$(pacman -Qqo "$plugin" 2>/dev/null); then
        :
    elif have rpm && owner=$(rpm -qf "$plugin" 2>/dev/null); then
        :
    else
        owner=''
    fi
    if [ -n "$owner" ]; then
        die "$plugin belongs to the package '$owner' - remove it with your package manager instead."
    fi
fi

# --- collect files ---
# (CMake writes install_manifest.txt without a final newline: "|| [ -n ]")
files=()
while IFS= read -r f || [ -n "$f" ]; do
    case $f in
        /*) ;;
        *) continue ;;
    esac
    case $f in *..*) continue ;; esac
    if [ -f "$f" ] || [ -L "$f" ]; then
        files+=("$f")
    fi
done <"$MANIFEST"

# login check, package manager hooks (automatic rebuild), state
extra=()
for f in "$AUTOSTART_FILE" \
    "/etc/pacman.d/hooks/$PROJECT.hook" \
    "/etc/apt/apt.conf.d/99$PROJECT" \
    "/etc/dnf/libdnf5-plugins/actions.d/$PROJECT.actions" \
    "/etc/dnf/plugins/post-transaction-actions.d/$PROJECT.action" \
    "/usr/lib/zypp/plugins/commit/$PROJECT" \
    "/etc/apk/commit_hooks.d/$PROJECT.post-commit"; do
    if [ -e "$f" ]; then extra+=("$f"); fi
done

# copy of the sources used by the automatic rebuild
src_copy=''
if [ -n "$INFO_DIR" ] && [ -r "$INFO_DIR/install-info" ]; then
    src_copy=$(sed -n 's/^source_dir=//p' "$INFO_DIR/install-info" | head -n1)
fi
case $src_copy in
    /usr/src/"$PROJECT"-*) ;;
    *) src_copy='' ;;
esac

info "Removing $NAME (${#files[@]} installed files, prefix ${PREFIX:-?})"
confirm "Remove $NAME?" || die "aborted"

# --- unload & disable the effect in the running session (as the user) ---
if [ "$(id -u)" -ne 0 ]; then
    print_cmd kwriteconfig6 --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" false --notify
    if [ "$DRY_RUN" -eq 0 ]; then
        if have kwriteconfig6; then
            kwriteconfig6 --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" false --notify 2>/dev/null ||
                kwriteconfig6 --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" false || true
        fi
        for q in qdbus6 qdbus-qt6 /usr/lib/qt6/bin/qdbus /usr/lib64/qt6/bin/qdbus; do
            if have "$q"; then
                "$q" org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "$EFFECT_ID" >/dev/null 2>&1 || true
                break
            fi
        done
        if ! have qdbus6 && have gdbus; then
            gdbus call --session --dest org.kde.KWin --object-path /Effects \
                --method org.kde.kwin.Effects.unloadEffect "$EFFECT_ID" >/dev/null 2>&1 || true
        fi
    fi
fi

# --- remove files ---
if [ "${#files[@]}" -gt 0 ]; then
    if [ "$DRY_RUN" -eq 1 ]; then
        for f in "${files[@]}"; do print_cmd "${SUDO[@]}" rm -f -- "$f"; done
    else
        "${SUDO[@]}" rm -f -- "${files[@]}"
    fi
fi
if [ "${#extra[@]}" -gt 0 ]; then
    run_root rm -f -- "${extra[@]}"
fi
for d in "$PREFIX/lib/$PROJECT" "$INFO_DIR" "/var/lib/$PROJECT"; do
    case $d in
        /*/"$PROJECT") ;;
        *) continue ;;
    esac
    if [ -d "$d" ]; then run_root rm -rf -- "$d"; fi
done
if [ -n "$src_copy" ] && [ -d "$src_copy" ]; then
    run_root rm -rf -- "$src_copy"
fi
if { [ "$KEEP_CONFIG" -eq 0 ] || [ "$LEGACY" -eq 1 ]; } && [ -e "/var/log/$PROJECT-rebuild.log" ]; then
    run_root rm -f -- "/var/log/$PROJECT-rebuild.log"
fi

# --- user data ---
if [ "$KEEP_CONFIG" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
    state="${XDG_STATE_HOME:-$HOME/.local/state}/$PROJECT"
    data="${XDG_DATA_HOME:-$HOME/.local/share}/$PROJECT"
    for d in "$state" "$data"; do
        if [ -d "$d" ]; then
            print_cmd rm -rf -- "$d"
            [ "$DRY_RUN" -eq 1 ] || rm -rf -- "$d"
        fi
    done
    if have kwriteconfig6; then
        # kwriteconfig6 cannot delete a whole group; delete the known keys.
        cfg="${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc"
        if [ -r "$cfg" ]; then
            keys=$(sed -n '/^\[Effect-infinitedesktop\]/,/^\[/{/^[A-Za-z]/s/=.*//p}' "$cfg")
            for k in $keys; do
                print_cmd kwriteconfig6 --file kwinrc --group Effect-infinitedesktop --key "$k" --delete
                [ "$DRY_RUN" -eq 1 ] || kwriteconfig6 --file kwinrc --group Effect-infinitedesktop --key "$k" --delete || true
            done
        fi
    fi
fi

info "$NAME has been removed."
echo "If KWin still shows the effect, log out and back in."
