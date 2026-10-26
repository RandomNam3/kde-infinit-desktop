#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# KDE Infinite Desktop - one-command installer
# ("If it runs KDE Plasma, it runs KDE Infinite Desktop").
#
#   ./install.sh  or  ./scripts/install.sh    (from the source tree)
#   curl -fsSL https://raw.githubusercontent.com/RandomNam3/kde-infinite-desktop/main/install.sh | bash
#                                             (downloads the sources itself)
#
#  1. detects the distribution (/etc/os-release: ID and ID_LIKE)
#  2. checks for Plasma 6 / KWin >= 6.8 and a Wayland session
#  3. installs the build dependencies with the native package manager
#     (asks first, unless --yes)
#  4. builds out of tree in a temporary directory, installs with sudo
#  5. keeps a copy of the sources in /usr/src/kde-infinite-desktop-<version>
#     and installs a package manager hook (pacman, apt, dnf, zypper, apk) that
#     rebuilds the plugin automatically after every KWin update - KWin only
#     loads plugins built for exactly its version (MAJOR.MINOR.PATCH)
#  6. enables the effect in the running KWin
#
# Run it as your normal user, not with sudo - it calls sudo itself.
# Everything is wrapped in main() so "curl ... | bash" is safe.

set -eu
set -o pipefail

readonly PROJECT=kde-infinite-desktop
readonly EFFECT_ID=infinitedesktop
readonly KWIN_MIN=6.8
readonly AUTOSTART_FILE=/etc/xdg/autostart/kde-infinite-desktop-kwin-check.desktop
# Sources for "curl ... | bash" (branch main; override with --source-url).
readonly DEFAULT_SOURCE_URL=https://github.com/RandomNam3/kde-infinite-desktop/archive/refs/heads/main.tar.gz

if ! SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-.}")" 2>/dev/null && pwd -P); then
    SCRIPT_DIR=$(pwd -P)
fi
SOURCE_DIR=''

# ---------------------------------------------------------------- options --
ASSUME_YES=0
INSTALL_DEPS=1
DEPS_ONLY=0
DEPS_FORCED=0
PREFIX=/usr
PREFIX_SET=0
ACTION=install
REBUILD=0
HOOK_MODE=0
DRY_RUN=0
RELOGIN_NEEDED=0
ENABLE_EFFECT=1
UPDATE_CHECK=1
AUTO_REBUILD=1
RUN_TESTS=0
FORCE=0
BUILD_TYPE=Release
BUILD_DIR=
SOURCE_ARG=
SOURCE_URL=${KID_SOURCE_URL:-$DEFAULT_SOURCE_URL}
WITH_EFFECT=ON
WITH_KCM=ON
WITH_APP=ON
UNINSTALL_ARGS=()

usage() {
    cat <<EOF
Usage: install.sh [OPTIONS]

Builds KDE Infinite Desktop from source, installs it, enables the effect and
sets up the automatic rebuild after KWin updates.

Options:
  -y, --yes             do not ask for confirmation
  -n, --dry-run         only print what would be done
      --no-deps         do not install build dependencies
      --deps            install build dependencies even with --rebuild
      --deps-only       only install the build dependencies, then exit (CI, containers)
      --prefix DIR      installation prefix (default: /usr). The KWin plugin itself
                        always goes into the Qt plugin directory, the only place
                        where KWin looks for effects.
      --rebuild         rebuild and reinstall (after a KWin update; reuses the
                        settings of the previous installation, implies --no-deps)
      --uninstall       remove the installation (see uninstall.sh --help)
      --no-auto-rebuild do not install the package manager hook that rebuilds
                        the plugin after KWin updates
      --no-update-check do not install the login check (notification + rebuild offer)
      --no-enable       do not enable/load the effect after installing
      --run-tests       build and run the unit tests before installing
      --build-type T    CMake build type (default: Release)
      --build-dir DIR   build in DIR and keep it (default: temporary directory)
      --source DIR      use the sources in DIR
      --source-url URL  download the sources from URL (.tar.gz) when not run
                        from a source tree (default: \$KID_SOURCE_URL or
                        $DEFAULT_SOURCE_URL)
      --without-app     do not build the standalone settings app
      --without-kcm     do not build the System Settings module
      --without-effect  do not build the KWin effect (developers only)
      --force           continue even if the KWin version / system checks fail
      --hook            (internal) non-interactive rebuild run by the update hook
  -h, --help            show this help

Examples:
  ./scripts/install.sh               # everything: dependencies, build, install, enable
  ./scripts/install.sh --dry-run     # show the plan, change nothing
  ./scripts/install.sh --rebuild     # manual rebuild (normally done automatically)
  ./scripts/install.sh --uninstall
EOF
}

# ---------------------------------------------------------------- output --
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_BOLD=$'\033[1m' C_RED=$'\033[31m' C_YEL=$'\033[33m' C_GRN=$'\033[32m' C_BLU=$'\033[34m' C_OFF=$'\033[0m'
else
    C_BOLD='' C_RED='' C_YEL='' C_GRN='' C_BLU='' C_OFF=''
fi
info() { printf '%s==>%s %s\n' "$C_BLU$C_BOLD" "$C_OFF" "$*"; }
ok() { printf '%s  ->%s %s\n' "$C_GRN" "$C_OFF" "$*"; }
warn() { printf '%sWarning:%s %s\n' "$C_YEL$C_BOLD" "$C_OFF" "$*" >&2; }
die() {
    printf '%sError:%s %s\n' "$C_RED$C_BOLD" "$C_OFF" "$*" >&2
    exit 1
}

# Print a command in copy-and-paste friendly form.
print_cmd() {
    local out='' a
    for a in "$@"; do
        out+=" $(printf '%q' "$a")"
    done
    printf '%s   $%s%s\n' "$C_BOLD" "$C_OFF" "$out"
}

# run CMD...: execute (or only print with --dry-run)
run() {
    print_cmd "$@"
    [ "$DRY_RUN" -eq 1 ] && return 0
    "$@"
}

# run_root CMD...: like run, but with root privileges
SUDO=()
run_root() {
    print_cmd "${SUDO[@]}" "$@"
    [ "$DRY_RUN" -eq 1 ] && return 0
    "${SUDO[@]}" "$@"
}

# install_root_file MODE SRC DEST: install a file with root privileges
install_root_file() {
    run_root install -D -m "$1" "$2" "$3"
}

