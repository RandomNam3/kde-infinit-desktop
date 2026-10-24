#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# KDE Infinit Desktop - build script for developers and packagers.
#
#   ./build.sh                       configure, build, run the unit tests
#   ./build.sh --install             ... and install + enable (scripts/install.sh)
#   ./build.sh --package             native package of THIS distro -> ./dist/
#   ./build.sh --container arch --package   package built in a container -> ./dist/
#   ./build.sh --container neon      build + tests in a container
#   ./build.sh --sanitize            ASan/UBSan build + tests
#   ./build.sh --clean               remove the build directories and dist/
#
# Build trees live outside the source tree, in
#   ${XDG_CACHE_HOME:-~/.cache}/kde-infinit-desktop/build/<type>
# (use --build-dir build/foo if you really want one inside it).
#
# End users: ./scripts/install.sh is the one-command installer.

set -eu
set -o pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
readonly ROOT PROJECT=kde-infinit-desktop

BUILD_TYPE=Debug
SANITIZE=0
RUN_TESTS=1
DO_INSTALL=0
DO_PACKAGE=0
DO_CLEAN=0
DO_DEPS=0
WITH_APP=1
CONTAINER=''
ENGINE=''
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CMAKE_ARGS=()
DIST_DIR=${KID_DIST_DIR:-$ROOT/dist}
BUILD_DIR=''
BUILD_ROOT=${KID_BUILD_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/kde-infinit-desktop/build}

usage() {
    cat <<EOF
Usage: ./build.sh [OPTIONS]

Default: configure, build and run the unit tests in
$BUILD_ROOT/<type> (outside the source tree).

  --release            Release build (default: Debug)
  --relwithdebinfo     RelWithDebInfo build
  --sanitize           AddressSanitizer + UndefinedBehaviorSanitizer (<build root>/asan)
  --no-tests           do not run the unit tests
  --build-dir DIR      use DIR as build directory
  --cmake-arg ARG      extra CMake argument (repeatable), e.g. --cmake-arg -DBUILD_KCM=OFF
  -j N                 parallel jobs (default: $JOBS)
  --deps               install the build dependencies first (asks for sudo)
  --without-app        do not build the settings app (also for --package)
  --install            install and enable it (scripts/install.sh --no-deps, sets up
                       the automatic rebuild after KWin updates)
  --package            build the native package of this distribution into ./dist/
                       (deb: dpkg-buildpackage, rpm: rpmbuild, Arch: makepkg). The
                       KWin version is part of the package version/file name.
  --container DISTRO   run in a podman/docker container instead:
                       neon arch fedora fedora-rawhide tumbleweed debian-sid alma10 alpine-edge
                       (with --package: package of that distro -> ./dist/)
  --engine E           podman or docker (default: podman, else docker)
  --clean              remove build/ subdirectories of this script and dist/
  -h, --help           show this help

Environment: KID_DIST_DIR (output directory), KID_AUTOREBUILD=0 (Arch: pin
kwin=X.Y.Z instead of the self-rebuilding package, used for prebuilt packages).
EOF
}

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$*" >&2; }
die() {
    printf '\033[1;31mError:\033[0m %s\n' "$*" >&2
    exit 1
}
have() { command -v "$1" >/dev/null 2>&1; }

