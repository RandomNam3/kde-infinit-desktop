#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Developer helper: build KDE Infinite Desktop and run the unit tests in
# containers of several distributions (podman or docker).
#
#   scripts/dev/container-build.sh                    # neon + arch
#   scripts/dev/container-build.sh all                # every distro below
#   scripts/dev/container-build.sh --sanitizers arch  # ASan/UBSan build
#   scripts/dev/container-build.sh --shell tumbleweed # shell after the build
#
# Dependency images are built once (tag localhost/kid-dev-<distro>) by running
# scripts/install.sh --deps-only inside them, so the installer's package lists
# are tested at the same time. Build trees live in named volumes
# (kid-dev-build-<distro>), never in the source tree; the source is mounted
# read-only.
#
# Works from Linux and from Git Bash on Windows (podman.exe / docker.exe).
# The same script is used with --inside by .github/workflows/ci.yml.

set -eu
set -o pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
SOURCE_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd -P)
readonly SCRIPT_DIR SOURCE_DIR

ALL_DISTROS=(neon arch fedora fedora-rawhide tumbleweed debian-sid alma10 alpine-edge)
DEFAULT_DISTROS=(neon arch)

ENGINE=''
EFFECT=auto
SANITIZERS=OFF
REBUILD_IMAGE=0
SHELL_AFTER=0
CLEAN=0
RUN_TESTS=1
INSIDE=0
IMAGE_ONLY=0
BUILD_TYPE=Debug
CMAKE_ARGS=()
DISTROS=()

usage() {
    cat <<EOF
Usage: scripts/dev/container-build.sh [OPTIONS] [DISTRO...]

Distros: ${ALL_DISTROS[*]} | all   (default: ${DEFAULT_DISTROS[*]})

Options:
  --engine E          podman or docker (default: podman, else docker)
  --effect on|off|auto
                      build the KWin effect (auto: only if the container has KWin >= 6.8)
  --sanitizers        configure with -DENABLE_SANITIZERS=ON (ASan + UBSan)
  --build-type T      CMake build type (default: Debug)
  --cmake-arg ARG     extra CMake argument (repeatable)
  --no-tests          build only
  --clean             wipe the build volume first
  --rebuild-image     rebuild the dependency image (e.g. after a KWin update)
  --shell             open a shell in the container after the build
  --image-only        only build the dependency image(s) if missing (used by build.sh)
  --inside            (internal) run the build steps in the current system
  -h, --help          show this help
EOF
}

while [ $# -gt 0 ]; do
    case $1 in
        --engine)
            ENGINE=$2
            shift
            ;;
        --effect)
            EFFECT=$2
            shift
            ;;
        --sanitizers) SANITIZERS=ON ;;
        --build-type)
            BUILD_TYPE=$2
            shift
            ;;
        --cmake-arg)
            CMAKE_ARGS+=("$2")
            shift
            ;;
        --no-tests) RUN_TESTS=0 ;;
        --clean) CLEAN=1 ;;
        --rebuild-image) REBUILD_IMAGE=1 ;;
        --shell) SHELL_AFTER=1 ;;
        --inside) INSIDE=1 ;;
        --image-only) IMAGE_ONLY=1 ;;
        -h | --help)
            usage
            exit 0
            ;;
        all) DISTROS=("${ALL_DISTROS[@]}") ;;
        -*)
            echo "unknown option: $1" >&2
            exit 1
            ;;
        *) DISTROS+=("$1") ;;
    esac
    shift
done
case $EFFECT in on | ON) EFFECT=ON ;; off | OFF) EFFECT=OFF ;; auto) ;; *)
    echo "--effect must be on, off or auto" >&2
    exit 1
    ;;
esac

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# version_ge A B
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

# ============================================================ --inside ==
# Configure, build, test and DESTDIR-install. Used inside the containers and
# directly by CI. Build trees go to $KID_BUILD_ROOT (default /build).
inside() {
    local src=$SOURCE_DIR
    local root=${KID_BUILD_ROOT:-/build}
    local kwin effect=$EFFECT name gen=()
    kwin=$(bash "$src/scripts/check-kwin-update.sh" --print-installed-kwin 2>/dev/null || true)
    if [ "$effect" = auto ]; then
        if [ -n "$kwin" ] && version_ge "$kwin" 6.8; then effect=ON; else effect=OFF; fi
    fi
    name=$([ "$SANITIZERS" = ON ] && echo asan || echo default)
    local build="$root/$name"
    log "KWin ${kwin:-not found} -> BUILD_EFFECT=$effect, sanitizers=$SANITIZERS, build dir $build"
    [ "$CLEAN" -eq 1 ] && rm -rf -- "$build"
    command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)

    cmake -S "$src" -B "$build" "${gen[@]}" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
        -DBUILD_TESTING=ON \
        -DBUILD_EFFECT="$effect" \
        -DENABLE_SANITIZERS="$SANITIZERS" \
        "${CMAKE_ARGS[@]}"
    cmake --build "$build" --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

    if [ "$RUN_TESTS" -eq 1 ]; then
        export QT_QPA_PLATFORM=offscreen
        export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1}
        export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
        ctest --test-dir "$build" --output-on-failure
    fi

    rm -rf -- "$build/destdir"
    DESTDIR="$build/destdir" cmake --install "$build" >/dev/null
    log "installed files (DESTDIR=$build/destdir):"
    (
        cd "$build/destdir" || exit 0
        shopt -s globstar dotglob nullglob
        for f in **; do
            if [ -f "$f" ] || [ -L "$f" ]; then printf '/%s\n' "$f"; fi
        done
    )
}

