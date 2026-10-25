#!/bin/bash
# Runs inside the runtime container: build+install the plugin, start a virtual
# KWin session and execute a scenario script inside it.
# usage: session.sh <scenario.sh> [outputs]
set -u
SCENARIO=$1
OUTPUTS=${2:-1}

# --- build & install plugin and test helper (incremental, build dirs are volumes)
if [ -z "${SKIP_BUILD:-}" ]; then
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
        -DBUILD_KCM=OFF -DBUILD_SETTINGS_APP=OFF -DBUILD_TESTING=OFF ${CMAKE_EXTRA:-} > /tmp/cfg.log 2>&1 || { tail -30 /tmp/cfg.log; exit 1; }
  cmake --build /build > /tmp/build.log 2>&1 || { grep -E "error" /tmp/build.log | head -30; exit 1; }
fi
cmake --install /build > /dev/null 2>&1 || { echo "install failed"; exit 1; }
cmake -S /th -B /thbuild -G Ninja > /dev/null 2>&1 && cmake --build /thbuild > /tmp/th.log 2>&1 && cmake --install /thbuild --prefix /usr/local > /dev/null 2>&1 || { tail -20 /tmp/th.log; exit 1; }

export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export HOME=/tmp/home
mkdir -p "$HOME/.config"
export XDG_CONFIG_HOME=$HOME/.config
export XDG_STATE_HOME=$HOME/.local/state
export QT_LOGGING_RULES="kwin_effect_infinitdesktop.debug=true"
# Qt logs to the (missing) journal otherwise.
export QT_FORCE_STDERR_LOGGING=1
# KWin's internal QtQuick windows (OSD messages) cannot get a GL context on
# llvmpipe+vgem in the container; render them in software.
export QT_QUICK_BACKEND=software

kwriteconfig6 --file kwinrc --group Plugins --key infinitdesktopEnabled true
kwriteconfig6 --file kwinrc --group Effect-infinitdesktop --key DebugLogging true
# Deterministic tests: no camera animations unless a scenario asks for them.
kwriteconfig6 --file kwinrc --group Effect-infinitdesktop --key AnimationDuration "${ANIMATION_MS:-0}"
[ -n "${EXTRA_CONFIG:-}" ] && eval "$EXTRA_CONFIG"

run() {
  if [ -n "${USE_GDB:-}" ]; then
    gdb -q -batch -ex "set debuginfod enabled off" -ex "handle SIGPIPE nostop noprint" -ex run -ex "thread apply all bt 25" \
        --args kwin_wayland --virtual --width 1600 --height 900 --output-count "$OUTPUTS" --no-lockscreen --xwayland > /tmp/kwin.log 2>&1 &
  elif [ -n "${USE_ASAN:-}" ]; then
    # The plugin is built with -fsanitize=address,undefined; KWin itself is not, so the runtime is preloaded.
    LD_PRELOAD="$(gcc -print-file-name=libasan.so):$(gcc -print-file-name=libubsan.so)" \
    ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:halt_on_error=1:log_path=/out/asan" \
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:log_path=/out/ubsan" \
      kwin_wayland --virtual --width 1600 --height 900 --output-count "$OUTPUTS" --no-lockscreen --xwayland > /tmp/kwin.log 2>&1 &
  else
    kwin_wayland --virtual --width 1600 --height 900 --output-count "$OUTPUTS" --no-lockscreen --xwayland > /tmp/kwin.log 2>&1 &
  fi
  KWIN_PID=$!
  for _ in $(seq 1 100); do
    [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 && break
    sleep 0.1
  done
  export WAYLAND_DISPLAY=wayland-0
  # Older KWin logs the display of its Xwayland; KWin 6.8 does not: it is on Xwayland's
  # command line.
  DISPLAY=$(grep -o 'DISPLAY=:[0-9]*' /tmp/kwin.log | head -1 | cut -d= -f2)
  for _ in $(seq 1 50); do
    [ -n "$DISPLAY" ] && break
    DISPLAY=$(pgrep -a Xwayland | grep -o ' :[0-9]*' | head -1 | tr -d ' ')
    sleep 0.1
  done
  export DISPLAY=${DISPLAY:-:0}
  sleep 1
  echo "--- effect loaded: $(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded infinitdesktop)"
  bash "$SCENARIO"
  local rc=$?
  kill $KWIN_PID 2>/dev/null
  wait $KWIN_PID 2>/dev/null
  return $rc
}
run
rc=$?
echo "--- kwin log (infinitdesktop / errors):"
grep -iE "${LOG_FILTER:-infinit|crash|segfault|ASSERT|fatal|blocked|flung|restored|navigation}" /tmp/kwin.log | grep -v "^\[" | head -${LOG_LINES:-40}
exit $rc