while [ $# -gt 0 ]; do
    case $1 in
        --release) BUILD_TYPE=Release ;;
        --relwithdebinfo) BUILD_TYPE=RelWithDebInfo ;;
        --debug) BUILD_TYPE=Debug ;;
        --sanitize | --sanitizers) SANITIZE=1 ;;
        --no-tests) RUN_TESTS=0 ;;
        --build-dir)
            BUILD_DIR=$2
            case $BUILD_DIR in /*) ;; *) BUILD_DIR=$PWD/$BUILD_DIR ;; esac
            shift
            ;;
        --cmake-arg)
            CMAKE_ARGS+=("$2")
            shift
            ;;
        -j)
            JOBS=$2
            shift
            ;;
        -j*) JOBS=${1#-j} ;;
        --deps) DO_DEPS=1 ;;
        --without-app) WITH_APP=0 ;;
        --install) DO_INSTALL=1 ;;
        --package) DO_PACKAGE=1 ;;
        --container)
            CONTAINER=$2
            shift
            ;;
        --engine)
            ENGINE=$2
            shift
            ;;
        --clean) DO_CLEAN=1 ;;
        -h | --help)
            usage
            exit 0
            ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

if [ -z "$BUILD_DIR" ]; then
    if [ "$SANITIZE" -eq 1 ]; then
        BUILD_DIR=$BUILD_ROOT/asan
    else
        BUILD_DIR=$BUILD_ROOT/$(printf '%s' "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
    fi
fi

# --------------------------------------------------------------- helpers --
OS_ID='' OS_LIKE=''
read_os_release() {
    local key value
    [ -r /etc/os-release ] || return 0
    while IFS='=' read -r key value; do
        value=${value%\"}
        value=${value#\"}
        case $key in
            ID) OS_ID=$value ;;
            ID_LIKE) OS_LIKE=$value ;;
        esac
    done </etc/os-release
}

# package_family: deb, rpm-fedora, rpm-suse or arch
package_family() {
    local id
    read_os_release
    for id in $OS_ID $OS_LIKE; do
        case $id in
            debian | ubuntu | neon) echo deb && return ;;
            arch | cachyos | manjaro | endeavouros) echo arch && return ;;
            opensuse* | suse) echo rpm-suse && return ;;
            fedora | rhel | centos | almalinux | rocky) echo rpm-fedora && return ;;
        esac
    done
    have dpkg-buildpackage && echo deb && return
    have makepkg && echo arch && return
    have zypper && echo rpm-suse && return
    have rpmbuild && echo rpm-fedora && return
    echo unknown
}

project_version() {
    sed -n 's/^project(kde-infinit-desktop VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -n1
}

kwin_version() {
    bash "$ROOT/scripts/check-kwin-update.sh" --print-installed-kwin 2>/dev/null || true
}

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

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

# stage_source DIR: copy the source tree (without build output) to DIR/kde-infinit-desktop-<ver>
stage_source() {
    local dest
    dest="$1/$PROJECT-$(project_version)"
    mkdir -p "$dest"
    tar -C "$ROOT" \
        --exclude=./build --exclude='./build-*' --exclude=./dist --exclude=./.git \
        --exclude=./.cache --exclude=./.idea --exclude=./.vscode --exclude=./.claude \
        --exclude='./obj-*' --exclude=./debian \
        -cf - . | tar -C "$dest" -xf -
    printf '%s\n' "$dest"
}

# ------------------------------------------------------------ container --
engine() {
    if [ -z "$ENGINE" ]; then
        if have podman; then ENGINE=podman; elif have docker; then ENGINE=docker; else die "neither podman nor docker found"; fi
    fi
    printf '%s\n' "$ENGINE"
}

host_path() {
    if have cygpath; then cygpath -w "$1"; else printf '%s\n' "$1"; fi
}

run_in_container() {
    local eng tag
    eng=$(engine)
    case $(uname -s) in
        MINGW* | MSYS* | CYGWIN*) export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' ;;
    esac
    if [ "$DO_PACKAGE" -eq 0 ]; then
        local args=(--engine "$eng" --build-type "$BUILD_TYPE")
        [ "$SANITIZE" -eq 1 ] && args+=(--sanitizers)
        [ "$RUN_TESTS" -eq 0 ] && args+=(--no-tests)
        [ "$WITH_APP" -eq 1 ] || args+=(--cmake-arg -DBUILD_SETTINGS_APP=OFF)
        local a
        for a in "${CMAKE_ARGS[@]}"; do args+=(--cmake-arg "$a"); done
        exec bash "$ROOT/scripts/dev/container-build.sh" "${args[@]}" "$CONTAINER"
    fi
    # dependency image (built by container-build.sh, reused)
    bash "$ROOT/scripts/dev/container-build.sh" --engine "$eng" --image-only "$CONTAINER"
    tag="localhost/kid-dev-$CONTAINER"
    mkdir -p "$DIST_DIR"
    local pkg_args=()
    [ "$WITH_APP" -eq 1 ] || pkg_args+=(--without-app)
    log "[$CONTAINER] building the package in $tag"
    "$eng" run --rm \
        -v "$(host_path "$ROOT"):/src:ro" \
        -v "$(host_path "$DIST_DIR"):/dist" \
        -e KID_DIST_DIR=/dist \
        -e KID_AUTOREBUILD="${KID_AUTOREBUILD:-1}" \
        --security-opt label=disable \
        "$tag" bash /src/build.sh --package --deps "${pkg_args[@]}"
    log "packages in $DIST_DIR:"
    ls -1 "$DIST_DIR"
}

# -------------------------------------------------------------- package --
package_deb() {
    local stage=$1 src=$2 kwin=$3 effect=$4
    local profiles=()
    cp -r "$ROOT/packaging/debian" "$src/debian"
    # debhelper treats executable config files as scripts (sources copied from
    # Windows or zip archives often have every file executable)
    local f
    for f in "$src"/debian/* "$src"/debian/source/*; do
        [ -f "$f" ] && chmod 0644 "$f"
    done
    chmod 0755 "$src/debian/rules"
    [ "$effect" = ON ] || profiles+=(pkg.kde-infinit-desktop.noeffect)
    if [ "$WITH_APP" -eq 0 ] || [ ! -r "$ROOT/src/app/CMakeLists.txt" ]; then
        profiles+=(pkg.kde-infinit-desktop.noapp)
    fi
    if [ "$effect" = ON ] && [ -n "$kwin" ]; then
        # 0.1.0-1 -> 0.1.0-1+kwin6.8.1: the package is built for exactly this KWin
        sed -i "1s/(\([^)]*\))/(\1+kwin$kwin)/" "$src/debian/changelog"
    fi
    local prof_csv
    prof_csv=$(
        IFS=,
        printf '%s' "${profiles[*]}"
    )
    if [ "$DO_DEPS" -eq 1 ]; then
        log "installing packaging tools and build dependencies (apt)"
        as_root env DEBIAN_FRONTEND=noninteractive apt-get update
        as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends dpkg-dev debhelper fakeroot
        (cd "$src" && as_root env DEBIAN_FRONTEND=noninteractive apt-get build-dep -y ${prof_csv:+-P "$prof_csv"} ./)
    fi
    log "dpkg-buildpackage${prof_csv:+ (profiles: $prof_csv)}"
    (cd "$src" && DEB_BUILD_PROFILES="${profiles[*]}" dpkg-buildpackage -us -uc -b)
    local f
    for f in "$stage"/*.deb; do
        case $f in *-dbgsym_*) continue ;; esac
        cp -v "$f" "$DIST_DIR/"
    done
}

package_rpm() {
    local stage=$1 src=$2 flavour=$3 effect=$4
    local spec top ver opts=()
    ver=$(project_version)
    top="$stage/rpmbuild"
    mkdir -p "$top"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}
    tar -C "$stage" -czf "$top/SOURCES/$PROJECT-$ver.tar.gz" "$PROJECT-$ver"
    if [ "$flavour" = suse ]; then
        spec="$ROOT/packaging/opensuse/$PROJECT.spec"
    else
        spec="$ROOT/packaging/fedora/$PROJECT.spec"
    fi
    cp "$spec" "$top/SPECS/"
    spec="$top/SPECS/$PROJECT.spec"
    # %bcond_without X is switched off by defining _without_X (works for rpmspec too)
    [ "$effect" = ON ] || opts+=(--define "_without_effect 1")
    if [ "$WITH_APP" -eq 0 ] || [ ! -r "$ROOT/src/app/CMakeLists.txt" ]; then
        opts+=(--define "_without_app 1")
    fi
    if [ "$DO_DEPS" -eq 1 ]; then
        log "installing packaging tools and build dependencies"
        local caps=() cap
        if [ "$flavour" = suse ]; then
            as_root zypper --non-interactive --gpg-auto-import-keys install rpm-build
        else
            as_root dnf install -y rpm-build
        fi
        while IFS= read -r cap; do
            [ -n "$cap" ] && caps+=("$cap")
        done < <(rpmspec -q --buildrequires "${opts[@]}" "$spec")
        if [ "$flavour" = suse ]; then
            as_root zypper --non-interactive --gpg-auto-import-keys install "${caps[@]}"
        else
            as_root dnf install -y "${caps[@]}"
        fi
    fi
    log "rpmbuild ${opts[*]}"
    rpmbuild -bb --define "_topdir $top" "${opts[@]}" "$spec"
    local f
    for f in "$top"/RPMS/*/*.rpm; do
        case $f in *-debuginfo-* | *-debugsource-*) continue ;; esac
        [ -e "$f" ] && cp -v "$f" "$DIST_DIR/"
    done
}