confirm() {
    [ "$ASSUME_YES" -eq 1 ] && return 0
    [ "$DRY_RUN" -eq 1 ] && return 0
    local reply
    printf '%s [Y/n] ' "$1"
    if [ -t 0 ]; then
        read -r reply || reply=n
    elif [ -c /dev/tty ] && (: </dev/tty) 2>/dev/null; then
        # "curl ... | bash": stdin is the script, ask on the terminal
        read -r reply </dev/tty || reply=n
    else
        echo
        die "$1 - no terminal to ask for confirmation, re-run with --yes"
    fi
    case $reply in
        '' | y | Y | yes | YES | j | J | ja | Ja) return 0 ;;
        *) return 1 ;;
    esac
}

have() { command -v "$1" >/dev/null 2>&1; }

# version_ge A B: true if version A >= version B (numeric, dot separated)
version_ge() {
    local IFS=.
    local -a a b
    read -r -a a <<<"${1%%[!0-9.]*}"
    read -r -a b <<<"${2%%[!0-9.]*}"
    local i x y
    for ((i = 0; i < ${#a[@]} || i < ${#b[@]}; i++)); do
        x=${a[i]:-0} y=${b[i]:-0}
        ((10#$x > 10#$y)) && return 0
        ((10#$x < 10#$y)) && return 1
    done
    return 0
}

parse_args() {
    while [ $# -gt 0 ]; do
        case $1 in
            -y | --yes) ASSUME_YES=1 UNINSTALL_ARGS+=(--yes) ;;
            --no-deps) INSTALL_DEPS=0 ;;
            --deps) INSTALL_DEPS=1 DEPS_FORCED=1 ;;
            --deps-only) DEPS_ONLY=1 INSTALL_DEPS=1 ;;
            --prefix)
                [ $# -ge 2 ] || die "--prefix needs a directory"
                PREFIX=$2 PREFIX_SET=1
                shift
                ;;
            --prefix=*) PREFIX=${1#*=} PREFIX_SET=1 ;;
            --rebuild) REBUILD=1 ;;
            --hook) HOOK_MODE=1 REBUILD=1 ASSUME_YES=1 ENABLE_EFFECT=0 ;;
            --uninstall) ACTION=uninstall ;;
            -n | --dry-run) DRY_RUN=1 UNINSTALL_ARGS+=(--dry-run) ;;
            --no-enable) ENABLE_EFFECT=0 ;;
            --no-update-check) UPDATE_CHECK=0 ;;
            --no-auto-rebuild) AUTO_REBUILD=0 ;;
            --run-tests) RUN_TESTS=1 ;;
            --build-type)
                [ $# -ge 2 ] || die "--build-type needs a value"
                BUILD_TYPE=$2
                shift
                ;;
            --build-type=*) BUILD_TYPE=${1#*=} ;;
            --build-dir)
                [ $# -ge 2 ] || die "--build-dir needs a directory"
                BUILD_DIR=$2
                shift
                ;;
            --build-dir=*) BUILD_DIR=${1#*=} ;;
            --source)
                [ $# -ge 2 ] || die "--source needs a directory"
                SOURCE_ARG=$2
                shift
                ;;
            --source=*) SOURCE_ARG=${1#*=} ;;
            --source-url)
                [ $# -ge 2 ] || die "--source-url needs a URL"
                SOURCE_URL=$2
                shift
                ;;
            --source-url=*) SOURCE_URL=${1#*=} ;;
            --without-app) WITH_APP=OFF ;;
            --without-kcm) WITH_KCM=OFF ;;
            --without-effect) WITH_EFFECT=OFF ;;
            --force) FORCE=1 ;;
            -h | --help)
                usage
                exit 0
                ;;
            *) die "unknown option: $1 (see --help)" ;;
        esac
        shift
    done
    case $PREFIX in
        /*) ;;
        *) die "--prefix must be an absolute path" ;;
    esac
    PREFIX=${PREFIX%/}
    [ -n "$PREFIX" ] || die "--prefix / is not supported"
}

# ------------------------------------------------------ distro detection --
OS_ID='' OS_LIKE='' OS_NAME='' OS_VERSION_ID=''
read_os_release() {
    local file key value
    for file in /etc/os-release /usr/lib/os-release; do
        [ -r "$file" ] || continue
        while IFS='=' read -r key value; do
            value=${value%\"}
            value=${value#\"}
            value=${value%\'}
            value=${value#\'}
            case $key in
                ID) OS_ID=$value ;;
                ID_LIKE) OS_LIKE=$value ;;
                PRETTY_NAME) OS_NAME=$value ;;
                VERSION_ID) OS_VERSION_ID=$value ;;
            esac
        done <"$file"
        return 0
    done
    return 1
}

FAMILY=''
detect_family() {
    local id
    for id in $OS_ID $OS_LIKE; do
        case $id in
            neon | kubuntu | ubuntu | debian | linuxmint | pop | elementary | zorin | devuan | raspbian | tuxedo | kdeneon) FAMILY=debian ;;
            arch | archarm | cachyos | manjaro | manjaro-arm | endeavouros | garuda | artix | arcolinux | rebornos | archcraft | biglinux | parabola | steamos) FAMILY=arch ;;
            fedora | nobara | ultramarine | risios) FAMILY=fedora ;;
            rhel | centos | almalinux | rocky | ol | circle | navylinux | eurolinux) FAMILY=rhel ;;
            opensuse | opensuse-* | suse | sles | sled) FAMILY=suse ;;
            gentoo | funtoo | calculate | redcore) FAMILY=gentoo ;;
            void) FAMILY=void ;;
            solus) FAMILY=solus ;;
            alpine | postmarketos) FAMILY=alpine ;;
            nixos) FAMILY=nixos ;;
            mageia | openmandriva | rosa) FAMILY=rpm-generic ;;
        esac
        [ -n "$FAMILY" ] && return 0
    done
    # Last resort: guess from the package manager.
    if have apt-get; then
        FAMILY=debian
    elif have pacman; then
        FAMILY=arch
    elif have zypper; then
        FAMILY=suse
    elif have dnf; then
        FAMILY=rpm-generic
    elif have emerge; then
        FAMILY=gentoo
    elif have xbps-install; then
        FAMILY=void
    elif have apk; then
        FAMILY=alpine
    elif have eopkg; then
        FAMILY=solus
    fi
    [ -n "$FAMILY" ]
}

# true if / or /usr is mounted read-only (image based / immutable systems)
usr_is_readonly() {
    local mnt opts best='' best_opts=''
    [ -r /proc/mounts ] || return 1
    while read -r _ mnt _ opts _; do
        case $mnt in
            / | /usr)
                if [ "${#mnt}" -ge "${#best}" ]; then
                    best=$mnt best_opts=$opts
                fi
                ;;
        esac
    done </proc/mounts
    case ,$best_opts, in
        *,ro,*) return 0 ;;
    esac
    return 1
}

dnf_is_dnf5() {
    have dnf5 && return 0
    have dnf && dnf --version 2>/dev/null | head -n1 | grep -q 'dnf5'
}

# --------------------------------------------------- dependency handling --
# Package names of the native package managers. Verified in containers
# (see docs/INSTALL.md for the distro table). An apt entry "a|b" is satisfied
# by either package; the first one the repositories have is installed.
DEPS=()
PM_NAME=''

deps_for_family() {
    case $FAMILY in
        debian)
            PM_NAME=apt
            DEPS=(build-essential cmake ninja-build pkgconf gettext tar
                extra-cmake-modules qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-svg-dev
                libkf6config-dev libkf6coreaddons-dev libkf6globalaccel-dev libkf6i18n-dev
                libkf6kcmutils-dev libkf6configwidgets-dev libkf6xmlgui-dev libkf6widgetsaddons-dev
                kwin-dev libwayland-dev libepoxy-dev libdrm-dev libvulkan-dev libxkbcommon-dev
                # runtime: QML modules of the settings app, SVG icons, tools used below
                qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts
                qml6-module-qtquick-effects qml6-module-qtquick-shapes qml6-module-qtquick-window
                # (KDE neon ships the SVG plugins in its qt6-svg, it has no qt6-svg-plugins)
                qml6-module-qtqml-workerscript 'qt6-svg-plugins|qt6-svg' libkf6config-bin qdbus-qt6)
            ;;
        arch)
            PM_NAME=pacman
            DEPS=(base-devel cmake ninja extra-cmake-modules
                qt6-base qt6-declarative qt6-svg qt6-tools
                kconfig kcoreaddons kglobalaccel ki18n kcmutils kconfigwidgets kxmlgui kwidgetsaddons
                kwin wayland libepoxy libdrm vulkan-headers libxkbcommon)
            ;;
        fedora | rhel)
            PM_NAME=dnf
            DEPS=(gcc-c++ cmake ninja-build gettext tar extra-cmake-modules kf6-rpm-macros
                qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtsvg-devel
                kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel
                kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel kf6-kwidgetsaddons-devel
                kwin-devel wayland-devel libepoxy-devel libdrm-devel vulkan-headers vulkan-loader-devel
                libxkbcommon-devel
                qt6-qtdeclarative qt6-qtsvg qt6-qttools kf6-kconfig)
            ;;
        suse)
            PM_NAME=zypper
            DEPS=(gcc-c++ cmake ninja gettext-tools tar kf6-extra-cmake-modules
                qt6-base-devel qt6-base-private-devel qt6-declarative-devel qt6-quickcontrols2-devel
                qt6-svg-devel qt6-test-devel
                kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel
                kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel kf6-kwidgetsaddons-devel
                kwin6-devel kf6-kwindowsystem-devel wayland-devel libepoxy-devel libdrm-devel
                vulkan-headers vulkan-devel
                libxkbcommon-devel
                qt6-declarative-imports libQt6Svg6 qt6-tools-qdbus kf6-kconfig)
            ;;
        rpm-generic)
            # Mageia, OpenMandriva, ...: resolve by CMake/pkg-config capability.
            PM_NAME=dnf
            DEPS=(gcc-c++ cmake ninja-build gettext tar extra-cmake-modules
                'cmake(Qt6Core)' 'cmake(Qt6Gui)' 'cmake(Qt6DBus)' 'cmake(Qt6Widgets)' 'cmake(Qt6Test)'
                'cmake(Qt6Quick)' 'cmake(Qt6QuickControls2)' 'cmake(Qt6Svg)'
                'cmake(KF6Config)' 'cmake(KF6CoreAddons)' 'cmake(KF6GlobalAccel)' 'cmake(KF6I18n)'
                'cmake(KF6KCMUtils)' 'cmake(KF6ConfigWidgets)' 'cmake(KF6XmlGui)' 'cmake(KF6WidgetsAddons)'
                'cmake(KWin)' 'cmake(KF6WindowSystem)' 'pkgconfig(wayland-server)' 'pkgconfig(epoxy)' 'pkgconfig(libdrm)'
                'pkgconfig(vulkan)' 'pkgconfig(xkbcommon)')
            ;;
        gentoo)
            PM_NAME=emerge
            DEPS=(dev-build/cmake dev-build/ninja kde-frameworks/extra-cmake-modules sys-devel/gettext
                dev-qt/qtbase dev-qt/qtdeclarative dev-qt/qtsvg
                kde-frameworks/kconfig kde-frameworks/kcoreaddons kde-frameworks/kglobalaccel
                kde-frameworks/ki18n kde-frameworks/kcmutils kde-frameworks/kconfigwidgets
                kde-frameworks/kxmlgui kde-frameworks/kwidgetsaddons
                kde-plasma/kwin dev-util/vulkan-headers)
            ;;
        void)
            PM_NAME=xbps
            DEPS=(base-devel cmake ninja pkg-config gettext extra-cmake-modules
                qt6-base-devel qt6-base-private-devel qt6-declarative-devel qt6-svg-devel
                kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel
                kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel kf6-kwidgetsaddons-devel
                kwin-devel wayland-devel libepoxy-devel libdrm-devel Vulkan-Headers vulkan-loader-devel
                libxkbcommon-devel qt6-declarative qt6-svg qt6-tools)
            ;;
        solus)
            PM_NAME=eopkg
            DEPS=(cmake ninja extra-cmake-modules
                qt6-base-devel qt6-declarative-devel qt6-svg-devel
                kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel
                kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kxmlgui-devel kf6-kwidgetsaddons-devel
                kwin-devel wayland-devel libepoxy-devel libdrm-devel vulkan-headers vulkan-devel
                libxkbcommon-devel)
            ;;
        alpine)
            PM_NAME=apk
            DEPS=(build-base cmake samurai pkgconf gettext tar extra-cmake-modules
                qt6-qtbase-dev qt6-qtbase-private-dev qt6-qtdeclarative-dev qt6-qtsvg-dev
                kconfig-dev kcoreaddons-dev kglobalaccel-dev ki18n-dev kcmutils-dev
                kconfigwidgets-dev kxmlgui-dev kwidgetsaddons-dev
                kwin-dev wayland-dev libepoxy-dev libdrm-dev vulkan-headers vulkan-loader-dev
                libxkbcommon-dev qt6-qttools)
            ;;
        *)
            return 1
            ;;
    esac
    # plugin packages needed for the automatic rebuild hook
    if [ "$AUTO_REBUILD" -eq 1 ] && [ "$PM_NAME" = dnf ]; then
        if dnf_is_dnf5; then
            DEPS+=(libdnf5-plugin-actions)
        else
            DEPS+=(python3-dnf-plugin-post-transaction-actions)
        fi
    fi
}

# pkg_installed NAME: true if the package (or capability) is installed
pkg_installed() {
    local p=$1
    # shellcheck disable=SC2016 # ${Status} is a dpkg-query format, not a shell expansion
    case $PM_NAME in
        apt) dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'ok installed' ;;
        pacman) pacman -T "$p" >/dev/null 2>&1 ;;
        dnf | zypper) rpm -q --whatprovides "$p" >/dev/null 2>&1 ;;
        emerge) compgen -G "/var/db/pkg/${p}-[0-9]*" >/dev/null 2>&1 ;;
        xbps) xbps-query "$p" >/dev/null 2>&1 ;;
        apk) apk info -e "$p" >/dev/null 2>&1 ;;
        *) return 1 ;;
    esac
}

# dep_installed ENTRY: true if the package of a DEPS entry, or one of its alternatives ("a|b"), is installed
dep_installed() {
    local alts alt
    IFS='|' read -ra alts <<<"$1"
    for alt in "${alts[@]}"; do
        pkg_installed "$alt" && return 0
    done
    return 1
}

# apt_available NAME: true if apt can install NAME (real or virtual package)
apt_available() {
    local line in_rp=0
    if apt-cache show "$1" >/dev/null 2>&1; then
        return 0
    fi
    while IFS= read -r line; do
        if [ "$in_rp" -eq 1 ] && [ -n "$line" ]; then
            return 0
        fi
        case $line in "Reverse Provides:"*) in_rp=1 ;; esac
    done < <(apt-cache showpkg "$1" 2>/dev/null)
    return 1
}

pacman_needs_sync() {
    local db
    for db in /var/lib/pacman/sync/core.db /var/lib/pacman/sync/extra.db; do
        [ -s "$db" ] || return 0
    done
    return 1
}

rhel_prepare_repos() {
    # KDE Plasma on RHEL & clones comes from EPEL ("EPEL KDE") and needs CRB.
    if ! rpm -q epel-release >/dev/null 2>&1; then
        case $OS_ID in
            rhel)
                die "EPEL is not enabled. On RHEL run:
  sudo subscription-manager repos --enable codeready-builder-for-rhel-${OS_VERSION_ID%%.*}-\$(arch)-rpms
  sudo dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-${OS_VERSION_ID%%.*}.noarch.rpm
and re-run this script."
                ;;
        esac
        info "KDE Plasma for ${OS_NAME:-EL} comes from EPEL - enabling EPEL and CRB"
        confirm "Enable the EPEL and CRB repositories?" || die "aborted"
        run_root dnf install -y epel-release
    fi
    if have crb; then
        run_root crb enable
    elif [ "$DRY_RUN" -eq 1 ]; then
        print_cmd "${SUDO[@]}" crb enable
    fi
}

install_deps() {
    deps_for_family || die "no dependency list for '${OS_ID:-unknown}' - install the dependencies listed in docs/INSTALL.md manually and re-run with --no-deps"

    [ "$FAMILY" = rhel ] && rhel_prepare_repos

    local missing=() p
    for p in "${DEPS[@]}"; do
        dep_installed "$p" || missing+=("$p")
    done
    if [ "${#missing[@]}" -eq 0 ]; then
        ok "all build dependencies are installed"
        return 0
    fi

    info "Missing packages (${PM_NAME}): ${missing[*]//|/ or }"
    confirm "Install them now?" || die "aborted - install them yourself or re-run with --no-deps"

    case $PM_NAME in
        apt)
            run_root env DEBIAN_FRONTEND=noninteractive apt-get update
            local avail=() unavailable=() alts alt choice
            for p in "${missing[@]}"; do
                choice=''
                IFS='|' read -ra alts <<<"$p"
                for alt in "${alts[@]}"; do
                    if [ "$DRY_RUN" -eq 1 ] || apt_available "$alt"; then
                        choice=$alt
                        break
                    fi
                done
                if [ -n "$choice" ]; then
                    avail+=("$choice")
                else
                    unavailable+=("$p")
                fi
            done
            [ "${#unavailable[@]}" -eq 0 ] || warn "not available in your repositories (skipped): ${unavailable[*]//|/ or }"
            [ "${#avail[@]}" -eq 0 ] || run_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${avail[@]}"
            ;;
        pacman)
            if pacman_needs_sync; then
                run_root pacman -Syu --needed --noconfirm "${missing[@]}"
            else
                run_root pacman -S --needed --noconfirm "${missing[@]}" ||
                    die "pacman failed - if packages were not found, update your system first (sudo pacman -Syu)"
            fi
            ;;
        dnf)
            run_root dnf install -y "${missing[@]}"
            ;;
        zypper)
            # Container images (and distrobox) ship busybox-gawk, which conflicts with
            # the gawk that rpm-build needs (pulled in by qt6-base-devel).
            if rpm -q busybox-gawk >/dev/null 2>&1; then
                run_root zypper --non-interactive install --force-resolution --no-recommends gawk
            fi
            run_root zypper --non-interactive --gpg-auto-import-keys install --no-recommends "${missing[@]}"
            ;;
        emerge)
            warn "On Gentoo the ebuild in packaging/gentoo is the preferred way."
            run_root emerge --ask=n --noreplace --oneshot "${missing[@]}"
            ;;
        xbps)
            run_root xbps-install -Sy "${missing[@]}"
            ;;
        eopkg)
            run_root eopkg install -y -c system.devel
            run_root eopkg install -y "${missing[@]}"
            ;;
        apk)
            run_root apk add "${missing[@]}"
            ;;
    esac
}

# ------------------------------------------------------- source handling --
is_source_tree() {
    [ -r "$1/CMakeLists.txt" ] && grep -q '^project(kde-infinite-desktop' "$1/CMakeLists.txt"
}

TMP_SOURCE=''
resolve_source() {
    if [ -n "$SOURCE_ARG" ]; then
        SOURCE_DIR=$(cd -- "$SOURCE_ARG" && pwd -P) || die "--source: $SOURCE_ARG not found"
        is_source_tree "$SOURCE_DIR" || die "$SOURCE_DIR is not a KDE Infinite Desktop source tree"
        return 0
    fi
    if is_source_tree "$SCRIPT_DIR/.."; then
        SOURCE_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
        return 0
    fi
    # Not started from a source tree (curl ... | bash): download the sources.
    info "Downloading the sources from $SOURCE_URL"
    if [ "$DRY_RUN" -eq 1 ]; then
        print_cmd curl -fsSL "$SOURCE_URL"
        SOURCE_DIR="${TMPDIR:-/tmp}/$PROJECT-src.XXXXXX"
        return 0
    fi
    TMP_SOURCE=$(mktemp -d "${TMPDIR:-/tmp}/$PROJECT-src.XXXXXX")
    if have curl; then
        curl -fsSL "$SOURCE_URL" | tar -xz -C "$TMP_SOURCE" --strip-components=1
    elif have wget; then
        wget -qO- "$SOURCE_URL" | tar -xz -C "$TMP_SOURCE" --strip-components=1
    else
        die "neither curl nor wget found"
    fi
    is_source_tree "$TMP_SOURCE" || die "the download does not contain the KDE Infinite Desktop sources"
    SOURCE_DIR=$TMP_SOURCE
}

project_version() {
    sed -n 's/^project(kde-infinite-desktop VERSION \([0-9][0-9.]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt" 2>/dev/null | head -n1
}

# Keep a copy of the sources for the automatic rebuild (like DKMS).
persist_source() {
    local dest=$1 old=$2
    if [ "$SOURCE_DIR" = "$dest" ]; then
        return 0
    fi
    info "Keeping a copy of the sources in $dest (used by the automatic rebuild)"
    run_root rm -rf -- "$dest"
    run_root mkdir -p "$dest"
    print_cmd tar -C "$SOURCE_DIR" --exclude=./build ... -cf - . "|" "${SUDO[@]}" tar -C "$dest" -xf -
    if [ "$DRY_RUN" -eq 0 ]; then
        tar -C "$SOURCE_DIR" \
            --exclude=./build --exclude='./build-*' --exclude=./dist --exclude=./.git \
            --exclude=./.cache --exclude=./.idea --exclude=./.vscode --exclude=./.claude \
            --exclude='./obj-*' --exclude=./debian \
            -cf - . | "${SUDO[@]}" tar -C "$dest" --no-same-owner -xf -
    fi
    # remove the copy of an older version
    case $old in
        /usr/src/"$PROJECT"-*)
            if [ "$old" != "$dest" ] && [ -d "$old" ]; then
                run_root rm -rf -- "$old"
            fi
            ;;
    esac
}

# ---------------------------------------------------- environment checks --
installed_kwin_version() {
    bash "$SCRIPT_DIR/check-kwin-update.sh" --print-installed-kwin 2>/dev/null ||
        { [ -n "$SOURCE_DIR" ] && bash "$SOURCE_DIR/scripts/check-kwin-update.sh" --print-installed-kwin 2>/dev/null; } ||
        true
}

SESSION_TYPE=''
KWIN_RUNNING=''
detect_session() {
    SESSION_TYPE=${XDG_SESSION_TYPE:-}
    if [ -z "$SESSION_TYPE" ] || [ "$SESSION_TYPE" = tty ]; then
        if [ -n "${WAYLAND_DISPLAY:-}" ]; then
            SESSION_TYPE=wayland
        elif [ -n "${DISPLAY:-}" ]; then
            SESSION_TYPE=x11
        fi
    fi
    if have pgrep; then
        if pgrep -u "$(id -u)" -x kwin_wayland >/dev/null 2>&1; then
            KWIN_RUNNING=wayland
        elif pgrep -u "$(id -u)" -x kwin_x11 >/dev/null 2>&1; then
            KWIN_RUNNING=x11
        fi
    fi
}

# ------------------------------------------------ D-Bus / config helpers --
kwin_effects_call() {
    # kwin_effects_call METHOD EFFECT  -> prints the reply
    local method=$1 arg=$2 q
    for q in qdbus6 qdbus-qt6 /usr/lib/qt6/bin/qdbus /usr/lib64/qt6/bin/qdbus; do
        if have "$q"; then
            "$q" org.kde.KWin /Effects "org.kde.kwin.Effects.$method" "$arg"
            return
        fi
    done
    if have gdbus; then
        gdbus call --session --dest org.kde.KWin --object-path /Effects \
            --method "org.kde.kwin.Effects.$method" "$arg"
        return
    fi
    if have dbus-send; then
        dbus-send --session --print-reply --dest=org.kde.KWin /Effects \
            "org.kde.kwin.Effects.$method" "string:$arg"
        return
    fi
    return 127
}

running_plugin_version() {
    # version of the plugin KWin is running right now (empty if none)
    local q
    for q in qdbus6 qdbus-qt6 /usr/lib/qt6/bin/qdbus /usr/lib64/qt6/bin/qdbus; do
        if have "$q"; then
            "$q" org.kde.KWin /InfiniteDesktop org.kde.InfiniteDesktop.version 2>/dev/null
            return
        fi
    done
    if have gdbus; then
        gdbus call --session --dest org.kde.KWin --object-path /InfiniteDesktop \
            --method org.kde.InfiniteDesktop.version 2>/dev/null | sed -n "s/^('\(.*\)',)\$/\1/p"
        return
    fi
    return 127
}

kwriteconfig() {
    local k
    for k in kwriteconfig6 /usr/lib/kf6/bin/kwriteconfig6 /usr/lib64/kf6/bin/kwriteconfig6; do
        if have "$k"; then
            "$k" "$@"
            return
        fi
    done
    return 127
}

enable_effect() {
    info "Enabling the effect"
    print_cmd kwriteconfig6 --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" true --notify
    if [ "$DRY_RUN" -eq 0 ]; then
        if ! kwriteconfig --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" true --notify 2>/dev/null &&
            ! kwriteconfig --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" true; then
            warn "kwriteconfig6 not found - enable the effect in System Settings > Window Management > Desktop Effects"
        fi
    fi

    if [ "$KWIN_RUNNING" != wayland ] && [ "$KWIN_RUNNING" != x11 ]; then
        ok "no running KWin found - the effect will be active after your next Plasma (Wayland) login"
        return 0
    fi
    if [ "$REBUILD" -eq 1 ]; then
        ok "rebuilt plugin installed - log out and back in so KWin loads the new build"
        return 0
    fi
    print_cmd qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "$EFFECT_ID"
    [ "$DRY_RUN" -eq 1 ] && return 0
    local was_loaded=0 running version
    if kwin_effects_call isEffectLoaded "$EFFECT_ID" 2>/dev/null | grep -q true; then
        was_loaded=1
    fi
    kwin_effects_call loadEffect "$EFFECT_ID" >/dev/null 2>&1 || true
    if kwin_effects_call isEffectLoaded "$EFFECT_ID" 2>/dev/null | grep -q true; then
        # A running KWin keeps the plugin library it loaded first: neither loadEffect
        # nor reloading the effect brings in the build that was just installed.
        running=$(running_plugin_version || true)
        version=$(project_version)
        if [ "$was_loaded" -eq 1 ] || { [ -n "$running" ] && [ -n "$version" ] && [ "$running" != "$version" ]; }; then
            RELOGIN_NEEDED=1
            warn "KWin keeps running the build it loaded before${running:+ (version $running)}."
            warn "Log out and back in to use the one just installed${version:+ (version $version)}."
        else
            ok "effect '${EFFECT_ID}' is loaded"
        fi
    else
        warn "KWin did not load the effect right now. This is normal right after a KWin update (log out and back in)."
        warn "Otherwise check: journalctl --user -b -g ${EFFECT_ID}"
    fi
}

# ------------------------------------------------------ install metadata --
read_install_info() {
    # read_install_info FILE: load prefix/options of a previous installation
    local file=$1 key value
    while IFS='=' read -r key value; do
        case $key in
            prefix) [ "$PREFIX_SET" -eq 1 ] || PREFIX=$value ;;
            build_type) BUILD_TYPE=$value ;;
            effect) [ "$WITH_EFFECT" = OFF ] || WITH_EFFECT=$value ;;
            kcm) [ "$WITH_KCM" = OFF ] || WITH_KCM=$value ;;
            app) [ "$WITH_APP" = OFF ] || WITH_APP=$value ;;
            update_check) [ "$UPDATE_CHECK" -eq 0 ] || UPDATE_CHECK=$value ;;
            auto_rebuild) [ "$AUTO_REBUILD" -eq 0 ] || AUTO_REBUILD=$value ;;
            source_dir) PREV_SOURCE_DIR=$value ;;
        esac
    done <"$file"
}

PREV_SOURCE_DIR=''
find_previous_install() {
    local d
    for d in "$PREFIX" /usr /usr/local; do
        if [ -r "$d/share/$PROJECT/install-info" ]; then
            printf '%s\n' "$d/share/$PROJECT/install-info"
            return 0
        fi
    done
    return 1
}

# hook_target FAMILY: where the package manager hook goes (empty: none)
HOOK_SRC='' HOOK_DEST='' HOOK_MODE_BITS=0644
select_hook() {
    HOOK_SRC='' HOOK_DEST='' HOOK_MODE_BITS=0644
    case $FAMILY in
        arch)
            HOOK_SRC=pacman.hook
            HOOK_DEST=/etc/pacman.d/hooks/kde-infinite-desktop.hook
            ;;
        debian)
            HOOK_SRC=apt.conf
            HOOK_DEST=/etc/apt/apt.conf.d/99kde-infinite-desktop
            ;;
        fedora | rhel | rpm-generic)
            if dnf_is_dnf5; then
                HOOK_SRC=dnf5.actions
                HOOK_DEST=/etc/dnf/libdnf5-plugins/actions.d/kde-infinite-desktop.actions
            else
                HOOK_SRC=dnf4.action
                HOOK_DEST=/etc/dnf/plugins/post-transaction-actions.d/kde-infinite-desktop.action
            fi
            ;;
        suse)
            HOOK_SRC=zypp-commit-plugin.sh
            HOOK_DEST=/usr/lib/zypp/plugins/commit/kde-infinite-desktop
            HOOK_MODE_BITS=0755
            ;;
        alpine)
            HOOK_SRC=apk-commit-hook.sh
            HOOK_DEST=/etc/apk/commit_hooks.d/kde-infinite-desktop.post-commit
            HOOK_MODE_BITS=0755
            ;;
    esac
}

install_helpers() {
    local libdir="$PREFIX/lib/$PROJECT" f
    info "Installing helper scripts to $libdir"
    for f in check-kwin-update.sh kwin-update-hook.sh uninstall.sh; do
        install_root_file 0755 "$SOURCE_DIR/scripts/$f" "$libdir/$f"
    done

    local tmp
    if [ "$AUTO_REBUILD" -eq 1 ]; then
        select_hook
        if [ -n "$HOOK_SRC" ]; then
            info "Installing the automatic rebuild after KWin updates ($HOOK_DEST)"
            tmp=$(mktemp)
            sed "s|/usr/lib/$PROJECT/|$libdir/|g" "$SOURCE_DIR/scripts/hooks/$HOOK_SRC" >"$tmp"
            install_root_file "$HOOK_MODE_BITS" "$tmp" "$HOOK_DEST"
            rm -f "$tmp"
        else
            warn "no package manager hook for '$FAMILY' - the login check will offer the rebuild instead"
        fi
    fi

    if [ "$UPDATE_CHECK" -eq 1 ]; then
        info "Installing the login check ($AUTOSTART_FILE)"
        tmp=$(mktemp)
        cat >"$tmp" <<EOF
[Desktop Entry]
Type=Application
Name=KDE Infinite Desktop: KWin update check
Comment=Rebuilds KDE Infinite Desktop for a new KWin version if the automatic rebuild did not run
Exec=$libdir/check-kwin-update.sh --notify --offer-rebuild --quiet
Icon=org.kde.infinitedesktop
OnlyShowIn=KDE;
NoDisplay=true
X-KDE-autostart-phase=2
EOF
        install_root_file 0644 "$tmp" "$AUTOSTART_FILE"
        rm -f "$tmp"
    elif [ -e "$AUTOSTART_FILE" ]; then
        run_root rm -f -- "$AUTOSTART_FILE"
    fi
}

remove_stale_files() {
    # remove_stale_files OLD NEW: delete files of the previous installation
    # that the new build no longer installs
    local old=$1 new=$2 f
    [ -r "$old" ] && [ -r "$new" ] || return 0
    # CMake writes install_manifest.txt without a final newline
    while IFS= read -r f || [ -n "$f" ]; do
        case $f in
            /*) ;;
            *) continue ;;
        esac
        case $f in *..*) continue ;; esac
        grep -qxF -- "$f" "$new" && continue
        if [ -f "$f" ] || [ -L "$f" ]; then
            run_root rm -f -- "$f"
        fi
    done <"$old"
}

CLEAN_BUILD_DIR=0
# shellcheck disable=SC2329 # invoked via "trap cleanup EXIT" in main()
cleanup() {
    if [ "$CLEAN_BUILD_DIR" -eq 1 ] && [ -n "$BUILD_DIR" ] && [ -d "$BUILD_DIR" ]; then
        rm -rf -- "$BUILD_DIR"
    fi
    if [ -n "$TMP_SOURCE" ] && [ -d "$TMP_SOURCE" ]; then
        rm -rf -- "$TMP_SOURCE"
    fi
}

# ================================================================== main ==
main() {
    parse_args "$@"
    trap cleanup EXIT

    if [ "$ACTION" = uninstall ]; then
        [ "$PREFIX_SET" -eq 1 ] && UNINSTALL_ARGS+=(--prefix "$PREFIX")
        [ -r "$SCRIPT_DIR/uninstall.sh" ] || die "uninstall.sh not found next to install.sh - use $PREFIX/lib/$PROJECT/uninstall.sh"
        exec bash "$SCRIPT_DIR/uninstall.sh" "${UNINSTALL_ARGS[@]}"
    fi

    # --- privileges ---
    IS_ROOT=0
    if [ "$(id -u)" -eq 0 ]; then
        IS_ROOT=1
        if [ -n "${SUDO_USER:-}" ] && [ "$FORCE" -eq 0 ] && [ "$DEPS_ONLY" -eq 0 ] && [ "$HOOK_MODE" -eq 0 ]; then
            die "please run this script as your normal user, not with sudo (it uses sudo itself where needed). Use --force to continue anyway."
        fi
        SUDO=()
    elif have sudo; then
        SUDO=(sudo)
    elif have doas; then
        SUDO=(doas)
    elif have run0; then
        SUDO=(run0)
    else
        [ "$DRY_RUN" -eq 1 ] || die "neither sudo nor doas found - run as root or install sudo"
        SUDO=(sudo)
    fi

    read_os_release || warn "/etc/os-release not found"
    detect_family || FAMILY=unknown

    if [ "$HOOK_MODE" -eq 0 ]; then
        info "KDE Infinite Desktop installer - ${OS_NAME:-unknown system} (family: $FAMILY)"
        [ "$DRY_RUN" -eq 1 ] && info "dry run: nothing will be changed"
    fi

    if [ "$FAMILY" = nixos ]; then
        die "NixOS: /usr is not used. Build with packaging/nix/default.nix (see docs/INSTALL.md)."
    fi
    if [ -e /run/ostree-booted ] || [ "$OS_ID" = steamos ] || { [ "$DEPS_ONLY" -eq 0 ] && usr_is_readonly; }; then
        if [ "$FORCE" -eq 0 ]; then
            die "this system has a read-only /usr (Fedora Kinoite/Atomic, SteamOS, openSUSE Kalpa, ...).
A from-source install into /usr is not possible. Build an RPM with ./build.sh --package and
layer it (rpm-ostree install ./dist/kde-infinite-desktop-*.rpm), see docs/INSTALL.md."
        fi
        warn "read-only /usr detected, continuing because of --force"
    fi

    # --- previous installation (rebuild) ---
    local prev=''
    if prev=$(find_previous_install); then
        if [ "$REBUILD" -eq 1 ]; then
            [ "$HOOK_MODE" -eq 1 ] || info "Rebuild: reusing the settings of $prev"
            read_install_info "$prev"
        else
            # remember the old source copy so it can be replaced
            PREV_SOURCE_DIR=$(sed -n 's/^source_dir=//p' "$prev" | head -n1)
        fi
    elif [ "$REBUILD" -eq 1 ] && [ "$HOOK_MODE" -eq 0 ]; then
        warn "no previous installation found - doing a normal installation"
    fi
    if [ "$REBUILD" -eq 1 ] && [ "$DEPS_FORCED" -eq 0 ]; then
        INSTALL_DEPS=0
    fi

    # --- sources (not needed for --deps-only) ---
    if [ "$DEPS_ONLY" -eq 0 ]; then
        resolve_source
    fi

    # --- KWin / session checks (before installing anything) ---
    local kwin_version msg
    kwin_version=$(installed_kwin_version)
    detect_session
    if [ "$DEPS_ONLY" -eq 0 ] && [ "$HOOK_MODE" -eq 0 ]; then
        if [ -z "$kwin_version" ]; then
            if [ "$FORCE" -eq 0 ] && [ "$DRY_RUN" -eq 0 ] && [ "$WITH_EFFECT" = ON ] && [ "$INSTALL_DEPS" -eq 0 ]; then
                die "KWin is not installed. KDE Infinite Desktop is a KWin plugin and needs KDE Plasma >= ${KWIN_MIN} (Wayland)."
            fi
            warn "could not determine the installed KWin version (is KDE Plasma installed?)"
        else
            info "Installed KWin: $kwin_version"
            if ! version_ge "$kwin_version" "$KWIN_MIN"; then
                msg="KWin $kwin_version is too old - KDE Infinite Desktop needs KWin/Plasma >= ${KWIN_MIN}.
See the version table in docs/INSTALL.md (KDE neon, Arch/CachyOS, openSUSE Tumbleweed,
Fedora Rawhide, Gentoo ~amd64 ... ship ${KWIN_MIN})."
                if [ "$FORCE" -eq 1 ] || [ "$WITH_EFFECT" = OFF ] || [ "$DRY_RUN" -eq 1 ]; then
                    warn "$msg"
                else
                    die "$msg"
                fi
            fi
        fi
        case $SESSION_TYPE in
            wayland) ok "Wayland session" ;;
            x11) warn "you are running an X11 session. KDE Infinite Desktop targets Plasma on Wayland - log into 'Plasma (Wayland)' to use it." ;;
            *) warn "no graphical session detected (SSH/container?) - the effect will be enabled for the next login" ;;
        esac
    fi

    # --- dependencies ---
    if [ "$INSTALL_DEPS" -eq 1 ]; then
        info "Checking build dependencies"
        install_deps
        kwin_version=$(installed_kwin_version)
    elif [ "$HOOK_MODE" -eq 0 ]; then
        info "Skipping dependency installation"
    fi
    if [ "$DEPS_ONLY" -eq 1 ]; then
        ok "build dependencies installed${kwin_version:+ (KWin $kwin_version)}"
        return 0
    fi
    have cmake || [ "$DRY_RUN" -eq 1 ] || die "cmake not found - install the build dependencies (run without --no-deps)"

    # --- configure & build ---
    if [ -z "$BUILD_DIR" ]; then
        if [ "$DRY_RUN" -eq 1 ]; then
            BUILD_DIR="${TMPDIR:-/tmp}/$PROJECT-build.XXXXXX"
        else
            BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/$PROJECT-build.XXXXXX")
            CLEAN_BUILD_DIR=1
        fi
    fi
    local generator=() testing=OFF jobs
    if have ninja || have samu; then
        generator=(-G Ninja)
    fi
    [ "$RUN_TESTS" -eq 1 ] && testing=ON

    info "Configuring (build directory: $BUILD_DIR)"
    run cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" "${generator[@]}" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
        -DBUILD_TESTING="$testing" \
        -DBUILD_EFFECT="$WITH_EFFECT" \
        -DBUILD_KCM="$WITH_KCM" \
        -DBUILD_SETTINGS_APP="$WITH_APP"

    info "Building"
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
    run cmake --build "$BUILD_DIR" --parallel "$jobs"

    if [ "$RUN_TESTS" -eq 1 ]; then
        info "Running unit tests"
        run env QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure
    fi

    # --- install ---
    confirm "Install KDE Infinite Desktop to $PREFIX (and the KWin plugin into the Qt plugin directory)?" || die "aborted"
    info "Installing"
    run_root cmake --install "$BUILD_DIR"

    local manifest="$BUILD_DIR/install_manifest.txt" info_dir="$PREFIX/share/$PROJECT"
    if [ "$DRY_RUN" -eq 0 ]; then
        if ! grep -q "/kwin/effects/plugins/${EFFECT_ID}\.so\$" "$manifest" 2>/dev/null; then
            [ "$WITH_EFFECT" = ON ] && warn "the KWin effect was not part of this build (are the sources in src/effect complete?)"
        fi
        remove_stale_files "$info_dir/install_manifest.txt" "$manifest"
    fi
    install_root_file 0644 "$manifest" "$info_dir/install_manifest.txt"

    # --- sources for the automatic rebuild, helpers, hooks ---
    local version src_copy
    version=$(project_version)
    src_copy="/usr/src/$PROJECT-${version:-unknown}"
    if [ "$HOOK_MODE" -eq 0 ]; then
        persist_source "$src_copy" "$PREV_SOURCE_DIR"
        install_helpers
    else
        src_copy=${PREV_SOURCE_DIR:-$SOURCE_DIR}
    fi

    local info_tmp
    info_tmp=$(mktemp)
    {
        printf 'prefix=%s\n' "$PREFIX"
        printf 'source_dir=%s\n' "$src_copy"
        printf 'version=%s\n' "$version"
        printf 'build_type=%s\n' "$BUILD_TYPE"
        printf 'effect=%s\nkcm=%s\napp=%s\n' "$WITH_EFFECT" "$WITH_KCM" "$WITH_APP"
        printf 'update_check=%s\nauto_rebuild=%s\n' "$UPDATE_CHECK" "$AUTO_REBUILD"
        printf 'kwin_version=%s\n' "$kwin_version"
        printf 'installed=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } >"$info_tmp"
    install_root_file 0644 "$info_tmp" "$info_dir/install-info"
    rm -f "$info_tmp"

    if [ "$HOOK_MODE" -eq 1 ]; then
        ok "rebuilt and installed for KWin ${kwin_version:-?}"
        return 0
    fi

    # --- enable ---
    if [ "$ENABLE_EFFECT" -eq 1 ] && [ "$WITH_EFFECT" = ON ]; then
        if [ "$IS_ROOT" -eq 1 ]; then
            warn "running as root: not enabling the effect for root. As your user run:"
            print_cmd kwriteconfig6 --file kwinrc --group Plugins --key "${EFFECT_ID}Enabled" true --notify
        else
            enable_effect
        fi
    fi

    # --- next steps ---
    local libdir="$PREFIX/lib/$PROJECT"
    echo
    info "Done."
    echo "Next steps:"
    if [ "$RELOGIN_NEEDED" -eq 1 ]; then
        echo "  * Log out and back in: KWin only loads the new build at login"
    fi
    echo "  * Configure: System Settings > Window Management > Desktop Effects > KDE Infinite Desktop"
    if [ "$WITH_APP" = ON ]; then
        echo "    or run: kde-infinite-desktop-settings"
    fi
    echo "  * Enable/disable: same place, or"
    echo "      kwriteconfig6 --file kwinrc --group Plugins --key ${EFFECT_ID}Enabled true|false --notify"
    if [ "$AUTO_REBUILD" -eq 1 ] && [ -n "$HOOK_DEST" ]; then
        echo "  * KWin updates: the plugin is rebuilt automatically ($HOOK_DEST);"
        echo "    log out and back in after a Plasma update. Log: /var/log/kde-infinite-desktop-rebuild.log"
    else
        echo "  * After a KWin/Plasma update run:  sudo $libdir/kwin-update-hook.sh"
    fi
    echo "  * Uninstall:  $libdir/uninstall.sh"
    echo "  * Problems?   journalctl --user -b -g ${EFFECT_ID}   (see docs/INSTALL.md)"
}

# "; exit" on the same line: bash never reads past it, even if this file is
# replaced while it runs (git pull, rebuild from /usr/src).
main "$@"; exit
