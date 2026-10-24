#!/bin/bash
# Runs the runtime scenarios in a headless KWin inside a container.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tests/runtime/run.sh [--asan] [scenario numbers...]   (default: 1 2 3 4 5 6)
# Results (screenshots, sanitizer logs) go to ${OUT:-/tmp/kid-runtime-tests}/s<N>/.
# Scenario 5 runs with other KWin effects enabled. Set BETTER_BLUR_SRC to a checkout of
# Better Blur DX (github.com/xarblu/kwin-effects-better-blur-dx) to test with it instead
# of the built-in blur.
set -u

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/../.." && pwd)
engine=$(command -v podman || command -v docker || true)
[ -n "$engine" ] || { echo "podman or docker is required" >&2; exit 1; }
image=localhost/kid-runtime-tests:latest
out=${OUT:-/tmp/kid-runtime-tests}
asan=""
scenarios=()
for arg in "$@"; do
    case "$arg" in
        --asan) asan=1 ;;
        *) scenarios+=("$arg") ;;
    esac
done
[ ${#scenarios[@]} -eq 0 ] && scenarios=(1 2 3 4 5 6)

# KWin needs a DRM render node; the virtual GEM driver provides one without a GPU.
[ -e /dev/dri ] || sudo modprobe vgem

"$engine" image inspect "$image" >/dev/null 2>&1 || "$engine" build -t "$image" -f "$here/Containerfile" "$here"

failures=0
for s in "${scenarios[@]}"; do
    outputs=1
    [ "$s" = 2 ] && outputs=2
    mkdir -p "$out/s$s"
    echo "=== scenario $s"
    env_args=()
    volume=kid-runtime-build
    if [ -n "$asan" ]; then
        env_args=(-e USE_ASAN=1 -e "CMAKE_EXTRA=-DENABLE_SANITIZERS=ON")
        volume=kid-runtime-build-asan
    fi
    session=session.sh
    if [ "$s" = 5 ]; then
        session=compat-session.sh
        [ -n "${BETTER_BLUR_SRC:-}" ] && env_args+=(-v "$BETTER_BLUR_SRC:/betterblur:ro" -v kid-runtime-betterblur:/bbbuild)
    fi
    "$engine" run --rm --device /dev/dri "${env_args[@]}" \
        -v "$project:/src:ro" -v "$here:/th:ro" -v "$out/s$s:/out" \
        -v "$volume:/build" -v kid-runtime-harness:/thbuild \
        "$image" dbus-run-session -- bash "/th/$session" "/th/scenario$s.sh" "$outputs" \
        | grep -E "PASS|FAIL|FAILURES|INFO|error|Segmentation|Abort|Sanitizer"
    [ "${PIPESTATUS[0]}" -eq 0 ] || failures=$((failures + 1))
done
echo
echo "screenshots and logs: $out"
exit $failures