package_arch() {
    local stage=$1 src=$2 kwin=$3
    [ "$WITH_APP" -eq 1 ] || echo "Warning: --without-app is not supported by the PKGBUILD (ignored)" >&2
    local ver pkgdir="$stage/pkg" f
    ver=$(project_version)
    mkdir -p "$pkgdir"
    cp "$ROOT/packaging/arch/PKGBUILD" "$ROOT/packaging/arch/kde-infinit-desktop.install" "$pkgdir/"
    # makepkg uses a local file with the name from source=() instead of downloading
    tar -C "$stage" -czf "$pkgdir/$PROJECT-$ver.tar.gz" "$PROJECT-$ver"
    if [ "$DO_DEPS" -eq 1 ]; then
        log "installing build dependencies (pacman)"
        as_root pacman -S --needed --noconfirm base-devel cmake ninja extra-cmake-modules vulkan-headers \
            kwin qt6-base qt6-declarative qt6-svg kconfig kcoreaddons kglobalaccel ki18n kcmutils \
            kconfigwidgets kxmlgui kwidgetsaddons
    fi
    if [ "$(id -u)" -eq 0 ]; then
        # makepkg refuses to run as root (containers): use a throw-away user
        id kidbuild >/dev/null 2>&1 || useradd -m kidbuild
        chown -R kidbuild "$stage"
        (cd "$pkgdir" && runuser -u kidbuild -- env KID_AUTOREBUILD="${KID_AUTOREBUILD:-1}" makepkg -f --noconfirm)
    else
        (cd "$pkgdir" && makepkg -f --noconfirm)
    fi
    for f in "$pkgdir"/*.pkg.tar.*; do
        [ -e "$f" ] || continue
        case $f in *-debug-*) continue ;; esac
        local base=${f##*/}
        # kde-infinit-desktop-0.1.0-1-x86_64.pkg.tar.zst -> ...-0.1.0+kwin6.8.1-1-x86_64...
        [ -n "$kwin" ] && base=${base/-$ver-/-$ver+kwin$kwin-}
        cp -v "$f" "$DIST_DIR/$base"
    done
}