if [ "$INSIDE" -eq 1 ]; then
    inside
    exit 0
fi

# ============================================================== host side ==
if [ -z "$ENGINE" ]; then
    if command -v podman >/dev/null 2>&1; then
        ENGINE=podman
    elif command -v docker >/dev/null 2>&1; then
        ENGINE=docker
    else
        echo "neither podman nor docker found" >&2
        exit 1
    fi
fi
[ "${#DISTROS[@]}" -gt 0 ] || DISTROS=("${DEFAULT_DISTROS[@]}")

# Git Bash / MSYS on Windows: keep /src etc. untouched and hand the engine a
# Windows path for the bind mount.
HOST_SRC=$SOURCE_DIR
case $(uname -s) in
    MINGW* | MSYS* | CYGWIN*)
        export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
        command -v cygpath >/dev/null 2>&1 && HOST_SRC=$(cygpath -w "$SOURCE_DIR")
        ;;
esac

containerfile() {
    # containerfile DISTRO: print the dependency image definition
    local base pre=''
    case $1 in
        neon)
            base=docker.io/library/ubuntu:24.04
            pre='ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends curl gnupg ca-certificates \
 && curl -fsSL https://archive.neon.kde.org/public.key | gpg --dearmor -o /usr/share/keyrings/neon.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/neon.gpg] http://archive.neon.kde.org/user noble main" > /etc/apt/sources.list.d/neon.list'
            ;;
        arch) base=docker.io/library/archlinux:latest ;;
        fedora) base=docker.io/library/fedora:latest ;;
        fedora-rawhide) base=docker.io/library/fedora:rawhide ;;
        tumbleweed) base=registry.opensuse.org/opensuse/tumbleweed:latest ;;
        debian-sid) base=docker.io/library/debian:sid ;;
        alma10) base=docker.io/library/almalinux:10 ;;
        alpine-edge)
            base=docker.io/library/alpine:edge
            pre='RUN apk add --no-cache bash'
            ;;
        *) return 1 ;;
    esac
    printf 'FROM %s\n' "$base"
    [ -n "$pre" ] && printf '%s\n' "$pre"
    printf '%s\n' 'COPY install.sh check-kwin-update.sh uninstall.sh /tmp/kid/' \
        'RUN bash /tmp/kid/install.sh --deps-only --yes && rm -rf /tmp/kid'
}

build_image() {
    local distro=$1 tag=$2 ctx
    ctx=$(mktemp -d)
    containerfile "$distro" >"$ctx/Containerfile"
    cp "$SOURCE_DIR/scripts/install.sh" "$SOURCE_DIR/scripts/check-kwin-update.sh" \
        "$SOURCE_DIR/scripts/uninstall.sh" "$ctx/"
    local ctx_host=$ctx
    command -v cygpath >/dev/null 2>&1 && ctx_host=$(cygpath -w "$ctx")
    log "building dependency image $tag"
    "$ENGINE" build --pull -t "$tag" -f "$ctx_host/Containerfile" "$ctx_host"
    rm -rf -- "$ctx"
}

failed=()
for distro in "${DISTROS[@]}"; do
    if ! containerfile "$distro" >/dev/null; then
        echo "unknown distro: $distro (known: ${ALL_DISTROS[*]})" >&2
        exit 1
    fi
    tag="localhost/kid-dev-$distro"
    need_image=$REBUILD_IMAGE
    "$ENGINE" image inspect "$tag" >/dev/null 2>&1 || need_image=1
    if [ "$need_image" -eq 1 ]; then
        build_image "$distro" "$tag" || { failed+=("$distro (image)"); continue; }
    fi
    [ "$IMAGE_ONLY" -eq 1 ] && continue

    args=(--inside --effect "$EFFECT" --build-type "$BUILD_TYPE")
    [ "$SANITIZERS" = ON ] && args+=(--sanitizers)
    [ "$RUN_TESTS" -eq 0 ] && args+=(--no-tests)
    [ "$CLEAN" -eq 1 ] && args+=(--clean)
    for a in "${CMAKE_ARGS[@]}"; do args+=(--cmake-arg "$a"); done

    log "[$distro] building"
    tty=()
    [ "$SHELL_AFTER" -eq 1 ] && tty=(-it)
    cmd="bash /src/scripts/dev/container-build.sh $(printf '%q ' "${args[@]}")"
    [ "$SHELL_AFTER" -eq 1 ] && cmd="$cmd; exec bash"
    if "$ENGINE" run --rm "${tty[@]}" \
        -v "$HOST_SRC:/src:ro" \
        -v "kid-dev-build-$distro:/build" \
        --security-opt label=disable \
        "$tag" bash -c "$cmd"; then
        log "[$distro] OK"
    else
        log "[$distro] FAILED"
        failed+=("$distro")
    fi
done

if [ "${#failed[@]}" -gt 0 ]; then
    echo "failed: ${failed[*]}" >&2
    exit 1
fi
log "all builds passed: ${DISTROS[*]}"