do_package() {
    local family kwin effect=ON stage src
    family=$(package_family)
    kwin=$(kwin_version)
    if [ -z "$kwin" ] || ! version_ge "$kwin" 6.8; then
        warn "KWin ${kwin:-not found} < 6.8: building the package WITHOUT the KWin effect"
        effect=OFF
    fi
    mkdir -p "$DIST_DIR"
    stage=$(mktemp -d "${TMPDIR:-/tmp}/$PROJECT-pkg.XXXXXX")
    src=$(stage_source "$stage")
    log "packaging $PROJECT $(project_version) for $family (KWin ${kwin:-none}) in $stage"
    case $family in
        deb) package_deb "$stage" "$src" "$kwin" "$effect" ;;
        rpm-fedora) package_rpm "$stage" "$src" fedora "$effect" ;;
        rpm-suse) package_rpm "$stage" "$src" suse "$effect" ;;
        arch) package_arch "$stage" "$src" "$kwin" ;;
        *) die "no packaging for this distribution - use scripts/install.sh or --container" ;;
    esac
    rm -rf -- "$stage" 2>/dev/null || true
    log "done: $DIST_DIR"
}

# ----------------------------------------------------------------- main --
if [ "$DO_CLEAN" -eq 1 ]; then
    for d in "$BUILD_ROOT/debug" "$BUILD_ROOT/release" "$BUILD_ROOT/relwithdebinfo" "$BUILD_ROOT/asan" "$DIST_DIR"; do
        if [ -d "$d" ]; then
            log "removing $d"
            rm -rf -- "$d"
        fi
    done
    if [ "$DO_PACKAGE" -eq 0 ] && [ "$DO_INSTALL" -eq 0 ] && [ -z "$CONTAINER" ]; then
        exit 0
    fi
fi

if [ -n "$CONTAINER" ]; then
    run_in_container
    exit 0
fi

if [ "$DO_DEPS" -eq 1 ] && [ "$DO_PACKAGE" -eq 0 ]; then
    bash "$ROOT/scripts/install.sh" --deps-only
fi

if [ "$DO_PACKAGE" -eq 1 ]; then
    do_package
    exit 0
fi

effect=ON
kwin=$(kwin_version)
if [ -z "$kwin" ] || ! version_ge "$kwin" 6.8; then
    warn "KWin ${kwin:-not found} < 6.8: building without the KWin effect"
    effect=OFF
fi
gen=()
have ninja && gen=(-G Ninja)
san=OFF
[ "$SANITIZE" -eq 1 ] && san=ON
app=ON
[ "$WITH_APP" -eq 1 ] || app=OFF

log "configure ($BUILD_TYPE, sanitizers $san, effect $effect) -> $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" "${gen[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
    -DBUILD_TESTING=ON \
    -DBUILD_EFFECT="$effect" \
    -DBUILD_SETTINGS_APP="$app" \
    -DENABLE_SANITIZERS="$san" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "${CMAKE_ARGS[@]}"
log "build"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [ "$RUN_TESTS" -eq 1 ]; then
    log "unit tests"
    QT_QPA_PLATFORM=offscreen \
        ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} \
        UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1} \
        ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [ "$DO_INSTALL" -eq 1 ]; then
    [ "$SANITIZE" -eq 0 ] || die "--install with --sanitize makes no sense (KWin would load an ASan plugin)"
    bash "$ROOT/scripts/install.sh" --no-deps --build-dir "$BUILD_DIR" --build-type "$BUILD_TYPE"
fi
